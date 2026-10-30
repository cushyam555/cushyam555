#include "DatabaseScan.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <stdexcept>
namespace bt {
bool validNcnRoot(const QString &root) {
    if (root.isEmpty() || !QDir(root).exists())
        return false;
    QSet<QString> children;
    for (const auto &entry : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        children.insert(entry.toCaseFolded());
    return children.contains("song") && children.contains("lyrics") && children.contains("cursor");
}
QString databaseSongKey(const ImportItem &item) {
    return (item.ncn ? QString("NCN:") : QString("KAR:")) + item.song.id.trimmed().toCaseFolded();
}
DatabaseScan scanDatabase(const QString &ncnRoot, const QString &karRoot,
                          const std::function<bool(int, int, const QString &)> &progress) {
    if (ncnRoot.isEmpty() && karRoot.isEmpty())
        throw std::runtime_error("เลือกที่เก็บเพลง NCN หรือ KAR อย่างน้อยหนึ่งตำแหน่ง");
    if (!ncnRoot.isEmpty() && !validNcnRoot(ncnRoot))
        throw std::runtime_error("ที่เก็บ NCN ต้องมีโฟลเดอร์ Song, Lyrics และ Cursor");
    if (!karRoot.isEmpty() && !QDir(karRoot).exists())
        throw std::runtime_error("ไม่พบโฟลเดอร์ KAR");
    DatabaseScan result;
    QVector<QPair<QString, bool>> candidates;
    auto notify = [&](int done, int total, const QString &name) {
        if (progress && !progress(done, total, name)) {
            result.canceled = true;
            result.plan.items.clear();
            return false;
        }
        return true;
    };
    auto collect = [&](const QString &root, bool ncn) {
        if (root.isEmpty())
            return true;
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        int inspected = 0;
        while (it.hasNext()) {
            auto path = it.next();
            auto ext = QFileInfo(path).suffix().toLower();
            if ((ncn && (ext == "mid" || ext == "midi")) || (!ncn && ext == "kar"))
                candidates.push_back({path, ncn});
            if (++inspected % 100 == 0 && !notify(0, 0, path))
                return false;
        }
        return true;
    };
    if (!collect(ncnRoot.isEmpty() ? QString() : ncnSongFolder(ncnRoot), true) ||
        !collect(karRoot, false))
        return result;
    result.discovered = candidates.size();
    QSet<QString> paths, identifiers;
    for (int i = 0; i < candidates.size(); ++i) {
        const auto &candidate = candidates[i];
        if (!notify(i, candidates.size(), candidate.first))
            return result;
        try {
            auto song = readSong(candidate.first);
            if (candidate.second) {
                auto files = ncnFiles(song.path);
                if (files.lyrics.isEmpty() || files.cursor.isEmpty())
                    throw std::runtime_error("ไม่พบ Lyrics/Cursor คู่กัน");
            }
            auto path = QFileInfo(song.path).canonicalFilePath();
#ifdef _WIN32
            path = path.toCaseFolded();
#endif
            if (paths.contains(path))
                continue;
            paths.insert(path);
            QVector<MidiEvent>().swap(song.events);
            QVector<Lyric>().swap(song.lyrics);
            ImportItem item{song, song.path, false, candidate.second};
            auto key = databaseSongKey(item);
            item.duplicate = identifiers.contains(key);
            if (item.duplicate)
                ++result.plan.duplicates;
            identifiers.insert(key);
            result.plan.items.push_back(std::move(item));
        } catch (const std::exception &e) {
            result.plan.errors << QFileInfo(candidate.first).fileName() + ": " + e.what();
        }
    }
    notify(candidates.size(), candidates.size(), QString());
    return result;
}
} // namespace bt
