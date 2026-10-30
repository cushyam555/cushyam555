#include "audio/Engine.h"
#include "core/Midi.h"
#include "core/SongImport.h"
#include "core/SongLibrary.h"
#include "plugins/VstEffect.h"
#include "ui/Appearance.h"
#include "ui/LyricCanvas.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <future>
class TestSignal : public bt::Effect {
    float value;
    bool source;

  public:
    TestSignal(float v, bool generate) : value(v), source(generate) {}
    QString name() const override {
        return "Test signal";
    }
    void process(float *b, int n, const std::vector<bt::MidiEvent> &, double, double) override {
        if (bypass)
            return;
        for (int i = 0; i < 2 * n; ++i)
            b[i] = source ? value : b[i] * value;
    }
    std::vector<bt::Parameter> parameters() const override {
        return {};
    }
    void setParameter(int, double) override {}
    QJsonObject save() const override {
        return {};
    }
};
class MidiRecorder : public TestSignal {
  public:
    std::vector<bt::MidiEvent> received;
    MidiRecorder() : TestSignal(0, false) {}
    void process(float *, int, const std::vector<bt::MidiEvent> &events, double, double) override {
        received.insert(received.end(), events.begin(), events.end());
    }
};
class CoreTests : public QObject {
    Q_OBJECT
    QByteArray midi() {
        QByteArray t =
            QByteArray::fromHex("00ff510307a12000ff050548656c6c6f00903c648360803c0000ff2f00");
        QByteArray b = QByteArray::fromHex("4d546864000000060000000101e04d54726b");
        for (int i = 24; i >= 0; i -= 8)
            b.append(char(t.size() >> i));
        return b + t;
    }
  private slots:
    void midiTimeline() {
        QTemporaryDir d;
        QFile f(d.path() + "/test.mid");
        QVERIFY(f.open(QFile::WriteOnly));
        f.write(midi());
        f.close();
        auto s = bt::readSong(f.fileName());
        QCOMPARE(s.events.size(), 2);
        QCOMPARE(s.lyrics.size(), 1);
        QCOMPARE(s.bpm, 120.);
        QVERIFY(std::abs(s.events[1].time - .5) < .001);
        QCOMPARE(s.lyrics[0].text, QString("Hello"));
    }
    void ncnCompanions() {
        QTemporaryDir d;
        for (const auto &s : {"sOnG/set", "lYrIcS/set", "cUrSoR/set"})
            QVERIFY(QDir().mkpath(d.path() + "/" + s));
        QFile mid(d.path() + "/sOnG/set/001.MID");
        QVERIFY(mid.open(QFile::WriteOnly));
        mid.write(midi());
        mid.close();
        QFile lyr(d.path() + "/lYrIcS/set/001.LyR");
        QVERIFY(lyr.open(QFile::WriteOnly));
        lyr.write("NCN title\nArtist\nC\n\n");
        lyr.write(QByteArray::fromHex("a1a2"));
        lyr.close();
        QFile cur(d.path() + "/cUrSoR/set/001.CuR");
        QVERIFY(cur.open(QFile::WriteOnly));
        cur.write(QByteArray::fromHex("00001800"));
        cur.close();
        auto song = bt::readSong(mid.fileName());
        QCOMPARE(song.title, QString("NCN title"));
        QCOMPARE(bt::readSong(lyr.fileName()).title, song.title);
        QCOMPARE(bt::readSong(cur.fileName()).lyrics.size(), song.lyrics.size());
        QCOMPARE(song.lyrics.size(), 2);
        QCOMPARE(song.lyrics[0].text, QString::fromUtf8("ก"));
        QVERIFY(std::abs(song.lyrics[1].time - .5) < .001);
    }
    void malformedMidi() {
        QTemporaryDir d;
        QFile f(d.path() + "/bad.mid");
        QVERIFY(f.open(QFile::WriteOnly));
        f.write("MThd");
        f.close();
        QVERIFY_EXCEPTION_THROWN(bt::readSong(f.fileName()), std::runtime_error);
    }
    void drumIsolation() {
        QCOMPARE(bt::partFor(9, 36), bt::DrumFirst);
        QCOMPARE(bt::partFor(9, 38), bt::DrumFirst + 1);
        QCOMPARE(bt::partFor(9, 42), bt::DrumFirst + 2);
        QVERIFY(bt::partFor(9, 46) != bt::partFor(9, 42));
        QCOMPARE(bt::partFor(0, 36), 0);
    }
    void midiPortFailureAndCleanup() {
        bt::MidiOutput output;
        QString message;
        QVERIFY(!output.open(0x7fffffff, message));
        QVERIFY(!message.isEmpty());
        output.send({0, 0, 0x90, 60, 100});
        output.panic();
        output.close();
        output.close();
        // Enumeration does not require an installed loopMIDI port.
        auto ports = bt::MidiOutput::ports();
        for (const auto &name : ports)
            QVERIFY(!name.isEmpty());
    }
    void allGmDrumsAreIndependent() {
        QSet<int> parts;
        for (int note = 35; note <= 81; ++note) {
            int part = bt::partFor(9, note);
            QVERIFY(part >= bt::DrumFirst && part < bt::PartCount);
            QCOMPARE(bt::drumNote(part), note);
            QVERIFY(!parts.contains(part));
            parts.insert(part);
            QVERIFY(!bt::partName(part).isEmpty());
        }
        QCOMPARE(parts.size(), 47);
    }
    void busRoutingAndSend() {
        bt::Engine engine;
        for (auto &strip : engine.strips)
            strip.volume = 1;
        engine.strips[bt::MasterPart].fx.clear();
        engine.strips[0].fx.push_back(std::make_shared<TestSignal>(.1f, true));
        engine.strips[1].fx.push_back(std::make_shared<TestSignal>(.05f, true));
        engine.strips[bt::BusFirst].fx.push_back(std::make_shared<TestSignal>(2.f, false));
        QVERIFY(engine.setRoute(0, bt::BusFirst));
        QVERIFY(!engine.setRoute(bt::BusFirst, bt::BusFirst + 1)); // cycles impossible
        QVERIFY(!engine.setRoute(0, 0));
        float b[128];
        engine.render(b, 64);
        QVERIFY(std::abs(b[0] - .25f) < .00001f);
        engine.strips[0].fxSend = .5;
        engine.strips[bt::TrackCount - 1].fx.push_back(std::make_shared<TestSignal>(2.f, false));
        engine.render(b, 64);
        QVERIFY(std::abs(b[0] - .35f) < .00001f);
        engine.strips[bt::BusFirst].mute = true;
        engine.render(b, 64);
        QVERIFY(std::abs(b[0] - .15f) < .00001f);
        engine.strips[bt::TrackCount - 1].solo = true;
        engine.render(b, 64);
        QVERIFY(std::abs(b[0] - .1f) < .00001f); // bus solo excludes direct channels
        engine.setOutputMode(bt::Engine::OutputMode::Midi);
        engine.render(b, 64);
        for (auto v : b)
            QCOMPARE(v, 0.f);
    }
    void melodyMuteAndProjectState() {
        bt::Engine engine;
        engine.loadSoundfont(QFINDTESTDATA("test-tone.sf2"));
        bt::Song song;
        song.duration = 2;
        song.events = {{0, 0, 0x98, 60, 100}, {1, 480, 0x88, 60, 0}};
        engine.setMelody(true);
        engine.load(song);
        engine.play();
        float b[512];
        engine.render(b, 256);
        for (auto v : b)
            QVERIFY(std::abs(v) < .000001f);
        engine.setMelody(false);
        engine.seek(0);
        engine.play();
        engine.render(b, 256);
        bool audible = false;
        for (auto v : b)
            audible |= std::abs(v) > .00001f;
        QVERIFY(audible);
        engine.setMelody(true);
        QVERIFY(engine.setRoute(3, bt::BusFirst + 2));
        engine.strips[3].fxSend = .37;
        engine.strips[bt::BusFirst + 2].fx.push_back(bt::builtin("Delay", 48000));
        auto state = engine.save();
        state["song"] = QString();
        bt::Engine restored;
        QVERIFY(restored.restore(state).isEmpty());
        QVERIFY(restored.melodyMuted());
        QCOMPARE(restored.melodyChannel(), 8);
        QCOMPARE(restored.strips[3].output, bt::BusFirst + 2);
        QCOMPARE(restored.strips[3].fxSend, .37);
        QCOMPARE(restored.strips[bt::BusFirst + 2].fx.size(), size_t(1));
    }
    void legacyProjectMigration() {
        QJsonArray tracks;
        for (int i = 0; i < 24; ++i)
            tracks.append(QJsonObject{{"volume", i == 23 ? .42 : .7}, {"mute", i == 19}});
        bt::Engine engine;
        QVERIFY(!engine.restore(QJsonObject{{"schema", 1}, {"tracks", tracks}}).isEmpty());
        QCOMPARE(engine.strips[bt::MasterPart].volume, .42);
        QVERIFY(engine.strips[bt::partFor(9, 48)].mute);
        QCOMPARE(engine.strips[bt::BusFirst].volume, 1.);
        QCOMPARE(engine.strips[0].output, bt::MasterPart);
    }
    void programRoutingSurvivesProgramChanges() {
        bt::Engine engine;
        auto strings = std::make_shared<MidiRecorder>(), guitar = std::make_shared<MidiRecorder>(),
             channelTwo = std::make_shared<MidiRecorder>();
        engine.strips[40].fx.push_back(strings);
        engine.strips[24].fx.push_back(guitar);
        engine.strips[2].fx.push_back(channelTwo);
        bt::Song song;
        song.duration = 1;
        song.events = {{0, 0, 0xc2, 40, 0},   {0, 0, 0xc7, 40, 0},   {0, 0, 0x92, 60, 100},
                       {0, 0, 0x97, 64, 100}, {.01, 0, 0xc2, 24, 0}, {.01, 0, 0x92, 60, 100},
                       {.02, 0, 0x82, 60, 0}, {.03, 0, 0x82, 60, 0}, {.04, 0, 0x87, 64, 0}};
        engine.load(song);
        engine.play();
        std::vector<float> pcm(4800);
        engine.render(pcm.data(), 2400);
        auto notes = [](const std::vector<bt::MidiEvent> &events) {
            QVector<int> result;
            for (const auto &e : events)
                if ((e.status & 240) == 0x90 || (e.status & 240) == 0x80)
                    result << ((e.status << 8) | e.data1);
            return result;
        };
        QCOMPARE(notes(strings->received), QVector<int>({(0x92 << 8) | 60, (0x97 << 8) | 64,
                                                         (0x82 << 8) | 60, (0x87 << 8) | 64}));
        QCOMPARE(notes(guitar->received), QVector<int>({(0x92 << 8) | 60, (0x82 << 8) | 60}));
        QVERIFY(notes(channelTwo->received).isEmpty());
        engine.seek(0);
        strings->received.clear();
        guitar->received.clear();
        engine.play();
        engine.render(pcm.data(), 2400);
        QCOMPARE(notes(strings->received).size(), 4);
    }
    void dynamicBusesAndSoundfontNames() {
        bt::Engine engine;
        QCOMPARE(engine.trackCount() - bt::BusFirst, 5);
        int last = 0;
        for (int i = 0; i < 7; ++i)
            last = engine.addBus();
        QCOMPARE(engine.trackCount() - bt::BusFirst, 12);
        QVERIFY(engine.setRoute(127, last));
        engine.strips[last].name = "Strings group";
        for (int i = 0; i < 128; ++i)
            engine.strips[last].fx.push_back(bt::builtin("Gain", 48000));
        auto state = engine.save();
        bt::Engine loaded;
        QVERIFY(loaded.restore(state).isEmpty());
        QCOMPARE(loaded.trackCount(), engine.trackCount());
        QCOMPARE(loaded.strips[127].output, last);
        QCOMPARE(loaded.strips[last].fx.size(), size_t(128));
        QCOMPARE(loaded.strips[last].name, QString("Strings group"));
        engine.loadSoundfont(QFINDTESTDATA("test-tone.sf2"));
        QCOMPARE(engine.presetNames().size(), 128);
        QVERIFY(engine.strips[0].name.contains("BT Test"));
        QVERIFY(engine.strips[0].name.startsWith("000"));
        QVERIFY(engine.strips[127].name.startsWith("127"));
        auto old = engine.save();
        old.remove("programBase");
        auto tracks = old["tracks"].toArray();
        auto first = tracks[0].toObject();
        first["name"] = "001 • BT Test";
        tracks[0] = first;
        old["tracks"] = tracks;
        bt::Engine upgraded;
        QVERIFY(upgraded.restore(old).isEmpty());
        QVERIFY(upgraded.strips[0].name.startsWith("000"));
        QVERIFY(engine.strips[127].name.contains("Not present"));
        engine.testNote(127);
        float pcm[512];
        engine.render(pcm, 256);
        // Missing bank-0 programs do not silently play a different SoundFont preset.
        for (auto value : pcm)
            QVERIFY(std::abs(value) < .00001f);
    }
    void managedNcnStorageAndPersistentIndex() {
        QTemporaryDir source, managed;
        for (const auto &dir : {"Song/set", "Lyrics/set", "Cursor/set"})
            QVERIFY(QDir().mkpath(source.path() + "/" + dir));
        auto write = [](const QString &path, const QByteArray &bytes) {
            QFile f(path);
            return f.open(QFile::WriteOnly) && f.write(bytes) == bytes.size();
        };
        auto mid = source.path() + "/Song/set/001.mid";
        QVERIFY(write(mid, midi()));
        QVERIFY(write(source.path() + "/Lyrics/set/001.lyr", "My song\nArtist\nC\n\nAB"));
        QVERIFY(write(source.path() + "/Cursor/set/001.cur", QByteArray::fromHex("00001800")));
        auto stored = bt::SongLibrary::copyNcn(mid, managed.path() + "/NCN");
        QCOMPARE(bt::SongLibrary::copyNcn(mid, managed.path() + "/NCN"), stored);
        QVERIFY(write(source.path() + "/Lyrics/set/001.lyr", "Changed\nArtist\nC\n\nAB"));
        QVERIFY_EXCEPTION_THROWN(bt::SongLibrary::copyNcn(mid, managed.path() + "/NCN"),
                                 std::runtime_error);
        QCOMPARE(bt::readSong(stored).title, QString("My song"));
        auto db = managed.path() + "/library.sqlite";
        {
            bt::SongLibrary library(db);
            auto song = bt::readSong(stored);
            library.put(song);
            library.put(song);
            QCOMPARE(library.entries().size(), 1);
        }
        QVERIFY(QDir(source.path()).removeRecursively());
        {
            bt::SongLibrary library(db);
            auto entries = library.entries();
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries[0].id, QString("001"));
            QCOMPARE(entries[0].title, QString("My song"));
            QCOMPARE(entries[0].key, QString("C"));
            QVERIFY(entries[0].events.isEmpty());
            QCOMPARE(bt::readSong(entries[0].path).lyrics.size(), 2);
            QVERIFY(QFile::remove(stored));
            library.removeMissing();
            QVERIFY(library.entries().isEmpty());
        }
    }
    void lyricStyleAndDarkPalette() {
        bt::LyricsStyle style;
        style.font = QFont("Tahoma", 62, QFont::Bold);
        style.background = QColor("#112233");
        style.sung = QColor("#ffcc00");
        style.text = QColor("#ffffff");
        style.offset = .7;
        style.alignment = 2;
        bt::LyricsStyle loaded;
        loaded.restore(style.save());
        QCOMPARE(loaded.font.pointSize(), 62);
        QCOMPARE(loaded.background, style.background);
        QCOMPARE(loaded.sung, style.sung);
        QCOMPARE(loaded.text, style.text);
        QCOMPARE(loaded.offset, .7);
        QCOMPARE(loaded.alignment, 2);
        bt::applyDarkPalette(*qobject_cast<QApplication *>(QCoreApplication::instance()));
        auto palette = QApplication::palette();
        QVERIFY(palette.color(QPalette::Window).lightnessF() < .2);
        QVERIFY(palette.color(QPalette::Text).lightnessF() > .7);
        QVERIFY(palette.color(QPalette::Base).lightnessF() < .2);
    }
    void smoothLyricsGapAndProfiles() {
        bt::LyricCanvas canvas;
        bt::LyricsStyle style;
        style.lineGap = 72;
        style.performance = 0;
        canvas.setAppearance(style);
        canvas.setSong({{0, "A"}, {1, "B\n"}, {2, "C"}, {3, "D"}}, 4);
        QCOMPARE(canvas.lineCount(), 2);
        QCOMPARE(canvas.lineGap(), 72);
        QVERIFY(std::abs(canvas.progress(0, .25) - .25) < .001);
        QVERIFY(std::abs(canvas.progress(0, .75) - .75) < .001);
        QCOMPARE(canvas.frameInterval(), 33);
        canvas.setSong({{0, "\\AB"}, {2, "\\CD"}, {4, "\\EF"}}, 6);
        QCOMPARE(canvas.lineCount(), 3);
        QVERIFY(std::abs(canvas.progress(0, .5) - .5) <
                .001); // KAR line markers consume no singing time
        QVERIFY(std::abs(canvas.progress(1, 2.5) - .5) < .001);
        style.performance = 2;
        canvas.setAppearance(style);
        QCOMPARE(canvas.frameInterval(), 8);
        bt::LyricsStyle restored;
        restored.restore(style.save());
        QCOMPARE(restored.lineGap, 72);
        QCOMPARE(restored.performance, 2);
        bt::Engine engine;
        engine.setPerformance(0);
        QCOMPARE(engine.voiceLimit(), 64);
        engine.setPerformance(1);
        QCOMPARE(engine.voiceLimit(), 128);
        engine.setPerformance(2);
        QCOMPARE(engine.voiceLimit(), 256);
        canvas.resize(800, 500);
        canvas.setPosition(2.5);
        QPixmap image(canvas.size());
        canvas.render(&image);
        QVERIFY(!image.isNull());
    }
    void importAnalysisAndExplicitOverwrite() {
        QTemporaryDir source, destination;
        auto file = source.path() + "/test.kar";
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            return f.open(QFile::WriteOnly) && f.write(data) == data.size();
        };
        QVERIFY(write(file, midi()));
        auto target = destination.path() + "/songs";
        auto plan = bt::analyzeImport({file, file}, target, true);
        QCOMPARE(plan.items.size(), 1);
        QCOMPARE(plan.duplicates, 0);
        QVERIFY(plan.errors.isEmpty());
        QVERIFY(!QFileInfo::exists(target));
        auto stored = bt::SongLibrary::copyKar(file, plan.items[0].destination);
        QVERIFY(write(file, midi() + "updated"));
        plan = bt::analyzeImport({file}, target, true);
        QCOMPARE(plan.duplicates, 1);
        QVERIFY_EXCEPTION_THROWN(bt::SongLibrary::copyKar(file, stored), std::runtime_error);
        bt::SongLibrary::copyKar(file, stored, true);
        QFile copied(stored);
        QVERIFY(copied.open(QFile::ReadOnly));
        QCOMPARE(copied.readAll(), midi() + "updated");
        auto plain = source.path() + "/standalone.mid";
        QVERIFY(write(plain, midi()));
        QVERIFY(!bt::supportedSong(plain));
        QVERIFY(bt::supportedSong(file));
        auto bad = bt::analyzeImport({plain}, target, true);
        QVERIFY(bad.items.isEmpty());
        QCOMPARE(bad.errors.size(), 1);
        auto canceled = bt::analyzeImport({file}, target, true, [](int, int) { return false; });
        QVERIFY(canceled.items.isEmpty());
        QVERIFY(!bt::songStorageAllowed(destination.path() + "/DATA/Songs",
                                        destination.path() + "/DATA"));
        QVERIFY(
            bt::songStorageAllowed(destination.path() + "/Music", destination.path() + "/DATA"));
    }
    void lockedCh9DoesNotCutOtherChannels() {
        bt::Engine engine;
        engine.loadSoundfont(QFINDTESTDATA("test-tone.sf2"));
        bt::Song song;
        song.duration = 2;
        song.events = {{0, 0, 0x90, 60, 100}, {0, 0, 0x98, 64, 100}};
        engine.load(song);
        engine.play();
        float pcm[512];
        engine.render(pcm, 256);
        engine.setMelody(true);
        QCOMPARE(engine.melodyChannel(), 8);
        engine.render(pcm, 256);
        bool audible = false;
        for (auto value : pcm)
            audible |= std::abs(value) > .00001f;
        QVERIFY(audible); // the non-melody channel continues its held note
        auto state = engine.save();
        state["melodyChannel"] = 3;
        state["song"] = QString();
        bt::Engine restored;
        QVERIFY(restored.restore(state).isEmpty());
        QCOMPARE(restored.melodyChannel(), 8);
    }
    void arbitraryChains() {
        bt::Engine engine;
        for (int i = 0; i < 128; ++i)
            engine.strips[15].fx.push_back(bt::builtin("Gain", 48000));
        QCOMPARE(engine.strips[15].fx.size(), size_t(128));
        QVERIFY(engine.strips[16].fx.empty());
        auto save = engine.save();
        bt::Engine restored;
        QVERIFY(restored.restore(save).isEmpty());
        QCOMPARE(restored.strips[15].fx.size(), size_t(128));
    }
    void fxBypassAndOrder() {
        auto gain = bt::builtin("Gain", 48000);
        auto sat = bt::builtin("Saturation", 48000);
        gain->setParameter(0, .8);
        sat->setParameter(0, .8);
        sat->setParameter(1, 1);
        std::vector<bt::MidiEvent> no;
        float a[2] = {.1f, .1f}, b[2] = {.1f, .1f};
        gain->process(a, 1, no, 120, 0);
        sat->process(a, 1, no, 120, 0);
        sat->process(b, 1, no, 120, 0);
        gain->process(b, 1, no, 120, 0);
        QVERIFY(std::abs(a[0] - b[0]) > .1);
        gain->bypass = true;
        float original[2] = {.1f, .1f};
        gain->process(original, 1, no, 120, 0);
        QCOMPARE(original[0], .1f);
    }
    void reverbTail() {
        auto fx = bt::builtin("Reverb", 48000);
        fx->setParameter(2, 1);
        std::vector<float> b(12000, 0);
        b[0] = b[1] = 1;
        std::vector<bt::MidiEvent> no;
        fx->process(b.data(), 6000, no, 120, 0);
        bool tail = false;
        for (int i = 1000; i < 12000; ++i)
            tail |= std::abs(b[size_t(i)]) > .00001f;
        QVERIFY(tail);
    }
    void finiteOutput() {
        bt::Engine engine;
        std::vector<float> frames(4096);
        engine.render(frames.data(), 2048);
        for (float x : frames)
            QVERIFY(std::isfinite(x) && std::abs(x) <= 1);
    }
    void soundfontAndStems() {
        bt::Engine engine;
        QTemporaryDir data;
        QString thaiPath = data.path() + QString::fromUtf8("/เสียงทดสอบ.sf2");
        QVERIFY(QFile::copy(QFINDTESTDATA("test-tone.sf2"), thaiPath));
        engine.loadSoundfont(thaiPath);
        engine.testNote(0);
        float b[512];
        bool nonzero = false;
        for (int i = 0; i < 8; ++i) {
            engine.render(b, 256);
            for (float v : b)
                nonzero |= std::abs(v) > .00001f;
        }
        QVERIFY(nonzero);
        QVERIFY(engine.peaks[0] > 0);
        QCOMPARE(engine.peaks[15].load(), 0.f);
    }
    void hiHatChoke() {
        bt::Engine engine;
        engine.loadSoundfont(QFINDTESTDATA("test-tone.sf2"));
        engine.strips[bt::partFor(9, 42)].mute = true;
        bt::Song song;
        song.duration = 2;
        song.events = {{0, 0, 0x99, 46, 110}, {.02, 20, 0x99, 42, 110}};
        engine.load(song);
        engine.play();
        float b[1024];
        engine.render(b, 512);
        bool audible = false;
        for (float sample : b)
            audible |= std::abs(sample) > .00001f;
        QVERIFY(audible);
        engine.render(b, 512);
        // The sampler applies a 10 ms de-click release; check after that fade.
        engine.render(b, 512);
        for (int i = 992; i < 1024; ++i)
            QVERIFY(std::abs(b[i]) < .00001f);
    }
    void pauseResume() {
        bt::Engine engine;
        engine.loadSoundfont(QFINDTESTDATA("test-tone.sf2"));
        bt::Song song;
        song.duration = 2;
        song.events = {{0, 0, 0x90, 60, 110}, {1, 480, 0x80, 60, 0}};
        engine.load(song);
        engine.play();
        float b[512];
        engine.render(b, 256);
        engine.pause();
        double paused = engine.position.load();
        engine.render(b, 256);
        for (float sample : b)
            QCOMPARE(sample, 0.f);
        QCOMPARE(engine.position.load(), paused);
        engine.play();
        engine.render(b, 256);
        bool audible = false;
        for (float sample : b)
            audible |= std::abs(sample) > .00001f;
        QVERIFY(audible);
        QVERIFY(engine.position.load() > paused);
    }
    void audioStartupIsAsynchronous() {
        auto engine = std::make_shared<bt::Engine>();
        auto release = std::make_shared<std::promise<void>>();
        auto gate = release->get_future().share();
        auto entered = std::make_shared<std::atomic<bool>>(false);
        engine->startAudioAsync([gate, entered](QString &error) {
            *entered = true;
            gate.wait_for(std::chrono::seconds(2));
            error = "Simulated driver failure";
            return false;
        });
        QTRY_VERIFY(entered->load());
        QCOMPARE(engine->audioState.load(), bt::Engine::AudioState::Starting);
        release->set_value();
        QTRY_COMPARE(engine->audioState.load(), bt::Engine::AudioState::Failed);
        QCOMPARE(engine->audioError(), QString("Simulated driver failure"));
    }
    void closingDuringAudioStartupIsSafe() {
        auto engine = std::make_shared<bt::Engine>();
        std::weak_ptr<bt::Engine> weak = engine;
        auto release = std::make_shared<std::promise<void>>();
        auto gate = release->get_future().share();
        auto entered = std::make_shared<std::atomic<bool>>(false);
        engine->startAudioAsync([gate, entered](QString &) {
            *entered = true;
            gate.wait_for(std::chrono::seconds(2));
            return false;
        });
        QTRY_VERIFY(entered->load());
        engine->stopAudio();
        engine.reset();
        QVERIFY(!weak.expired());
        release->set_value();
        QTRY_VERIFY(weak.expired());
    }
    void realVst() {
        auto path = qEnvironmentVariable("BT_TEST_VST3");
        QVERIFY2(!path.isEmpty(), "BT_TEST_VST3 must identify the integration module");
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        try {
            auto list = bt::scanVst3(path);
            QVERIFY(!list.isEmpty());
            auto info = list.first().toObject();
            auto fx = std::make_shared<bt::VstEffect>(path, info["id"].toString(), 48000);
            float samples[128];
            std::fill_n(samples, 128, .5f);
            std::vector<bt::MidiEvent> no;
            fx->process(samples, 64, no, 120, 0);
            QVERIFY(std::abs(samples[0] - .25f) < .01f);
            fx->setParameter(0, .25);
            std::fill_n(samples, 128, .5f);
            fx->process(samples, 64, no, 120, 0);
            QVERIFY(std::abs(samples[0] - .125f) < .01f);
            fx->bypass = true;
            std::fill_n(samples, 128, .5f);
            fx->process(samples, 64, no, 120, 0);
            QVERIFY(std::abs(samples[0] - .5f) < .001f);
            fx->bypass = false;
            auto state = fx->save();
            auto restored = std::make_shared<bt::VstEffect>(path, info["id"].toString(), 48000);
            restored->restore(state);
            std::fill_n(samples, 128, .5f);
            restored->process(samples, 64, no, 120, 0);
            QVERIFY(std::abs(samples[0] - .125f) < .01f);
        } catch (const std::exception &e) {
            QFAIL(e.what());
        }
    }
};
QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
