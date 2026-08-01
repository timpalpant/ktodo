#include "richtext.h"

#include <QRegularExpression>
#include <QUrl>

namespace RichText {

namespace {

/// Markdown link, or a bare http(s)/www URL.
const QRegularExpression &linkPattern()
{
    // The Markdown target allows one level of balanced parentheses, so links
    // to addresses like https://e.com/Foo_(bar) are not truncated.
    static const QRegularExpression re(QStringLiteral(R"(\[([^\]\n]+)\]\(((?:[^()\s]|\([^()\s]*\))+)\)|((?:https?://|www\.)[^\s<>"']+))"));
    return re;
}

/// Drops trailing characters that belong to the sentence, not the address.
QString trimTrailingPunctuation(QString candidate)
{
    while (!candidate.isEmpty()) {
        const QChar last = candidate.back();
        if (QStringLiteral(".,;:!?").contains(last)) {
            candidate.chop(1);
            continue;
        }
        // Only drop a closing bracket with no opener, so URLs containing
        // balanced parentheses survive intact.
        if (last == QLatin1Char(')') && candidate.count(QLatin1Char(')')) > candidate.count(QLatin1Char('('))) {
            candidate.chop(1);
            continue;
        }
        break;
    }
    return candidate;
}

/**
 * Returns a safe absolute URL, or an empty string when the target must not
 * become a clickable link.
 */
QString sanitizeUrl(const QString &candidate)
{
    if (candidate.isEmpty()) {
        return {};
    }

    QString absolute = candidate;
    if (absolute.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
        absolute.prepend(QStringLiteral("https://"));
    }

    const QUrl url(absolute, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative()) {
        return {};
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("mailto")) {
        return {};
    }
    return absolute;
}

QString escape(const QString &text)
{
    QString out = text.toHtmlEscaped();
    // Preserve the line structure the user typed.
    out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return out;
}

QString anchor(const QString &target, const QString &label)
{
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(QString(target).toHtmlEscaped(), escape(label));
}

} // namespace

QString toHtml(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }

    QString out;
    out.reserve(text.size() + 32);

    // Links are located in the raw text and the pieces escaped separately, so
    // an ampersand inside a URL is not double-escaped in the href.
    qsizetype cursor = 0;
    auto it = linkPattern().globalMatch(text);

    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();

        out += escape(text.mid(cursor, match.capturedStart() - cursor));
        cursor = match.capturedEnd();

        if (match.capturedStart(1) >= 0) {
            // [label](target)
            const QString target = sanitizeUrl(match.captured(2));
            out += target.isEmpty() ? escape(match.captured(0)) : anchor(target, match.captured(1));
            continue;
        }

        // Bare URL: any punctuation it shed still belongs in the output.
        const QString raw = match.captured(3);
        const QString trimmed = trimTrailingPunctuation(raw);
        const QString target = sanitizeUrl(trimmed);

        if (target.isEmpty()) {
            out += escape(raw);
            continue;
        }
        out += anchor(target, trimmed);
        out += escape(raw.mid(trimmed.size()));
    }

    out += escape(text.mid(cursor));
    return out;
}

bool hasLinks(const QString &text)
{
    auto it = linkPattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString candidate = match.capturedStart(1) >= 0 ? match.captured(2) : trimTrailingPunctuation(match.captured(3));
        if (!sanitizeUrl(candidate).isEmpty()) {
            return true;
        }
    }
    return false;
}

} // namespace RichText
