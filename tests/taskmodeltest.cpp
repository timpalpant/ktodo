/*
 * Tests for the task hierarchy drop and collapse contracts.
 *
 * These cases exercise the model against the same optimistic Repository used
 * by the GUI: a hierarchy change must update parent_id locally and queue the
 * matching Todoist move/reorder commands before any network sync happens.
 */

#include "data/database.h"
#include "data/repository.h"
#include "models/taskmodel.h"
#include "sync/commandqueue.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

QJsonObject task(const QString &id, const QString &parentId, int order, const QString &sectionId = {})
{
    QJsonObject out{
        {QStringLiteral("id"), id},
        {QStringLiteral("content"), id},
        {QStringLiteral("project_id"), QStringLiteral("project")},
        {QStringLiteral("child_order"), order},
        {QStringLiteral("priority"), 1},
    };
    if (!parentId.isEmpty()) {
        out.insert(QStringLiteral("parent_id"), parentId);
    }
    if (!sectionId.isEmpty()) {
        out.insert(QStringLiteral("section_id"), sectionId);
    }
    return out;
}

QJsonObject completedTask(const QString &id, const QString &parentId, int order)
{
    QJsonObject out = task(id, parentId, order);
    out.insert(QStringLiteral("checked"), true);
    return out;
}

QJsonObject datedTask(const QString &id, const QDate &date, int order)
{
    QJsonObject out = task(id, {}, order);
    out.insert(QStringLiteral("due"), QJsonObject{{QStringLiteral("date"), date.toString(Qt::ISODate)}});
    return out;
}

QJsonObject collapsedTask(const QString &id, const QString &parentId, int order)
{
    QJsonObject out = task(id, parentId, order);
    out.insert(QStringLiteral("is_collapsed"), true);
    return out;
}

int rowFor(const TaskModel &model, const QString &id)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row, 0), TaskModel::TaskIdRole).toString() == id) {
            return row;
        }
    }
    return -1;
}

int headerFor(const TaskModel &model, const QString &id)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row, 0), TaskModel::HeaderIdRole).toString() == id) {
            return row;
        }
    }
    return -1;
}

void load(const QJsonArray &items, const QJsonArray &sections = {})
{
    Repository::instance()->applySyncPayload(QJsonObject{
        {QStringLiteral("projects"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("project")},
             {QStringLiteral("name"), QStringLiteral("Project")},
         }}},
        {QStringLiteral("sections"), sections},
        {QStringLiteral("items"), items},
    });
}

void configureProjectModel(TaskModel &model)
{
    model.setMode(TaskModel::ProjectTasks);
    model.setProjectId(QStringLiteral("project"));
    QCoreApplication::processEvents();
}

} // namespace

class TaskModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void middleDropMakesTaskASubtask();
    void edgeDropPromotesSubtaskToRoot();
    void cannotNestTaskBelowItsOwnDescendant();
    void lowerEdgeOfParentUsesParentSiblingScope();
    void bottomBoundaryKeepsRootTaskAtRoot();
    void headerEdgeTargetsSectionRoot();

    void showCompletedAddsCompletedTasksWithoutHidingActiveOnes();
    void showCompletedRevealsCompletedSubtasks();
    void showCompletedAppliesToDateAndLabelViews();
    void completedTasksFetchedSeparatelyJoinTheList();
    void upcomingCalendarNavigationFindsDaysAndCountsTasks();
    void upcomingGroupsAndCollapsesOverdueTasks();

    void collapsingHidesSubtasksAndSyncsTheState();
    void subtaskCountsIncludeHiddenCompletedChildren();
    void collapsedSubtreeStillCountsTowardTheNestingLimit();
    void droppingIntoACollapsedTaskRevealsIt();
    void collapsingInAReadOnlyProjectStaysLocal();

    void noOpSyncEmitsNothing();
    void editingATaskUpdatesInPlaceWithoutReset();
    void addingATaskInsertsWithoutReset();
    void completingATaskRemovesItsRowWithoutReset();
    void reorderingFallsBackToAResetButEndsUpCorrect();

