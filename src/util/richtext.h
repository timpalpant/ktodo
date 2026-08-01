#pragma once

#include <QString>

/**
 * Converts Todoist's task text into the small HTML subset Qt's rich text
 * engine renders.
 *
 * Descriptions routinely contain Markdown links such as
 * `[QCAD - Download](https://qcad.org/en/download)` as well as bare URLs, and
 * both should be clickable.
 */
namespace RichText {

/**
 * Escapes @p text and turns Markdown links and bare URLs into anchors.
 *
 * Only http, https and mailto targets become links. Anything else (notably
 * `javascript:` and `file:`) is rendered as literal text, so a task synced
 * from another device cannot smuggle an executable or local-file link into
 * the UI.
 */
QString toHtml(const QString &text);

/// True when @p text contains something toHtml() would turn into a link.
bool hasLinks(const QString &text);

} // namespace RichText
