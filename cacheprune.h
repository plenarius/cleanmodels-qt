#ifndef CACHEPRUNE_H
#define CACHEPRUNE_H

#include "constants.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// The command line for `cleanmodels cache prune` (cleanmodels 4.17 and
// later), which trims the folder compile --cache-dir keeps: it removes the
// models no compile has used for --unused-for (30d by default) and the files
// nothing refers to any more. It prints one line on success ("removed N of M
// models and K files (S); L models kept") and its errors on stderr.
namespace CachePrune {

// Asked for in whole days; the CLI's own default is 30 days.
constexpr int DefaultDays = 30;
constexpr int MinDays = 1;
constexpr int MaxDays = 3650;

// The --unused-for value: whole days, which the CLI reads as "30d".
inline QString unusedFor(int days)
{
    return QString::number(days) + QLatin1Char('d');
}

inline QStringList args(const QString &cacheDir, int days)
{
    return { QString::fromLatin1(CliCommand::Cache), QString::fromLatin1(CliCommand::Prune),
             QString::fromLatin1(CliFlag::UnusedFor), unusedFor(days), cacheDir };
}

// The arguments that print `cache prune`'s usage, and whether that usage is
// the command's (an older CLI without it prints its general usage instead).
inline QStringList helpArgs()
{
    return { QString::fromLatin1(CliCommand::Cache), QString::fromLatin1(CliCommand::Prune),
             QStringLiteral("--help") };
}

inline bool helpListsPrune(const QByteArray &usage)
{
    return usage.contains("-unused-for");
}

} // namespace CachePrune

#endif
