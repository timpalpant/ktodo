/*
 * Tests for the evaluator for Todoist's saved-filter language.
 *
 * Filters are written by users and stored server-side, so the parser sees
 * arbitrary text. The contract is that an unparseable or unsupported filter
 * evaluates to false — showing an empty list — rather than throwing or
 * matching everything.
 *
 * Every query here is evaluated with a null Repository. FilterQuery handles
 * that explicitly: terms that need to resolve a project, section, collaborator
 * or parent task return false instead of dereferencing it. That keeps these
 * tests free of SQLite and of any account state, and it also pins the
 * behaviour the app relies on before the first sync completes.
 */

#include "query/filterquery.h"

#include <QTest>

using namespace Todoist;

namespace {

Item task(const QString &content = QStringLiteral("a task"))
{
    Item i;
    i.id = QStringLiteral("1");
    i.content = content;
    return i;
}

Item dueOn(const QDate &date)
{
    Item i = task();
    i.due.date = date.toString(Qt::ISODate);
    return i;
}

/// Evaluates @p query against @p item with no repository available.
bool matches(const QString &query, const Item &item)
{
    return FilterQuery(query, nullptr).matches(item);
}

} // namespace

class FilterQueryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void priorityUsesFilterNumbering_data();
    void priorityUsesFilterNumbering();

    void labels_data();
    void labels();

    void dateTerms_data();
    void dateTerms();

    void nextNDays();
    void dueOnKeyedTerms();

    void booleanAnd();
    void booleanOr();
    void booleanComma();
    void negation();
    void parentheses();
    void precedenceAndBindsTighterThanOr();

    void textSearchMatchesContentAndDescription();
    void textSearchIsCaseInsensitive();

    void termsNeedingARepositoryAreFalse_data();
    void termsNeedingARepositoryAreFalse();

    void assigneeTermsWithoutARepository();
    void recurring();
    void unparseableQueriesMatchNothing_data();
    void unparseableQueriesMatchNothing();
    void whitespaceAndCaseAreTolerated_data();
    void whitespaceAndCaseAreTolerated();
};

void FilterQueryTest::priorityUsesFilterNumbering_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<int>("apiPriority");
    QTest::addColumn<bool>("expected");

    // The filter language calls the most urgent level p1; the API stores that
    // as 4. Getting this inversion wrong would silently show the wrong tasks.
    QTest::newRow("p1 matches api 4") << "p1" << 4 << true;
    QTest::newRow("p1 rejects api 1") << "p1" << 1 << false;
    QTest::newRow("p2 matches api 3") << "p2" << 3 << true;
    QTest::newRow("p3 matches api 2") << "p3" << 2 << true;
    QTest::newRow("p4 matches api 1") << "p4" << 1 << true;
    QTest::newRow("p4 rejects api 4") << "p4" << 4 << false;
    QTest::newRow("priority 1 spelled out") << "priority 1" << 4 << true;
    QTest::newRow("uppercase") << "P1" << 4 << true;
}

void FilterQueryTest::priorityUsesFilterNumbering()
{
    QFETCH(QString, query);
    QFETCH(int, apiPriority);
    QFETCH(bool, expected);

    Item i = task();
    i.priority = apiPriority;
    QCOMPARE(matches(query, i), expected);
}

void FilterQueryTest::labels_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QStringList>("labels");
    QTest::addColumn<bool>("expected");

    QTest::newRow("match") << "@home" << QStringList{"home"} << true;
    QTest::newRow("one of several") << "@home" << QStringList{"work", "home"} << true;
    QTest::newRow("no match") << "@home" << QStringList{"work"} << false;
    QTest::newRow("none at all") << "@home" << QStringList{} << false;
    QTest::newRow("case insensitive") << "@Home" << QStringList{"home"} << true;
    QTest::newRow("no labels, has none") << "no labels" << QStringList{} << true;
    QTest::newRow("no labels, has one") << "no labels" << QStringList{"home"} << false;
    QTest::newRow("no label singular") << "no label" << QStringList{} << true;
}

void FilterQueryTest::labels()
{
    QFETCH(QString, query);
    QFETCH(QStringList, labels);
    QFETCH(bool, expected);

    Item i = task();
    i.labels = labels;
    QCOMPARE(matches(query, i), expected);
}

