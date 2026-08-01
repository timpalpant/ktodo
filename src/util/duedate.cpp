#include "duedate.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

using namespace Todoist;

namespace DueDate {

namespace {

/// Maps an English weekday name or prefix to Qt's 1..7 numbering.
int weekdayFromWord(const QString &word)
{
    static const QStringList days = {QStringLiteral("monday"),
                                     QStringLiteral("tuesday"),
                                     QStringLiteral("wednesday"),
                                     QStringLiteral("thursday"),
                                     QStringLiteral("friday"),
                                     QStringLiteral("saturday"),
                                     QStringLiteral("sunday")};
    for (int i = 0; i < days.size(); ++i) {
        if (days.at(i).startsWith(word) && word.size() >= 3) {
            return i + 1;
        }
    }
    return 0;
}

Due allDay(const QDate &date, bool recurring = false)
{
    Due d;
    d.date = date.toString(Qt::ISODate);
    d.isRecurring = recurring;
    return d;
}

} // namespace

Due guessLocal(const QString &text)
{
    const QString s = text.trimmed().toLower();
    if (s.isEmpty()) {
        return {};
    }

    const QDate today = QDate::currentDate();

    // Anything recurring is left for the server; guessing would be wrong more
    // often than right, and the placeholder still shows the typed text.
    if (s.startsWith(QLatin1String("every"))) {
        Due d;
        d.isRecurring = true;
        return d;
    }

    if (s == QLatin1String("today") || s == QLatin1String("tod")) {
        return allDay(today);
    }
    if (s == QLatin1String("tomorrow") || s == QLatin1String("tom")) {
        return allDay(today.addDays(1));
    }
    if (s == QLatin1String("yesterday")) {
        return allDay(today.addDays(-1));
    }
    if (s == QLatin1String("next week")) {
        return allDay(today.addDays(7));
    }
    if (s == QLatin1String("next month")) {
        return allDay(today.addMonths(1));
    }

    // "in 3 days" / "in 2 weeks"
    static const QRegularExpression inN(QStringLiteral("^in\\s+(\\d+)\\s+(day|days|week|weeks|month|months)$"));
    if (const auto m = inN.match(s); m.hasMatch()) {
        const int n = m.captured(1).toInt();
        const QString unit = m.captured(2);
        if (unit.startsWith(QLatin1String("day"))) {
            return allDay(today.addDays(n));
        }
        if (unit.startsWith(QLatin1String("week"))) {
            return allDay(today.addDays(7 * n));
        }
        return allDay(today.addMonths(n));
    }

    // A bare or "next"-prefixed weekday resolves to the next such day.
    QString dayWord = s;
    if (dayWord.startsWith(QLatin1String("next "))) {
        dayWord = dayWord.mid(5);
    }
    if (const int target = weekdayFromWord(dayWord); target > 0) {
        int delta = target - today.dayOfWeek();
        if (delta <= 0) {
            delta += 7;
        }
        return allDay(today.addDays(delta));
    }

    // Explicit ISO date, optionally with a time.
    static const QRegularExpression iso(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})$"));
    if (const auto m = iso.match(s); m.hasMatch()) {
        return allDay(QDate::fromString(m.captured(1), Qt::ISODate));
    }

    return {};
}

bool isToday(const Due &due)
{
    return due.isValid() && due.localDate() == QDate::currentDate();
}

bool isTomorrow(const Due &due)
{
    return due.isValid() && due.localDate() == QDate::currentDate().addDays(1);
}

bool isOverdue(const Due &due)
{
    if (!due.isValid()) {
        return false;
    }
    if (due.hasTime()) {
        return due.localDateTime() < QDateTime::currentDateTime();
    }
    return due.localDate() < QDate::currentDate();
}

QString formatShort(const Due &due)
{
    if (!due.isValid()) {
        // A recurring due with no resolved date still has its typed text.
        return due.string;
    }

    const QDate date = due.localDate();
    const QDate today = QDate::currentDate();
    const QLocale locale;

    QString day;
    if (date == today) {
        day = i18n("Today");
    } else if (date == today.addDays(1)) {
        day = i18n("Tomorrow");
    } else if (date == today.addDays(-1)) {
        day = i18n("Yesterday");
    } else if (date > today && date < today.addDays(7)) {
        day = locale.dayName(date.dayOfWeek(), QLocale::ShortFormat);
    } else if (date.year() == today.year()) {
        day = locale.toString(date, QStringLiteral("d MMM"));
    } else {
        day = locale.toString(date, QStringLiteral("d MMM yyyy"));
    }

    if (due.hasTime()) {
        day += QLatin1Char(' ') + locale.toString(due.localDateTime().time(), QLocale::ShortFormat);
    }
    if (due.isRecurring) {
        // A repeat marker matches how the mobile apps flag recurring tasks.
        day = QStringLiteral("↻ ") + day;
    }
    return day;
}

QString formatLong(const Due &due)
{
    if (!due.isValid()) {
        return due.string;
    }
    if (!due.string.isEmpty() && due.isRecurring) {
        return due.string;
    }
    const QLocale locale;
    QString out = locale.toString(due.localDate(), QLocale::LongFormat);
    if (due.hasTime()) {
        out += QLatin1Char(' ') + locale.toString(due.localDateTime().time(), QLocale::ShortFormat);
    }
    return out;
}

QString dayHeading(const QDate &date)
{
    const QDate today = QDate::currentDate();
    const QLocale locale;
    const QString pretty = locale.toString(date, QStringLiteral("ddd d MMM"));

    if (date == today) {
        return i18nc("@title:group day heading", "Today · %1", pretty);
    }
    if (date == today.addDays(1)) {
        return i18nc("@title:group day heading", "Tomorrow · %1", pretty);
    }
    return pretty;
}

} // namespace DueDate
