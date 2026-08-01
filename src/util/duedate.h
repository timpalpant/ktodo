#pragma once

#include "data/types.h"

#include <QDate>
#include <QString>

/**
 * Due-date helpers.
 *
 * Todoist parses natural language server-side and that remains the source of
 * truth. The local guess here exists only so a task created offline sorts
 * into the right list before the first sync corrects it.
 */
namespace DueDate {

/// Best-effort local interpretation of common English date phrases.
Todoist::Due guessLocal(const QString &text);

/// Short label for a task row, e.g. "Today", "Sat", "12 Aug", "Tomorrow 09:00".
QString formatShort(const Todoist::Due &due);

/// Longer label used in the task editor.
QString formatLong(const Todoist::Due &due);

/// True when the due date has passed (all-day dates compare by date only).
bool isOverdue(const Todoist::Due &due);
bool isToday(const Todoist::Due &due);
bool isTomorrow(const Todoist::Due &due);

/// Heading for a day group in Upcoming, e.g. "Today · Fri 1 Aug".
QString dayHeading(const QDate &date);

} // namespace DueDate