private:
    QTemporaryDir m_temp;
};

void TaskModelTest::initTestCase()
{
    QVERIFY2(m_temp.isValid(), "Could not create temporary cache directory");
    QVERIFY2(Database::instance().open(m_temp.filePath(QStringLiteral("test.sqlite"))), qPrintable(Database::instance().lastError()));
}

void TaskModelTest::init()
{
    Database::instance().wipe();
    CommandQueue::instance()->clear();
}

void TaskModelTest::middleDropMakesTaskASubtask()
{
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
        task(QStringLiteral("beta"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    const int alpha = rowFor(model, QStringLiteral("alpha"));
    const int beta = rowFor(model, QStringLiteral("beta"));
    QVERIFY(alpha >= 0);
    QVERIFY(beta >= 0);
    QVERIFY(model.canDrop(alpha, beta, beta, true));

    model.commitDrop(alpha, beta, beta, true);

    QCOMPARE(Repository::instance()->item(QStringLiteral("alpha")).parentId, QStringLiteral("beta"));
    const QVector<PendingCommand> commands = CommandQueue::instance()->take();
    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.at(0).type, QStringLiteral("item_move"));
    QCOMPARE(commands.at(0).args.value(QStringLiteral("parent_id")).toString(), QStringLiteral("beta"));
    QCOMPARE(commands.at(1).type, QStringLiteral("item_reorder"));
}

void TaskModelTest::edgeDropPromotesSubtaskToRoot()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("child"), QStringLiteral("parent"), 0),
        task(QStringLiteral("other"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    const int child = rowFor(model, QStringLiteral("child"));
    const int other = rowFor(model, QStringLiteral("other"));
    QVERIFY(child >= 0);
    QVERIFY(other >= 0);
    // Top edge of a root task: the child becomes its root-level sibling.
    QVERIFY(model.canDrop(child, other, other, false));

    model.commitDrop(child, other, other, false);

    const Todoist::Item moved = Repository::instance()->item(QStringLiteral("child"));
    QVERIFY(moved.parentId.isEmpty());
    QCOMPARE(moved.projectId, QStringLiteral("project"));
}

void TaskModelTest::cannotNestTaskBelowItsOwnDescendant()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("child"), QStringLiteral("parent"), 0),
    });
    TaskModel model;
    configureProjectModel(model);

    const int parent = rowFor(model, QStringLiteral("parent"));
    const int child = rowFor(model, QStringLiteral("child"));
    QVERIFY(parent >= 0);
    QVERIFY(child >= 0);
    QVERIFY(!model.canDrop(parent, child, child, true));
}

void TaskModelTest::lowerEdgeOfParentUsesParentSiblingScope()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("child"), QStringLiteral("parent"), 0),
        task(QStringLiteral("other"), {}, 1),
        task(QStringLiteral("third"), {}, 2),
    });
    TaskModel model;
    configureProjectModel(model);

    const int parent = rowFor(model, QStringLiteral("parent"));
    const int third = rowFor(model, QStringLiteral("third"));
    QVERIFY(parent >= 0);
    QVERIFY(third >= 0);
    // Passing an insert boundary after parent but before its flattened child
    // mirrors the parent's lower mouse edge. `targetIndex` keeps the drop in
    // the root scope and places third before the next root task, "other".
    QVERIFY(model.canDrop(third, parent + 1, parent, false));

    model.commitDrop(third, parent + 1, parent, false);

    const Todoist::Item moved = Repository::instance()->item(QStringLiteral("third"));
    QVERIFY(moved.parentId.isEmpty());
    QCOMPARE(moved.childOrder, 1);
    QCOMPARE(Repository::instance()->item(QStringLiteral("other")).childOrder, 2);
}

