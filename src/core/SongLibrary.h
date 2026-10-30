#pragma once
#include "Midi.h"
#include <QSqlDatabase>
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
    static QString copyNcn(const QString &midi, const QString &destinationRoot);
};
} // namespace bt
