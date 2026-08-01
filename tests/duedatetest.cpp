/*
 * Tests for the due-date helpers.
 *
 * Todoist parses natural language server-side and that stays the source of
 * truth. DueDate::guessLocal exists only so a task created offline sorts into
 * the right list before the first sync corrects it — so what is asserted here
 * is that the guess is right when it commits to one, and that it declines to
 * guess otherwise rather than inventing a date.
 *
 * Everything is relative to QDate::currentDate(), because the code under test
 * reads the clock directly.
 */

#include "util/duedate.h"

#include <QLocale>
#include <QTest>

using namespace Todoist;

namespace {

/// An all-day due, the shape the API returns for a floating date.
Due allDay(const QDate &date)
{
    Due d;
    d.date = date.toString(Qt::ISODate);
    return d;
}

/// A due with a time, i.e. an RFC3339 timestamp.
Due timed(const QDateTime &when)
{
    Due d;
    d.date = when.toString(Qt::ISODate);
    return d;
}

} // namespace

class DueDateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void guessRelativeWords_data();
    void guessRelativeWords();

    void guessInNUnits_data();
    void guessInNUnits();

    void guessWeekdayIsAlwaysInTheFuture_data();
    void guessWeekdayIsAlwaysInTheFuture();

    void guessRecurringIsLeftToTheServer();
    void guessIsoDate();
    void guessDeclinesUnknownText_data();
    void guessDeclinesUnknownText();

    void isToday();
    void isTomorrow();

    void isOverdueAllDay();
    void isOverdueWithTime();
    void isOverdueIgnoresInvalid();

    void formatShortRelativeDays();
    void formatShortMarksRecurring();
    void formatShortFallsBackToTypedText();

    void dayHeadingLabelsTodayAndTomorrow();
};

void DueDateTest::initTestCase()
{
    // formatShort and dayHeading render weekday and month names through
    // QLocale, so the locale has to be pinned or the expectations drift with
    // the machine running the suite.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void DueDateTest::guessRelativeWords_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("daysFromToday");

    QTest::newRow("today") << "today" << 0;
    QTest::newRow("tod") << "tod" << 0;
    QTest::newRow("tomorrow") << "tomorrow" << 1;
    QTest::newRow("tom") << "tom" << 1;
    QTest::newRow("yesterday") << "yesterday" << -1;
    QTest::newRow("next week") << "next week" << 7;
    // Case and surrounding whitespace are what a user actually types.
    QTest::newRow("mixed case") << "ToMoRRoW" << 1;
    QTest::newRow("padded") << "  today  " << 0;
}

void DueDateTest::guessRelativeWords()
{
    QFETCH(QString, text);
    QFETCH(int, daysFromToday);

    const Due due = DueDate::guessLocal(text);
    QVERIFY2(due.isValid(), qPrintable(text));
    QCOMPARE(due.localDate(), QDate::currentDate().addDays(daysFromToday));
    QVERIFY(!due.hasTime());
}

void DueDateTest::guessInNUnits_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("daysFromToday");

    QTest::newRow("in 3 days") << "in 3 days" << 3;
    QTest::newRow("in 1 day") << "in 1 day" << 1;
    QTest::newRow("in 2 weeks") << "in 2 weeks" << 14;
    QTest::newRow("in 0 days") << "in 0 days" << 0;
}

void DueDateTest::guessInNUnits()
{
    QFETCH(QString, text);
    QFETCH(int, daysFromToday);

    const Due due = DueDate::guessLocal(text);
    QVERIFY2(due.isValid(), qPrintable(text));
    QCOMPARE(due.localDate(), QDate::currentDate().addDays(daysFromToday));
}

void DueDateTest::guessWeekdayIsAlwaysInTheFuture_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("weekday"); // Qt numbering: Monday = 1

    QTest::newRow("monday") << "monday" << 1;
    QTest::newRow("fri") << "fri" << 5;
    QTest::newRow("sunday") << "sunday" << 7;
    QTest::newRow("next tuesday") << "next tuesday" << 2;
    QTest::newRow("wed") << "wed" << 3;
}

void DueDateTest::guessWeekdayIsAlwaysInTheFuture()
{
    QFETCH(QString, text);
    QFETCH(int, weekday);

    const Due due = DueDate::guessLocal(text);
    QVERIFY2(due.isValid(), qPrintable(text));

    const QDate date = due.localDate();
    QCOMPARE(date.dayOfWeek(), weekday);

    // "Monday" said on a Monday means next Monday, never today: the resolved
    // day is always 1..7 days ahead.
    const qint64 delta = QDate::currentDate().daysTo(date);
    QVERIFY2(delta >= 1 && delta <= 7, qPrintable(QStringLiteral("delta=%1").arg(delta)));
}

void DueDateTest::guessRecurringIsLeftToTheServer()
{
    // Guessing a recurrence locally would be wrong more often than right, so
    // the flag is set but no date is invented; the UI still shows the text.
    const Due due = DueDate::guessLocal(QStringLiteral("every monday"));
    QVERIFY(due.isRecurring);
    QVERIFY(!due.isValid());
}

void DueDateTest::guessIsoDate()
{
    const Due due = DueDate::guessLocal(QStringLiteral("2026-08-02"));
    QVERIFY(due.isValid());
    QCOMPARE(due.localDate(), QDate(2026, 8, 2));
}

