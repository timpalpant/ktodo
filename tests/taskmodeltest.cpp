/*
 * Tests for the task hierarchy drop contract.
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

QTEST_GUILESS_MAIN(TaskModelTest)

#include "taskmodeltest.moc"
