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
    QString path, title, artist;
    QVector<MidiEvent> events;
    QVector<Lyric> lyrics;
    double duration = 0;
    double bpm = 120;
    int division = 480;
};
Song readSong(const QString &path);
int partFor(int channel, int note);
QString partName(int part);
int drumNote(int part);
constexpr int PartCount = 62, MasterPart = 62, BusFirst = 63, BusCount = 6, TrackCount = 69;
} // namespace bt
