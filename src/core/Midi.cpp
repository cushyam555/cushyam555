#include "Midi.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <stdexcept>
#include <tuple>
namespace bt {
namespace {
QString existingPath(QString path) {
    if (QFileInfo::exists(path))
        return path;
    auto info = QFileInfo(path);
    auto parent = info.dir().absolutePath();
    if (parent == path)
        return {};
    auto dir = existingPath(parent);
    if (dir.isEmpty())
        return {};
    for (const auto &name : QDir(dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        if (name.compare(info.fileName(), Qt::CaseInsensitive) == 0)
            return QDir(dir).filePath(name);
    return {};
}
struct Reader {
    QByteArray b;
    int p = 0;
    int end = 0;
    explicit Reader(QByteArray data) : b(std::move(data)), end(b.size()) {}
    int byte() {
        if (p >= end)
            throw std::runtime_error("MIDI truncated");
        return static_cast<unsigned char>(b[p++]);
    }
    int be(int n) {
        int v = 0;
        while (n--)
            v = (v << 8) | byte();
        return v;
    }
    int vlq() {
        int v = 0;
        for (int i = 0; i < 4; ++i) {
            int a = byte();
            v = (v << 7) | (a & 127);
            if (!(a & 128))
                return v;
        }
        throw std::runtime_error("Invalid MIDI variable length");
    }
    QByteArray take(int n) {
        if (n < 0 || n > end - p)
            throw std::runtime_error("MIDI chunk exceeds file");
        auto a = b.mid(p, n);
        p += n;
        return a;
    }
};
QString decode(const QByteArray &b) {
    QString s = QString::fromUtf8(b);
    if (s.toUtf8() == b)
        return s;
    s.clear();
    for (unsigned char c : b) {
        if (c < 128)
            s += QChar(c);
        else if (c >= 0xa1 && c <= 0xfb)
            s += QChar(0x0e01 + c - 0xa1);
        else
            s += QChar(' ');
    }
    return s;
}
struct Raw {
    int tick = 0, order = 0, status = 0, a = 0, b = 0, tempo = 0;
    QString lyric;
};
} // namespace
QString ncnSongFolder(const QString &root) {
    return existingPath(QDir(root).filePath("Song"));
}
NcnFiles ncnFiles(const QString &path) {
    QFileInfo info(path);
    auto full = QDir::fromNativeSeparators(info.absoluteFilePath());
    int pos = full.lastIndexOf("/Song/", -1, Qt::CaseInsensitive);
    if (pos < 0)
        return {};
    QString root = full.left(pos), relative = full.mid(pos + 6);
    QString base = relative.left(relative.size() - info.suffix().size() - 1);
    return {info.absoluteFilePath(), existingPath(root + "/Lyrics/" + base + ".lyr"),
            existingPath(root + "/Cursor/" + base + ".cur"), relative};
}
Song readSong(const QString &input) {
    QString path = input;
    auto info = QFileInfo(path);
    auto ext = info.suffix().toLower();
    if (ext == "lyr" || ext == "cur") {
        auto normalized = QDir::fromNativeSeparators(info.absoluteFilePath());
        QString folder = ext == "lyr" ? "/Lyrics/" : "/Cursor/";
        int pos = normalized.lastIndexOf(folder, -1, Qt::CaseInsensitive);
        QString base = pos >= 0
                           ? normalized.left(pos) + "/Song/" + normalized.mid(pos + folder.size())
                           : normalized;
        base.chop(info.suffix().size() + 1);
        path.clear();
        for (const auto &suffix : {".mid", ".midi", ".kar"}) {
            auto candidate = existingPath(base + suffix);
            if (!candidate.isEmpty()) {
                path = candidate;
                break;
            }
        }
        if (path.isEmpty())
            throw std::runtime_error("NCN needs its matching MIDI in the Song folder");
    }
    QFile f(path);
    if (!f.open(QFile::ReadOnly))
        throw std::runtime_error("Cannot read song");
    if (f.size() > 32 * 1024 * 1024)
        throw std::runtime_error("MIDI file exceeds 32 MB");
    Reader r(f.readAll());
    if (r.take(4) != "MThd" || r.be(4) != 6)
        throw std::runtime_error("Not a standard MIDI file");
    int format = r.be(2), tracks = r.be(2), division = r.be(2);
    if (format > 1 || !tracks || !division || (division & 0x8000))
        throw std::runtime_error("Only MIDI format 0/1 with PPQ is supported");
    QVector<Raw> raw;
    int order = 0;
    for (int track = 0; track < tracks; ++track) {
        if (r.take(4) != "MTrk")
            throw std::runtime_error("Missing MIDI track");
        int size = r.be(4);
        Reader t(r.take(size));
        int tick = 0, running = 0;
        while (t.p < t.end) {
            int delta = t.vlq();
            if (delta > 0x7fffffff - tick)
                throw std::runtime_error("MIDI time overflow");
            tick += delta;
            int s = t.byte();
            if (s < 128) {
                if (!running)
                    throw std::runtime_error("Invalid running status");
                --t.p;
                s = running;
            }
            Raw e;
            e.tick = tick;
            e.order = order++;
            if (s == 0xff) {
                int type = t.byte();
                auto data = t.take(t.vlq());
                if (type == 0x51 && data.size() == 3)
                    e.tempo = (static_cast<unsigned char>(data[0]) << 16) |
                              (static_cast<unsigned char>(data[1]) << 8) |
                              static_cast<unsigned char>(data[2]);
                if (type == 5 || (type == 1 && data.size() && !data.startsWith('@')))
                    e.lyric = decode(data);
                raw.push_back(e);
                running = 0;
                if (type == 0x2f)
                    break;
            } else if (s == 0xf0 || s == 0xf7) {
                t.take(t.vlq());
                running = 0;
            } else if (s >= 0x80 && s <= 0xef) {
                running = s;
                e.status = s;
                e.a = t.byte();
                if ((s & 0xf0) != 0xc0 && (s & 0xf0) != 0xd0)
                    e.b = t.byte();
                if (e.a > 127 || e.b > 127)
                    throw std::runtime_error("Invalid MIDI data");
                raw.push_back(e);
            } else
                throw std::runtime_error("Unsupported MIDI status");
            if (raw.size() > 2000000)
                throw std::runtime_error("Too many MIDI events");
        }
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Raw &a, const Raw &b) {
        return std::tie(a.tick, a.order) < std::tie(b.tick, b.order);
    });
    Song song;
    song.id = QFileInfo(path).completeBaseName();
    song.path = QFileInfo(path).absoluteFilePath();
    song.title = QFileInfo(path).completeBaseName();
    song.division = division;
    int last = 0;
    double seconds = 0, micro = 500000;
    bool firstTempo = true;
    for (const auto &e : raw) {
        seconds += (e.tick - last) * micro / 1000000.0 / division;
        last = e.tick;
        if (e.tempo > 0) {
            micro = e.tempo;
            if (firstTempo) {
                song.bpm = 60000000.0 / micro;
                firstTempo = false;
            }
        }
        if (e.status)
            song.events.push_back({seconds, e.tick, e.status, e.a, e.b});
        if (!e.lyric.isEmpty())
            song.lyrics.push_back({seconds, e.lyric});
        song.duration = seconds;
    }
    song.duration += 1;
    // NCN companion files: lyrics use four header lines, CUR stores 1/24-quarter positions.
    QFileInfo songInfo(path);
    QString normalized = QDir::fromNativeSeparators(songInfo.absoluteFilePath());
    int songFolder = normalized.lastIndexOf("/Song/", -1, Qt::CaseInsensitive);
    if (songFolder >= 0) {
        QString root = normalized.left(songFolder);
        QString relative = normalized.mid(songFolder + 6);
        QString base = relative.left(relative.size() - songInfo.suffix().size() - 1);
        QString lyrPath = existingPath(root + "/Lyrics/" + base + ".lyr");
        QString curPath = existingPath(root + "/Cursor/" + base + ".cur");
        QFile l(lyrPath), c(curPath);
        if (l.open(QFile::ReadOnly) && c.open(QFile::ReadOnly)) {
            auto lines = decode(l.readAll()).replace("\r\n", "\n").replace('\r', '\n').split('\n');
            if (lines.size() >= 4) {
                song.title = lines[0].trimmed();
                song.artist = lines[1].trimmed();
                song.key = lines[2].trimmed();
                QString text = lines.mid(4).join("\n");
                auto curs = c.readAll();
                song.lyrics.clear();
                int charIndex = 0;
                auto tickTime = [&](int target) {
                    double time = 0, tempo = 500000;
                    int prev = 0;
                    for (const auto &e : raw) {
                        if (e.tick > target)
                            break;
                        if (e.tempo > 0) {
                            time += (e.tick - prev) * tempo / 1e6 / division;
                            prev = e.tick;
                            tempo = e.tempo;
                        }
                    }
                    return time + (target - prev) * tempo / 1e6 / division;
                };
                for (int i = 0; i + 1 < curs.size() && charIndex < text.size(); i += 2) {
                    int unit = static_cast<unsigned char>(curs[i]) |
                               (static_cast<unsigned char>(curs[i + 1]) << 8);
                    song.lyrics.push_back(
                        {tickTime(unit * division / 24), text.mid(charIndex++, 1)});
                }
            }
        }
    }
    return song;
}
int drumNote(int part) {
    static const int first[] = {36, 38, 42, 46, 45, 49, 39, 60};
    if (part < DrumFirst || part >= PartCount)
        return 60;
    if (part < DrumFirst + 8)
        return first[part - DrumFirst];
    int index = DrumFirst + 8;
    for (int note = 35; note <= 81; ++note) {
        if (std::find(std::begin(first), std::end(first), note) != std::end(first))
            continue;
        if (index++ == part)
            return note;
    }
    return 60;
}
int partFor(int channel, int note) {
    if (channel != 9)
        return channel < 9 ? channel
                           : channel - 1; // legacy channel mapping; engine routes melodic programs
    for (int p = DrumFirst; p < PartCount; ++p)
        if (drumNote(p) == note)
            return p;
    return DrumFirst + 7;
}
QString partName(int p) {
    if (p == MasterPart)
        return "MASTER";
    if (p >= BusFirst)
        return QString("Bus %1").arg(p - BusFirst + 1);
    if (p >= DrumFirst) {
        static const char *names[] = {"Acoustic bass drum",
                                      "Kick",
                                      "Side stick",
                                      "Snare",
                                      "Hand clap",
                                      "Electric snare",
                                      "Low floor tom",
                                      "Closed hi-hat",
                                      "High floor tom",
                                      "Pedal hi-hat",
                                      "Low tom",
                                      "Open hi-hat",
                                      "Low-mid tom",
                                      "High-mid tom",
                                      "Crash 1",
                                      "High tom",
                                      "Ride 1",
                                      "Chinese cymbal",
                                      "Ride bell",
                                      "Tambourine",
                                      "Splash",
                                      "Cowbell",
                                      "Crash 2",
                                      "Vibraslap",
                                      "Ride 2",
                                      "High bongo",
                                      "Low bongo",
                                      "Mute high conga",
                                      "Open high conga",
                                      "Low conga",
                                      "High timbale",
                                      "Low timbale",
                                      "High agogo",
                                      "Low agogo",
                                      "Cabasa",
                                      "Maracas",
                                      "Short whistle",
                                      "Long whistle",
                                      "Short guiro",
                                      "Long guiro",
                                      "Claves",
                                      "High woodblock",
                                      "Low woodblock",
                                      "Mute cuica",
                                      "Open cuica",
                                      "Mute triangle",
                                      "Open triangle"};
        return QString("%1 • %2").arg(names[drumNote(p) - 35]).arg(drumNote(p));
    }
    return QString("%1 • Program %2")
        .arg(QStringList{"Piano", "Chromatic percussion", "Organ", "Guitar", "Bass", "Strings",
                         "Ensemble", "Brass", "Reed", "Pipe", "Synth lead", "Synth pad",
                         "Synth effects", "Ethnic", "Percussive", "Sound effects"}
                 .at(p / 8))
        .arg(p + 1);
}
} // namespace bt
