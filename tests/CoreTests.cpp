#include "audio/Engine.h"
#include "core/Midi.h"
#include "plugins/VstEffect.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
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
        for (const auto &s : {"Song/set", "Lyrics/set", "Cursor/set"})
            QVERIFY(QDir().mkpath(d.path() + "/" + s));
        QFile mid(d.path() + "/Song/set/001.mid");
        QVERIFY(mid.open(QFile::WriteOnly));
        mid.write(midi());
        mid.close();
        QFile lyr(d.path() + "/Lyrics/set/001.lyr");
        QVERIFY(lyr.open(QFile::WriteOnly));
        lyr.write("NCN title\nArtist\nC\n\n");
        lyr.write(QByteArray::fromHex("a1a2"));
        lyr.close();
        QFile cur(d.path() + "/Cursor/set/001.cur");
        QVERIFY(cur.open(QFile::WriteOnly));
        cur.write(QByteArray::fromHex("00001800"));
        cur.close();
        auto song = bt::readSong(mid.fileName());
        QCOMPARE(song.title, QString("NCN title"));
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
    void realVst() {
        auto path = qEnvironmentVariable("BT_TEST_VST3");
        if (path.isEmpty())
            QSKIP("Set BT_TEST_VST3 to the built integration plugin");
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
        auto state = fx->save();
        auto restored = std::make_shared<bt::VstEffect>(path, info["id"].toString(), 48000);
        restored->restore(state);
        std::fill_n(samples, 128, .5f);
        restored->process(samples, 64, no, 120, 0);
        QVERIFY(std::abs(samples[0] - .125f) < .01f);
    }
};
QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