void FilterQueryTest::dateTerms_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<int>("dueDaysFromToday"); // 999 means "no due date"
    QTest::addColumn<bool>("expected");

    // "today" in the filter language means due today *or earlier*, which is
    // what the Today view shows.
    QTest::newRow("today, due today") << "today" << 0 << true;
    QTest::newRow("today, overdue") << "today" << -1 << true;
    QTest::newRow("today, tomorrow") << "today" << 1 << false;
    QTest::newRow("today, undated") << "today" << 999 << false;

    QTest::newRow("overdue, yesterday") << "overdue" << -1 << true;
    QTest::newRow("overdue, today") << "overdue" << 0 << false;
    QTest::newRow("od alias") << "od" << -1 << true;

    QTest::newRow("no date, undated") << "no date" << 999 << true;
    QTest::newRow("no date, dated") << "no date" << 0 << false;
    QTest::newRow("nodate alias") << "nodate" << 999 << true;

    QTest::newRow("tomorrow, tomorrow") << "tomorrow" << 1 << true;
    QTest::newRow("tomorrow, today") << "tomorrow" << 0 << false;
}

void FilterQueryTest::dateTerms()
{
    QFETCH(QString, query);
    QFETCH(int, dueDaysFromToday);
    QFETCH(bool, expected);

    const Item i = dueDaysFromToday == 999 ? task() : dueOn(QDate::currentDate().addDays(dueDaysFromToday));
    QCOMPARE(matches(query, i), expected);
}

void FilterQueryTest::nextNDays()
{
    QVERIFY(matches(QStringLiteral("7 days"), dueOn(QDate::currentDate().addDays(3))));
    QVERIFY(matches(QStringLiteral("next 7 days"), dueOn(QDate::currentDate().addDays(7))));
    QVERIFY(!matches(QStringLiteral("7 days"), dueOn(QDate::currentDate().addDays(8))));
    // The window is open-ended in the past, matching how Todoist shows it.
    QVERIFY(matches(QStringLiteral("7 days"), dueOn(QDate::currentDate().addDays(-3))));
    QVERIFY(!matches(QStringLiteral("7 days"), task()));
}

void FilterQueryTest::dueOnKeyedTerms()
{
    const QDate today = QDate::currentDate();

    QVERIFY(matches(QStringLiteral("due before: tomorrow"), dueOn(today)));
    QVERIFY(!matches(QStringLiteral("due before: today"), dueOn(today)));

    QVERIFY(matches(QStringLiteral("due after: today"), dueOn(today.addDays(1))));
    QVERIFY(!matches(QStringLiteral("due after: today"), dueOn(today)));

    QVERIFY(matches(QStringLiteral("due: today"), dueOn(today)));
    QVERIFY(!matches(QStringLiteral("due: today"), dueOn(today.addDays(1))));

    // An explicit ISO date is the unambiguous form.
    QVERIFY(matches(QStringLiteral("due: 2026-08-02"), dueOn(QDate(2026, 8, 2))));

    // A date word that cannot be parsed must not match everything.
    QVERIFY(!matches(QStringLiteral("due before: qwertyuiop"), dueOn(today)));
}

void FilterQueryTest::booleanAnd()
{
    Item i = task();
    i.priority = 4;
    i.labels = QStringList{"home"};

    QVERIFY(matches(QStringLiteral("p1 & @home"), i));
    QVERIFY(!matches(QStringLiteral("p1 & @work"), i));
    QVERIFY(!matches(QStringLiteral("p2 & @home"), i));
}

void FilterQueryTest::booleanOr()
{
    Item i = task();
    i.labels = QStringList{"home"};

    QVERIFY(matches(QStringLiteral("@home | @work"), i));
    QVERIFY(!matches(QStringLiteral("@office | @work"), i));
}

void FilterQueryTest::booleanComma()
{
    // A comma is Todoist's other spelling of "or".
    Item i = task();
    i.labels = QStringList{"home"};

    QVERIFY(matches(QStringLiteral("@home, @work"), i));
    QVERIFY(!matches(QStringLiteral("@office, @work"), i));
}

void FilterQueryTest::negation()
{
    Item i = task();
    i.labels = QStringList{"home"};

    QVERIFY(matches(QStringLiteral("!@work"), i));
    QVERIFY(!matches(QStringLiteral("!@home"), i));
    QVERIFY(matches(QStringLiteral("!!@home"), i));
}

void FilterQueryTest::parentheses()
{
    Item i = task();
    i.priority = 4;
    i.labels = QStringList{"home"};

    QVERIFY(matches(QStringLiteral("(p1 | p2) & @home"), i));
    QVERIFY(!matches(QStringLiteral("(p2 | p3) & @home"), i));
    QVERIFY(matches(QStringLiteral("!(p2 & @home)"), i));
}

void FilterQueryTest::precedenceAndBindsTighterThanOr()
{
    // "@work & p1 | @home" must read as "(@work & p1) | @home". Parsed the
    // other way, a p1 task with neither label would match.
    Item homeOnly = task();
    homeOnly.labels = QStringList{"home"};
    homeOnly.priority = 1;
    QVERIFY(matches(QStringLiteral("@work & p1 | @home"), homeOnly));

    Item urgentNoLabel = task();
    urgentNoLabel.priority = 4;
    QVERIFY(!matches(QStringLiteral("@work & p1 | @home"), urgentNoLabel));
}