void TaskModelTest::bottomBoundaryKeepsRootTaskAtRoot()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("child"), QStringLiteral("parent"), 0),
        task(QStringLiteral("other"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    const int other = rowFor(model, QStringLiteral("other"));
    QVERIFY(other >= 0);
    // Outside the final row there is no task target. The nearest flattened
    // row is `child`, but that must not turn `other` into a child of parent.
    QVERIFY(model.canDrop(other, model.rowCount(), -1, false));

    model.commitDrop(other, model.rowCount(), -1, false);

    QVERIFY(Repository::instance()->item(QStringLiteral("other")).parentId.isEmpty());
}

void TaskModelTest::headerEdgeTargetsSectionRoot()
{
    load(
        QJsonArray{
            task(QStringLiteral("parent"), {}, 0, QStringLiteral("first")),
            task(QStringLiteral("child"), QStringLiteral("parent"), 0, QStringLiteral("first")),
            task(QStringLiteral("candidate"), {}, 0, QStringLiteral("second")),
        },
        QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("first")},
                {QStringLiteral("name"), QStringLiteral("First")},
                {QStringLiteral("project_id"), QStringLiteral("project")},
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("second")},
                {QStringLiteral("name"), QStringLiteral("Second")},
                {QStringLiteral("project_id"), QStringLiteral("project")},
            },
        });
    TaskModel model;
    configureProjectModel(model);

    const int candidate = rowFor(model, QStringLiteral("candidate"));
    const int secondHeader = headerFor(model, QStringLiteral("second"));
    QVERIFY(candidate >= 0);
    QVERIFY(secondHeader >= 0);
    // Top edge of a section header moves to the preceding section's root
    // list, rather than becoming a child of that section's last subtask.
    QVERIFY(model.canDrop(candidate, secondHeader, secondHeader, false));

    model.commitDrop(candidate, secondHeader, secondHeader, false);

    const Todoist::Item moved = Repository::instance()->item(QStringLiteral("candidate"));
    QVERIFY(moved.parentId.isEmpty());
    QCOMPARE(moved.sectionId, QStringLiteral("first"));
}

void TaskModelTest::showCompletedAddsCompletedTasksWithoutHidingActiveOnes()
{
    load(QJsonArray{
        task(QStringLiteral("active"), {}, 0),
        completedTask(QStringLiteral("done"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    QVERIFY(rowFor(model, QStringLiteral("active")) >= 0);
    QCOMPARE(rowFor(model, QStringLiteral("done")), -1);

    model.setShowCompleted(true);
    QCoreApplication::processEvents();

    // The toggle widens the list; it must not trade the active tasks away for
    // the completed ones.
    QVERIFY(rowFor(model, QStringLiteral("active")) >= 0);
    QVERIFY(rowFor(model, QStringLiteral("done")) >= 0);
    // Completed tasks sort below the active ones, whatever order they carry.
    QVERIFY(rowFor(model, QStringLiteral("active")) < rowFor(model, QStringLiteral("done")));

    model.setShowCompleted(false);
    QCoreApplication::processEvents();
    QVERIFY(rowFor(model, QStringLiteral("active")) >= 0);
    QCOMPARE(rowFor(model, QStringLiteral("done")), -1);
}

void TaskModelTest::showCompletedRevealsCompletedSubtasks()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("open"), QStringLiteral("parent"), 0),
        completedTask(QStringLiteral("finished"), QStringLiteral("parent"), 1),
    });
    TaskModel model;
    configureProjectModel(model);

    QCOMPARE(rowFor(model, QStringLiteral("finished")), -1);

    model.setShowCompleted(true);
    QCoreApplication::processEvents();

    const int parent = rowFor(model, QStringLiteral("parent"));
    const int finished = rowFor(model, QStringLiteral("finished"));
    QVERIFY(parent >= 0);
    QVERIFY(finished > parent);
    // Still nested under its parent rather than promoted to a row of its own.
    QCOMPARE(model.data(model.index(finished, 0), TaskModel::DepthRole).toInt(), 1);
    QVERIFY(model.data(model.index(finished, 0), TaskModel::CheckedRole).toBool());
}