void DueDateTest::guessDeclinesUnknownText_data()
{
    QTest::addColumn<QString>("text");

    // Anything not confidently understood must yield an invalid Due so the
    // task is filed as undated rather than under a wrong date.
    QTest::newRow("empty") << "";
    QTest::newRow("whitespace") << "   ";
    QTest::newRow("prose") << "call the plumber";
    QTest::newRow("two letters") << "mo";
    QTest::newRow("not a date") << "someday";
    QTest::newRow("malformed iso") << "2026-13-45";
}

void DueDateTest::guessDeclinesUnknownText()
{
    QFETCH(QString, text);
    QVERIFY2(!DueDate::guessLocal(text).isValid(), qPrintable(text));
}

void DueDateTest::isToday()
{
    QVERIFY(DueDate::isToday(allDay(QDate::currentDate())));
    QVERIFY(!DueDate::isToday(allDay(QDate::currentDate().addDays(1))));
    QVERIFY(!DueDate::isToday(Due()));
}

void DueDateTest::isTomorrow()
{
    QVERIFY(DueDate::isTomorrow(allDay(QDate::currentDate().addDays(1))));
    QVERIFY(!DueDate::isTomorrow(allDay(QDate::currentDate())));
    QVERIFY(!DueDate::isTomorrow(Due()));
}

void DueDateTest::isOverdueAllDay()
{
    QVERIFY(DueDate::isOverdue(allDay(QDate::currentDate().addDays(-1))));
    // An all-day task due today is not overdue until the day is out.
    QVERIFY(!DueDate::isOverdue(allDay(QDate::currentDate())));
    QVERIFY(!DueDate::isOverdue(allDay(QDate::currentDate().addDays(1))));
}

void DueDateTest::isOverdueWithTime()
{
    // A timed task compares by the instant, so one earlier today is overdue
    // even though the date is not.
    const QDateTime now = QDateTime::currentDateTime();
    QVERIFY(DueDate::isOverdue(timed(now.addSecs(-3600))));
    QVERIFY(!DueDate::isOverdue(timed(now.addSecs(3600))));
}

void DueDateTest::isOverdueIgnoresInvalid()
{
    QVERIFY(!DueDate::isOverdue(Due()));

    // A recurring due with no resolved date must not be reported as overdue.
    Due recurring;
    recurring.string = QStringLiteral("every monday");
    recurring.isRecurring = true;
    QVERIFY(!DueDate::isOverdue(recurring));
}

void DueDateTest::formatShortRelativeDays()
{
    QCOMPARE(DueDate::formatShort(allDay(QDate::currentDate())), QStringLiteral("Today"));
    QCOMPARE(DueDate::formatShort(allDay(QDate::currentDate().addDays(1))), QStringLiteral("Tomorrow"));
    QCOMPARE(DueDate::formatShort(allDay(QDate::currentDate().addDays(-1))), QStringLiteral("Yesterday"));

    // Inside the coming week the weekday name is enough to place a task.
    const QDate inThreeDays = QDate::currentDate().addDays(3);
    QCOMPARE(DueDate::formatShort(allDay(inThreeDays)), QLocale().dayName(inThreeDays.dayOfWeek(), QLocale::ShortFormat));

    // Further out it needs a date, and a different year needs the year too.
    const QDate farOff = QDate::currentDate().addDays(60);
    const QString far = DueDate::formatShort(allDay(farOff));
    QVERIFY2(far.contains(QLocale().monthName(farOff.month(), QLocale::ShortFormat)), qPrintable(far));

    const QDate nextYear = QDate::currentDate().addYears(1);
    QVERIFY(DueDate::formatShort(allDay(nextYear)).contains(QString::number(nextYear.year())));
}

void DueDateTest::formatShortMarksRecurring()
{
    Due due = allDay(QDate::currentDate());
    due.isRecurring = true;
    QVERIFY2(DueDate::formatShort(due).startsWith(QStringLiteral("↻")), qPrintable(DueDate::formatShort(due)));
}

void DueDateTest::formatShortFallsBackToTypedText()
{
    // A recurring due the server has not resolved yet still has to show
    // something, and the text the user typed is the best available label.
    Due due;
    due.string = QStringLiteral("every 2nd Tuesday");
    due.isRecurring = true;
    QCOMPARE(DueDate::formatShort(due), QStringLiteral("every 2nd Tuesday"));
    QCOMPARE(DueDate::formatLong(due), QStringLiteral("every 2nd Tuesday"));
}

void DueDateTest::dayHeadingLabelsTodayAndTomorrow()
{
    QVERIFY(DueDate::dayHeading(QDate::currentDate()).startsWith(QStringLiteral("Today")));
    QVERIFY(DueDate::dayHeading(QDate::currentDate().addDays(1)).startsWith(QStringLiteral("Tomorrow")));

    // Any other day is just the formatted date, with no prefix.
    const QDate other = QDate::currentDate().addDays(5);
    const QString heading = DueDate::dayHeading(other);
    QVERIFY2(!heading.contains(QStringLiteral("Today")), qPrintable(heading));
    QVERIFY2(!heading.contains(QStringLiteral("Tomorrow")), qPrintable(heading));
    QVERIFY2(heading.contains(QLocale().dayName(other.dayOfWeek(), QLocale::ShortFormat)), qPrintable(heading));
}

QTEST_MAIN(DueDateTest)

#include "duedatetest.moc"
