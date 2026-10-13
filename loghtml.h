#ifndef LOGHTML_H
#define LOGHTML_H

#include "constants.h"

#include <QLatin1String>
#include <QString>
#include <QStringBuilder>

// Compact helpers for assembling the small HTML fragments used by the raw log
// drawer and the detail panel. Every fragment in the codebase used to inline
// the same `"<span style=\"color:" + LogColor::X + ";\">"` template, which
// drifted in subtle ways (single vs double quotes, `<br>` placement, missing
// `</span>`). Centralising the templates here means one place defines the
// shape and every call site reads as the intent: "info line", "detail entry",
// not "string concatenation glyph soup".
//
// These helpers do NOT escape their input. Callers must pass `.toHtmlEscaped()`
// on any text that originated from a file path, CLI message, or user input.
// Static literals are safe to pass through as-is.
namespace LogHtml {

// Inline coloured <span>. For tagging a fragment within a larger sentence.
inline QString span(const char *color, const QString &innerHtml)
{
    return QStringLiteral("<span style=\"color:") % QLatin1String(color)
         % QStringLiteral(";\">") % innerHtml % QStringLiteral("</span>");
}

// Coloured <p> with a trailing <br> — the canonical "one log line" shape used
// by `appendDebugHtml`.
inline QString logLine(const char *color, const QString &innerHtml)
{
    return QStringLiteral("<p>") % span(color, innerHtml) % QStringLiteral("</p><br>");
}

// Coloured <p> without the trailing break — used by the detail panel where
// paragraph margins do the spacing instead of a literal <br>.
inline QString detailLine(const char *color, const QString &innerHtml)
{
    return QStringLiteral("<p style='color:") % QLatin1String(color)
         % QStringLiteral(";'>") % innerHtml % QStringLiteral("</p>");
}

// Coloured <li> for detail-panel lists (repairs, fix descriptions).
inline QString listItem(const char *color, const QString &innerHtml)
{
    return QStringLiteral("<li style=\"color:") % QLatin1String(color)
         % QStringLiteral(";\">") % innerHtml % QStringLiteral("</li>");
}

// Coloured <pre> for verbatim stderr / stack-trace blocks.
inline QString preBlock(const char *color, const QString &innerHtml)
{
    return QStringLiteral("<pre style='color:") % QLatin1String(color)
         % QStringLiteral(";'>") % innerHtml % QStringLiteral("</pre>");
}

} // namespace LogHtml

#endif // LOGHTML_H