void TaskModelTest::showCompletedAppliesToDateAndLabelViews()
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);

    QJsonObject open = task(QStringLiteral("open"), {}, 0);
    open.insert(QStringLiteral("due"), QJsonObject{{QStringLiteral("date"), today}});
    open.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("errands")});

    QJsonObject done = completedTask(QStringLiteral("done"), {}, 1);
    done.insert(QStringLiteral("due"), QJsonObject{{QStringLiteral("date"), today}});
    done.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("errands")});

    load(QJsonArray{open, done});

    // The toggle is offered on every task list, so it has to mean something on
    // the views that are not a project.
    TaskModel todayModel;
    todayModel.setMode(TaskModel::Today);
    QCoreApplication::processEvents();
    QCOMPARE(rowFor(todayModel, QStringLiteral("done")), -1);
    todayModel.setShowCompleted(true);
    QCoreApplication::processEvents();
    QVERIFY(rowFor(todayModel, QStringLiteral("open")) >= 0);
    QVERIFY(rowFor(todayModel, QStringLiteral("done")) >= 0);

    TaskModel labelModel;
    labelModel.setMode(TaskModel::LabelTasks);
    labelModel.setLabelName(QStringLiteral("errands"));
    QCoreApplication::processEvents();
    QCOMPARE(rowFor(labelModel, QStringLiteral("done")), -1);
    labelModel.setShowCompleted(true);
    QCoreApplication::processEvents();
    QVERIFY(rowFor(labelModel, QStringLiteral("open")) >= 0);
    QVERIFY(rowFor(labelModel, QStringLiteral("done")) >= 0);
}

void TaskModelTest::completedTasksFetchedSeparatelyJoinTheList()
{
    load(QJsonArray{task(QStringLiteral("active"), {}, 0)});

    TaskModel model;
    configureProjectModel(model);
    model.setShowCompleted(true);
    QCoreApplication::processEvents();
    QCOMPARE(rowFor(model, QStringLiteral("archived")), -1);

    // The completed-tasks endpoint reports completion through completed_at and
    // does not always set `checked`, so the repository has to infer it.
    Repository::instance()->applyCompletedItems(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("archived")},
        {QStringLiteral("content"), QStringLiteral("archived")},
        {QStringLiteral("project_id"), QStringLiteral("project")},
        {QStringLiteral("completed_at"), QStringLiteral("2026-08-01T09:00:00.000000Z")},
    }});
    QCoreApplication::processEvents();

    const int archived = rowFor(model, QStringLiteral("archived"));
    QVERIFY(archived >= 0);
    QVERIFY(model.data(model.index(archived, 0), TaskModel::CheckedRole).toBool());

    // ...and drops back out of sight when the toggle goes off again.
    model.setShowCompleted(false);
    QCoreApplication::processEvents();
    QCOMPARE(rowFor(model, QStringLiteral("archived")), -1);
    QVERIFY(rowFor(model, QStringLiteral("active")) >= 0);
}

void TaskModelTest::upcomingCalendarNavigationFindsDaysAndCountsTasks()
{
    const QDate today = QDate::currentDate();
    const QDate nextWeek = today.addDays(7);
    const QDate farFuture = today.addDays(90);
    load(QJsonArray{
        datedTask(QStringLiteral("first"), today, 0),
        datedTask(QStringLiteral("second"), nextWeek, 1),
        datedTask(QStringLiteral("third"), nextWeek, 2),
        // Upcoming is not an arbitrary 30-day window: the full future agenda
        // must remain reachable from the calendar popup.
        datedTask(QStringLiteral("later"), farFuture, 3),
    });

    TaskModel model;
    model.setMode(TaskModel::Upcoming);
    QCoreApplication::processEvents();

    QCOMPARE(model.taskCountForDate(today), 1);
    QCOMPARE(model.taskCountForDate(nextWeek), 2);
    QCOMPARE(model.taskCountForDate(today.addDays(1)), 0);

    const int nextWeekHeader = model.rowForDate(nextWeek);
    QVERIFY(nextWeekHeader >= 0);
    QCOMPARE(model.dateForRow(nextWeekHeader), nextWeek);
    QCOMPARE(model.dateForRow(nextWeekHeader + 1), nextWeek);

    // Tapping an empty day advances to the next populated agenda group.
    QCOMPARE(model.rowForDate(today.addDays(1)), nextWeekHeader);
    QVERIFY(model.rowForDate(farFuture) >= 0);
    QCOMPARE(model.dateForRow(model.rowForDate(farFuture)), farFuture);
}

