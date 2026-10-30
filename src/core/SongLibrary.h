#pragma once
#include "Midi.h"
#include <QSqlDatabase>
#include <functional>
namespace bt {
QString applicationDataPath();
// Metadata is persistent; MIDI events and full lyrics are loaded only when playing.
class SongLibrary {
    QString connection;
    QSqlDatabase database;

  public:
    explicit SongLibrary(const QString &file);
    ~SongLibrary();
    SongLibrary(const SongLibrary &) = delete;
    SongLibrary &operator=(const SongLibrary &) = delete;
    void put(const Song &song);
    void beginBatch();
    void endBatch();
    QVector<Song> entries() const;
    void removeMissing();
    void replaceAll(const QVector<Song> &songs,
                    const std::function<bool(int, int, const QString &)> &progress = {});
    static QString copyNcn(const QString &midi, const QString &destinationRoot,
                           bool overwrite = false);
    static QString copyKar(const QString &source, const QString &destination,
                           bool overwrite = false);
};
} // namespace bt
