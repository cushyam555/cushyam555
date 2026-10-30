#pragma once
#include "SongImport.h"
namespace bt {
struct DatabaseScan {
    ImportPlan plan;
    int discovered = 0;
    bool canceled = false;
};
bool validNcnRoot(const QString &root);
QString databaseSongKey(const ImportItem &item);
DatabaseScan scanDatabase(const QString &ncnRoot, const QString &karRoot,
                          const std::function<bool(int, int, const QString &)> &progress = {});
} // namespace bt