void TaskModelTest::upcomingGroupsAndCollapsesOverdueTasks()
{
    const QDate today = QDate::currentDate();
    load(QJsonArray{
        datedTask(QStringLiteral("late"), today.addDays(-2), 0),
        datedTask(QStringLiteral("current"), today, 1),
    });

    TaskModel model;
    model.setMode(TaskModel::Upcoming);
    QCoreApplication::processEvents();

    const int overdueHeader = headerFor(model, QStringLiteral("overdue"));
    QCOMPARE(overdueHeader, 0);
    QCOMPARE(model.data(model.index(overdueHeader, 0), TaskModel::HeaderTextRole).toString(), QStringLiteral("Overdue"));
    QVERIFY(model.data(model.index(overdueHeader, 0), TaskModel::CanCollapseRole).toBool());
    QVERIFY(!model.data(model.index(overdueHeader, 0), TaskModel::IsCollapsedRole).toBool());
    QVERIFY(rowFor(model, QStringLiteral("late")) > overdueHeader);
    QCOMPARE(model.taskCount(), 2);

    model.toggleOverdueCollapsed();

    QCOMPARE(headerFor(model, QStringLiteral("overdue")), 0);
    QVERIFY(model.data(model.index(0, 0), TaskModel::IsCollapsedRole).toBool());
    QCOMPARE(rowFor(model, QStringLiteral("late")), -1);
    QVERIFY(rowFor(model, QStringLiteral("current")) >= 0);
    QCOMPARE(model.taskCount(), 2);
    QCOMPARE(model.taskCountForDate(today.addDays(-2)), 1);

    model.toggleOverdueCollapsed();
    QVERIFY(rowFor(model, QStringLiteral("late")) > 0);
}

