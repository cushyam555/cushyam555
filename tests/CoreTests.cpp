#include "audio/Engine.h"
#include "core/Midi.h"
#include "plugins/VstEffect.h"
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
        QCOMPARE(bt::readSong(lyr.fileName()).path, mid.fileName());
        QCOMPARE(bt::readSong(cur.fileName()).path, mid.fileName());
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
        QCOMPARE(bt::partFor(9, 36), 15);
        QCOMPARE(bt::partFor(9, 38), 16);
        QCOMPARE(bt::partFor(9, 42), 17);
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
            QVERIFY(part >= 15 && part < bt::PartCount);
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
        song.events = {{0, 0, 0x93, 60, 100}, {1, 480, 0x83, 60, 0}};
        engine.setMelody(true, 3);
        engine.load(song);
        engine.play();
        float b[512];
        engine.render(b, 256);
        for (auto v : b)
            QVERIFY(std::abs(v) < .000001f);
        engine.setMelody(false, 3);
        engine.seek(0);
        engine.play();
        engine.render(b, 256);
        bool audible = false;
        for (auto v : b)
            audible |= std::abs(v) > .00001f;
        QVERIFY(audible);
        engine.setMelody(true, 3);
        QVERIFY(engine.setRoute(3, bt::BusFirst + 2));
        engine.strips[3].fxSend = .37;
        engine.strips[bt::BusFirst + 2].fx.push_back(bt::builtin("Delay", 48000));
        auto state = engine.save();
        state["song"] = QString();
        bt::Engine restored;
        QVERIFY(restored.restore(state).isEmpty());
        QVERIFY(restored.melodyMuted());
        QCOMPARE(restored.melodyChannel(), 3);
        QCOMPARE(restored.strips[3].output, bt::BusFirst + 2);
        QCOMPARE(restored.strips[3].fxSend, .37);
        QCOMPARE(restored.strips[bt::BusFirst + 2].fx.size(), size_t(1));
    }
    void legacyProjectMigration() {
        QJsonArray tracks;
        for (int i = 0; i < 24; ++i)
            tracks.append(QJsonObject{{"volume", i == 23 ? .42 : .7}, {"mute", i == 19}});
        bt::Engine engine;
        QVERIFY(engine.restore(QJsonObject{{"schema", 1}, {"tracks", tracks}}).isEmpty());
        QCOMPARE(engine.strips[bt::MasterPart].volume, .42);
        QVERIFY(engine.strips[bt::partFor(9, 48)].mute);
        QCOMPARE(engine.strips[bt::BusFirst].volume, 1.);
        QCOMPARE(engine.strips[0].output, bt::MasterPart);
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
        engine.strips[17].mute = true;
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
