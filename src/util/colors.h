#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

/**
 * Todoist identifies colours by name ("berry_red", "charcoal", ...).
 * These map onto the palette the web and mobile clients use so a project
 * keeps the same colour across devices.
 */
namespace Colors {

QColor fromTodoistName(const QString &name);

/// Colour for an API priority (1 = none .. 4 = urgent).
QColor priorityColor(int apiPriority);

/// Names offered in the project/label colour picker, in palette order.
QStringList paletteNames();

} // namespace Colors