void TaskModelTest::collapsingHidesSubtasksAndSyncsTheState()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("child"), QStringLiteral("parent"), 0),
        task(QStringLiteral("other"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    const int parent = rowFor(model, QStringLiteral("parent"));
    QVERIFY(parent >= 0);
    QVERIFY(model.data(model.index(parent, 0), TaskModel::CanCollapseRole).toBool());
    QVERIFY(!model.data(model.index(parent, 0), TaskModel::IsCollapsedRole).toBool());
    QVERIFY(model.hasCollapsibleRows());

    model.toggleCollapsed(parent);
    QCoreApplication::processEvents();

    // The sub-task is hidden, but the parent and its siblings stay put.
    QCOMPARE(rowFor(model, QStringLiteral("child")), -1);
    QVERIFY(rowFor(model, QStringLiteral("other")) >= 0);
    const int collapsed = rowFor(model, QStringLiteral("parent"));
    QVERIFY(collapsed >= 0);
    QVERIFY(model.data(model.index(collapsed, 0), TaskModel::IsCollapsedRole).toBool());
    // Still one of one sub-task: collapsing hides rows, it does not drop them.
    QCOMPARE(model.data(model.index(collapsed, 0), TaskModel::SubtaskCountRole).toInt(), 1);

    // Todoist stores the state per task, so it is pushed like any other edit.
    QVERIFY(Repository::instance()->item(QStringLiteral("parent")).isCollapsed);
    const QVector<PendingCommand> commands = CommandQueue::instance()->take();
    QCOMPARE(commands.size(), 1);
    QCOMPARE(commands.at(0).type, QStringLiteral("item_update"));
    QCOMPARE(commands.at(0).args.value(QStringLiteral("is_collapsed")).toBool(), true);

    model.toggleCollapsed(collapsed);
    QCoreApplication::processEvents();

    QVERIFY(rowFor(model, QStringLiteral("child")) >= 0);
    QVERIFY(!Repository::instance()->item(QStringLiteral("parent")).isCollapsed);
}

void TaskModelTest::subtaskCountsIncludeHiddenCompletedChildren()
{
    load(QJsonArray{
        task(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("first"), QStringLiteral("parent"), 0),
        completedTask(QStringLiteral("second"), QStringLiteral("parent"), 1),
        completedTask(QStringLiteral("third"), QStringLiteral("parent"), 2),
        task(QStringLiteral("lonely"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    const int parent = rowFor(model, QStringLiteral("parent"));
    QVERIFY(parent >= 0);
    // Completed sub-tasks are filtered out of the list, yet the indicator has
    // to say "1 of 3" for the fraction to mean anything.
    QCOMPARE(model.data(model.index(parent, 0), TaskModel::SubtaskCountRole).toInt(), 3);
    QCOMPARE(model.data(model.index(parent, 0), TaskModel::SubtaskCompletedCountRole).toInt(), 2);

    const int lonely = rowFor(model, QStringLiteral("lonely"));
    QVERIFY(lonely >= 0);
    QCOMPARE(model.data(model.index(lonely, 0), TaskModel::SubtaskCountRole).toInt(), 0);
    QVERIFY(!model.data(model.index(lonely, 0), TaskModel::CanCollapseRole).toBool());
}

void TaskModelTest::collapsedSubtreeStillCountsTowardTheNestingLimit()
{
    load(QJsonArray{
        task(QStringLiteral("level0"), {}, 0),
        task(QStringLiteral("level1"), QStringLiteral("level0"), 0),
        task(QStringLiteral("level2"), QStringLiteral("level1"), 0),
        task(QStringLiteral("level3"), QStringLiteral("level2"), 0),
        collapsedTask(QStringLiteral("mover"), {}, 1),
        task(QStringLiteral("passenger"), QStringLiteral("mover"), 0),
    });
    TaskModel model;
    configureProjectModel(model);

    const int mover = rowFor(model, QStringLiteral("mover"));
    const int level3 = rowFor(model, QStringLiteral("level3"));
    QVERIFY(mover >= 0);
    QVERIFY(level3 >= 0);
    QCOMPARE(rowFor(model, QStringLiteral("passenger")), -1);

    // The hidden sub-task would land five levels deep. Measuring the source's
    // height from the visible rows alone would have missed it.
    QVERIFY(!model.canDrop(mover, level3, level3, true));

    const int level2 = rowFor(model, QStringLiteral("level2"));
    QVERIFY(level2 >= 0);
    QVERIFY(model.canDrop(mover, level2, level2, true));
}

void TaskModelTest::droppingIntoACollapsedTaskRevealsIt()
{
    load(QJsonArray{
        collapsedTask(QStringLiteral("parent"), {}, 0),
        task(QStringLiteral("child"), QStringLiteral("parent"), 0),
        task(QStringLiteral("mover"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    const int parent = rowFor(model, QStringLiteral("parent"));
    const int mover = rowFor(model, QStringLiteral("mover"));
    QVERIFY(parent >= 0);
    QVERIFY(mover >= 0);

    model.commitDrop(mover, parent, parent, true);
    QCoreApplication::processEvents();

    QCOMPARE(Repository::instance()->item(QStringLiteral("mover")).parentId, QStringLiteral("parent"));
    // A task dropped into a collapsed parent would otherwise disappear.
    QVERIFY(!Repository::instance()->item(QStringLiteral("parent")).isCollapsed);
    QVERIFY(rowFor(model, QStringLiteral("mover")) >= 0);

    // The hidden sibling keeps its place: renumbering from the visible rows
    // alone would have given both tasks child_order 0.
    QCOMPARE(Repository::instance()->item(QStringLiteral("child")).childOrder, 0);
    QCOMPARE(Repository::instance()->item(QStringLiteral("mover")).childOrder, 1);
}

void TaskModelTest::collapsingInAReadOnlyProjectStaysLocal()
{
    Repository::instance()->applySyncPayload(QJsonObject{
        {QStringLiteral("projects"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("project")},
             {QStringLiteral("name"), QStringLiteral("Shared")},
             {QStringLiteral("role"), QStringLiteral("READ_ONLY")},
         }}},
        {QStringLiteral("items"),
         QJsonArray{
             task(QStringLiteral("parent"), {}, 0),
             task(QStringLiteral("child"), QStringLiteral("parent"), 0),
         }},
    });
    TaskModel model;
    configureProjectModel(model);

    const int parent = rowFor(model, QStringLiteral("parent"));
    QVERIFY(parent >= 0);

    model.toggleCollapsed(parent);
    QCoreApplication::processEvents();

    // Folding still works, but a collaborator who cannot write to the task
    // must not queue a command the server would only reject.
    QCOMPARE(rowFor(model, QStringLiteral("child")), -1);
    QVERIFY(Repository::instance()->item(QStringLiteral("parent")).isCollapsed);
    QVERIFY(CommandQueue::instance()->take().isEmpty());
}

// The rest of this file guards the rebuild path that replaced an
// unconditional beginResetModel()/endResetModel() on every sync: a full
// reset tore down and recreated every delegate in the view, which is what
// showed up as rerendering jank whenever a sync landed. Rebuilds should now
// emit the narrowest signal that describes what actually changed, and only
// fall back to a reset when rows that persist across the change have also
// been reordered.

void TaskModelTest::noOpSyncEmitsNothing()
{
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
        task(QStringLiteral("beta"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);

    // Re-applying the same payload is what a sync that found nothing new
    // looks like: the repository still emits itemsChanged.
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
        task(QStringLiteral("beta"), {}, 1),
    });
    QCoreApplication::processEvents();

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(dataChangedSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
}

void TaskModelTest::editingATaskUpdatesInPlaceWithoutReset()
{
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
        task(QStringLiteral("beta"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);
    const int alpha = rowFor(model, QStringLiteral("alpha"));
    QVERIFY(alpha >= 0);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);

    Repository::instance()->setItemContent(QStringLiteral("alpha"), QStringLiteral("Renamed"), {});
    QCoreApplication::processEvents();

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(dataChangedSpy.count() >= 1);
    // The row stayed put; only its content changed.
    QCOMPARE(rowFor(model, QStringLiteral("alpha")), alpha);
    QCOMPARE(model.data(model.index(alpha, 0), TaskModel::ContentRole).toString(), QStringLiteral("Renamed"));
}

void TaskModelTest::addingATaskInsertsWithoutReset()
{
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
    });
    TaskModel model;
    configureProjectModel(model);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);

    Repository::instance()->addItem(QStringLiteral("New task"), QStringLiteral("project"), {}, {}, {}, 1, {}, {}, {});
    QCoreApplication::processEvents();

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(insertedSpy.count() >= 1);
    QVERIFY(rowFor(model, QStringLiteral("alpha")) >= 0);
    QCOMPARE(model.taskCount(), 2);
}

void TaskModelTest::completingATaskRemovesItsRowWithoutReset()
{
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
        task(QStringLiteral("beta"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);
    QVERIFY(!model.showCompleted());

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);

    Repository::instance()->completeItem(QStringLiteral("alpha"));
    QCoreApplication::processEvents();

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(removedSpy.count() >= 1);
    QCOMPARE(rowFor(model, QStringLiteral("alpha")), -1);
    QVERIFY(rowFor(model, QStringLiteral("beta")) >= 0);
}

void TaskModelTest::reorderingFallsBackToAResetButEndsUpCorrect()
{
    load(QJsonArray{
        task(QStringLiteral("alpha"), {}, 0),
        task(QStringLiteral("beta"), {}, 1),
    });
    TaskModel model;
    configureProjectModel(model);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    // Swap child_order so the two rows trade places: the surviving rows are
    // the same set, but no longer in the same relative order.
    Repository::instance()->reorderItems({{QStringLiteral("alpha"), 1}, {QStringLiteral("beta"), 0}});
    QCoreApplication::processEvents();

    QVERIFY(resetSpy.count() >= 1);
    QCOMPARE(rowFor(model, QStringLiteral("beta")), 0);
    QCOMPARE(rowFor(model, QStringLiteral("alpha")), 1);
}

QTEST_GUILESS_MAIN(TaskModelTest)

#include "taskmodeltest.moc"
