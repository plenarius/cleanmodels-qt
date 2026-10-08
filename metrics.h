#ifndef METRICS_H
#define METRICS_H

#include "constants.h"

#include <QFontMetrics>
#include <QtMath>

// Convert the em-multiplier design tokens declared in `Layout` into pixel
// values bound to a specific widget's font metrics. Calling sites pass their
// own `fontMetrics()` so the chrome scales with whatever font Qt has handed
// the widget — including system-level accessibility bumps and HiDPI scaling
// that happens after `constants.h` was compiled.
//
// Vertical sizing keys off `fontMetrics().height()` (the widget's line
// height) so a row-of-text-and-icon stays proportional. Horizontal sizing
// keys off `horizontalAdvance("0")` so "9 chars wide" really is roughly 9
// digits in whatever font the widget is using — including monospace
// log fonts vs. proportional UI fonts.
namespace Layout {

inline int emH(const QFontMetrics &fm, double em)
{
    return qRound(fm.height() * em);
}

inline int emW(const QFontMetrics &fm, double chars)
{
    return qRound(fm.horizontalAdvance(QLatin1Char('0')) * chars);
}

} // namespace Layout

#endif // METRICS_H
