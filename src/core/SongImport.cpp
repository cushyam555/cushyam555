#include "SongImport.h"
#include "SongLibrary.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <stdexcept>
namespace bt {
namespace {
QString normalized(const QString &path) {
    QFileInfo info(path);
    auto key = info.canonicalFilePath();
    if (key.isEmpty()) {
        auto parent = info.dir().absolutePath();
        if (parent != path)
            key = normalized(parent) + "/" + info.fileName();
        else
            key = info.absoluteFilePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(key)).toCaseFolded();
}
} // namespace
bool supportedSong(const QString &path) {
    auto ext = QFileInfo(path).suffix().toLower();
    if (ext == "kar")
        return true;
    if (ext != "mid" && ext != "midi" && ext != "lyr" && ext != "cur")
        return false;
    if (ext == "mid" || ext == "midi") {
        auto files = ncnFiles(path);
        return QFileInfo::exists(path) && !files.lyrics.isEmpty() && !files.cursor.isEmpty();
    }
    try {
        auto song = readSong(path);
        auto files = ncnFiles(song.path);
        return !files.lyrics.isEmpty() && !files.cursor.isEmpty();
    } catch (...) {
        return false;
    }
}
bool songStorageAllowed(const QString &destination, const QString &dataRoot) {
    if (destination.trimmed().isEmpty())
        return false;
    auto dest = normalized(destination), data = normalized(dataRoot);
    return dest != data && !dest.startsWith(data + "/");
}
ImportPlan analyzeImport(const QStringList &paths, const QString &destination, bool copy,
                         const std::function<bool(int, int)> &progress) {
    ImportPlan plan;
    QSet<QString> sources, targets;
    for (int i = 0; i < paths.size(); ++i) {
        if (progress && !progress(i, paths.size())) {
            plan.items.clear();
            plan.errors.clear();
            return plan;
        }
        try {
            if (!supportedSong(paths[i]))
                throw std::runtime_error("รองรับเพลง NCN ที่มีไฟล์คู่กัน หรือ KAR เท่านั้น");
            auto song = readSong(paths[i]);
            auto key = normalized(song.path);
            if (sources.contains(key))
                continue;
            sources.insert(key);
            auto files = ncnFiles(song.path);
            bool ncn = !files.midi.isEmpty() && !files.lyrics.isEmpty() && !files.cursor.isEmpty();
            QString target = song.path;
            if (copy) {
                if (ncn) {
                    auto info = QFileInfo(files.relative);
                    auto base =
                        files.relative.left(files.relative.size() - info.suffix().size() - 1);
                    target = QDir(destination).filePath("Song/" + base + ".mid");
                } else
                    target = QDir(destination).filePath("KAR/" + QFileInfo(song.path).fileName());
            }
            bool orphan = false;
            if (copy && ncn) {
                auto base = files.relative.left(files.relative.size() -
                                                QFileInfo(files.relative).suffix().size() - 1);
                orphan = QFileInfo::exists(QDir(destination).filePath("Lyrics/" + base + ".lyr")) ||
                         QFileInfo::exists(QDir(destination).filePath("Cursor/" + base + ".cur"));
            }
            bool duplicate =
                orphan || QFileInfo::exists(target) && (copy || normalized(target) != key);
            auto destKey = normalized(target);
            duplicate |= targets.contains(destKey);
            targets.insert(destKey);
            if (duplicate)
                ++plan.duplicates;
            song.events.clear();
            song.lyrics.clear();
            plan.items.push_back({song, target, duplicate, ncn});
        } catch (const std::exception &e) {
            plan.errors << QFileInfo(paths[i]).fileName() + ": " + e.what();
        }
    }
    return plan;
}
} // namespace bt
