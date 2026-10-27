#pragma once

#include <QDir>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

// A scratch folder in the first of `parents` where one can be made (each is
// created first if missing). `failures` collects "<parent>: <reason>" lines for
// the ones that failed. Returns nullptr, with every failure listed, if none work.
inline QTemporaryDir *makeScratchDir(const QStringList &parents, QString *failures = nullptr)
{
    for (const QString &parent : parents) {
        if (parent.isEmpty())
            continue;
        QDir().mkpath(parent);
        auto *dir = new QTemporaryDir(QDir(parent).filePath(QStringLiteral("cleanmodels-qt-XXXXXX")));
        if (dir->isValid())
            return dir;
        if (failures)
            *failures += QStringLiteral("\n%1: %2").arg(QDir::toNativeSeparators(parent), dir->errorString());
        delete dir;
    }
    return nullptr;
}