void FilterQueryTest::textSearchMatchesContentAndDescription()
{
    Item i = task(QStringLiteral("Renew passport"));
    i.description = QStringLiteral("booking reference ABC123");

    QVERIFY(matches(QStringLiteral("search: passport"), i));
    QVERIFY(matches(QStringLiteral("search: ABC123"), i));
    QVERIFY(!matches(QStringLiteral("search: visa"), i));

    // A bare word with no sigil is also a text search.
    QVERIFY(matches(QStringLiteral("passport"), i));
    QVERIFY(!matches(QStringLiteral("visa"), i));
}

void FilterQueryTest::textSearchIsCaseInsensitive()
{
    const Item i = task(QStringLiteral("Renew Passport"));
    QVERIFY(matches(QStringLiteral("search: passport"), i));
    QVERIFY(matches(QStringLiteral("PASSPORT"), i));
}

void FilterQueryTest::termsNeedingARepositoryAreFalse_data()
{
    QTest::addColumn<QString>("query");

    // Without a repository these cannot be resolved. Returning false shows an
    // empty list, which is honest; matching everything would not be.
    QTest::newRow("project") << "#Work";
    QTest::newRow("project and children") << "##Work";
    QTest::newRow("section") << "/Backlog";
    QTest::newRow("shared") << "shared";
    QTest::newRow("subtask of") << "subtask of: Plan trip";
}

void FilterQueryTest::termsNeedingARepositoryAreFalse()
{
    QFETCH(QString, query);

    Item i = task();
    i.projectId = QStringLiteral("p1");
    i.sectionId = QStringLiteral("s1");
    i.parentId = QStringLiteral("t0");

    QVERIFY2(!matches(query, i), qPrintable(query));
    // The query is still well-formed; it just cannot be answered.
    QVERIFY2(FilterQuery(query, nullptr).isValid(), qPrintable(query));
}

void FilterQueryTest::assigneeTermsWithoutARepository()
{
    // "assigned" and "no assignee" read the item alone, so they work with no
    // repository; "assigned to:" needs one to resolve a name.
    Item assigned = task();
    assigned.responsibleUid = QStringLiteral("u2");

    QVERIFY(matches(QStringLiteral("assigned"), assigned));
    QVERIFY(!matches(QStringLiteral("no assignee"), assigned));
    QVERIFY(!matches(QStringLiteral("assigned to: me"), assigned));

    const Item unassigned = task();
    QVERIFY(!matches(QStringLiteral("assigned"), unassigned));
    QVERIFY(matches(QStringLiteral("no assignee"), unassigned));
    QVERIFY(matches(QStringLiteral("unassigned"), unassigned));
}

void FilterQueryTest::recurring()
{
    Item i = dueOn(QDate::currentDate());
    i.due.isRecurring = true;
    QVERIFY(matches(QStringLiteral("recurring"), i));
    QVERIFY(!matches(QStringLiteral("recurring"), dueOn(QDate::currentDate())));
}

void FilterQueryTest::unparseableQueriesMatchNothing_data()
{
    QTest::addColumn<QString>("query");

    QTest::newRow("empty") << "";
    QTest::newRow("whitespace") << "   ";
    QTest::newRow("bare operator") << "&";
    QTest::newRow("dangling and") << "p1 &";
    QTest::newRow("empty parens") << "()";
}

void FilterQueryTest::unparseableQueriesMatchNothing()
{
    QFETCH(QString, query);

    // Whatever the parser makes of these, the one thing that must not happen
    // is a filter that matches an unrelated task.
    Item i = task(QStringLiteral("unrelated"));
    i.priority = 2;
    i.labels = QStringList{"misc"};

    QVERIFY2(!matches(query, i), qPrintable(QStringLiteral("query=[%1]").arg(query)));
}

void FilterQueryTest::whitespaceAndCaseAreTolerated_data()
{
    QTest::addColumn<QString>("query");

    // These are all the same filter as far as a user is concerned.
    QTest::newRow("plain") << "p1&@home";
    QTest::newRow("spaced") << "p1 & @home";
    QTest::newRow("padded") << "   p1   &   @home   ";
    QTest::newRow("mixed case") << "P1 & @Home";
}

void FilterQueryTest::whitespaceAndCaseAreTolerated()
{
    QFETCH(QString, query);

    Item i = task();
    i.priority = 4;
    i.labels = QStringList{"home"};

    QVERIFY2(matches(query, i), qPrintable(query));
}

QTEST_MAIN(FilterQueryTest)

#include "filterquerytest.moc"
