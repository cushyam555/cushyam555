#pragma once
#include "Midi.h"
#include <QStringList>
#include <functional>
namespace bt {
struct ImportItem {
    Song song;
    QString destination;
    bool duplicate = false, ncn = false;
};
struct ImportPlan {
    QVector<ImportItem> items;
    QStringList errors;
    int duplicates = 0;
};
bool supportedSong(const QString &path);
bool songStorageAllowed(const QString &destination, const QString &dataRoot);
ImportPlan analyzeImport(const QStringList &paths, const QString &destination, bool copy,
                         const std::function<bool(int, int)> &progress = {});
} // namespace bt
