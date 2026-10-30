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
constexpr int PartCount = 23, MasterPart = 23, TrackCount = 24;
} // namespace bt
