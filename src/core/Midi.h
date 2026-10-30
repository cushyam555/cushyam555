#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
namespace bt {
struct MidiEvent {
    double time = 0;
    int tick = 0;
    int status = 0, data1 = 0, data2 = 0;
};
struct Lyric {
    double time = 0;
    QString text;
};
struct Song {
    QString path, title, artist, id, key;
    QVector<MidiEvent> events;
    QVector<Lyric> lyrics;
    double duration = 0;
    double bpm = 120;
    int division = 480;
};
struct NcnFiles {
    QString midi, lyrics, cursor, relative;
};
NcnFiles ncnFiles(const QString &midiPath);
QString ncnSongFolder(const QString &root);
Song readSong(const QString &path);
int partFor(int channel, int note);
QString partName(int part);
int drumNote(int part);
constexpr int ProgramCount = 128, DrumFirst = 128, PartCount = 175, MasterPart = 175,
              BusFirst = 176, BusCount = 5, TrackCount = 181;
} // namespace bt
