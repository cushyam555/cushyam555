#include <QtTest>
#include <bass.h>
#include <bassmidi.h>
#include <cmath>
#include <QTemporaryDir>
#include "SongDatabase.h"
#include "Midi/MidiFile.h"
#include "Utils.h"
class CoreTest : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString songs;
    QByteArray midi() {
        QByteArray track = QByteArray::fromHex("00ff510307a12000ff050548656c6c6f00903c648360803c0000ff2f00");
        QByteArray bytes = QByteArray::fromHex("4d546864000000060000000101e04d54726b");
        for (int shift=24; shift>=0; shift-=8) bytes.append(char(quint32(track.size()) >> shift));
        return bytes + track;
    }
    void writeFile(const QString &path, const QByteArray &bytes) {
        QFile f(path); QVERIFY(f.open(QFile::WriteOnly)); QCOMPARE(f.write(bytes), qint64(bytes.size()));
    }
private slots:
    void initTestCase() {
        QVERIFY(data.isValid()); qputenv("BT_KARAOKE_DATA_DIR", data.path().toUtf8());
        songs = data.path() + "/songs"; QVERIFY(QDir().mkpath(songs));
        writeFile(songs + "/demo.kar", midi());
    }
    void parseMidiAndLyrics() {
        MidiFile file; QVERIFY(file.read(songs + "/demo.kar"));
        QCOMPARE(file.resorution(), uint32_t(480)); QVERIFY(file.lyrics().contains("Hello"));
        QVERIFY(!file.tempoEvents().isEmpty()); QCOMPARE(file.tempoEvents().first()->bpm(), 120);
        QVERIFY(!file.read(songs + "/missing.mid"));
    }
    void importSearchAndRebuild() {
        SongDatabase db; QVERIFY(db.isOpenned()); db.setKarPath(songs);
        db.start(); QVERIFY(db.wait(10000)); QCOMPARE(db.count(), 1);
        Song *s = db.search("demo"); QVERIFY(s); QCOMPARE(s->songType(), QString("KAR"));
        QCOMPARE(s->path(), QString("/demo.kar"));
        db.start(); QVERIFY(db.wait(10000)); QCOMPARE(db.count(), 1);
        const QString ncn = data.path() + "/NCN";
        for (const QString &dir : {"Song", "Lyrics", "Cursor"}) QVERIFY(QDir().mkpath(ncn + "/" + dir));
        writeFile(ncn + "/Song/001.mid", midi());
        writeFile(ncn + "/Lyrics/001.lyr", "NCN demo\nArtist\nC\n\nHello\n");
        writeFile(ncn + "/Cursor/001.cur", QByteArray::fromHex("00001800"));
        QVERIFY(db.setNcnPath(ncn)); db.start(); QVERIFY(db.wait(10000)); QCOMPARE(db.count(), 2);
        s = db.search("001"); QVERIFY(s); QCOMPARE(s->songType(), QString("NCN"));
        QVERIFY(!db.insertKAR(songs, "bad", songs + "/missing.mid", "missing.mid"));
    }
    void renderMidiWithSoundfont() {
        QVERIFY2(BASS_Init(0, 44100, 0, nullptr, nullptr), "BASS no-sound device failed");
        const QString sf = QFINDTESTDATA("fixtures/test-tone.sf2");
        QVERIFY(!sf.isEmpty());
        HSOUNDFONT font = BASS_MIDI_FontInit(QFile::encodeName(sf).constData(), 0);
        QVERIFY(font != 0);
        const QString path = songs + "/demo.kar";
        HSTREAM stream = BASS_MIDI_StreamCreateFile(FALSE, QFile::encodeName(path).constData(),
            0, 0, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, 44100);
        QVERIFY(stream != 0);
        BASS_MIDI_FONT mapping = {font, -1, 0};
        QVERIFY(BASS_MIDI_StreamSetFonts(stream, &mapping, 1));
        float samples[8192]; bool nonzero = false;
        for (int i=0; i<20; ++i) {
            DWORD bytes = BASS_ChannelGetData(stream, samples, sizeof(samples));
            if (bytes == DWORD(-1)) break;
            for (DWORD j=0; j<bytes / sizeof(float); ++j)
                if (std::fabs(samples[j]) > 0.00001f) nonzero = true;
        }
        BASS_StreamFree(stream); BASS_MIDI_FontFree(font); BASS_Free();
        QVERIFY2(nonzero, "MIDI synthesis produced only silence");
    }
    void playlistRoundTrip() {
        Song song; song.setId("001"); song.setName(QString::fromUtf8("ทดสอบเพลงไทย"));
        song.setArtist("BT"); song.setSongType("NCN"); song.setPath("/Song/001.mid");
        song.setTranspose(2); song.setBpmSpeed(-5);
        QList<Song*> original; original << &song;
        const QString path = data.path() + "/queue.handypl";
        QVERIFY(Utils::savePlaylist(path, original));
        QList<Song*> loaded; QVERIFY(Utils::loadPlaylist(path, loaded));
        QCOMPARE(loaded.size(), 1); QCOMPARE(loaded.first()->name(), song.name());
        QCOMPARE(loaded.first()->transpose(), 2); QCOMPARE(loaded.first()->bpmSpeed(), -5);
        qDeleteAll(loaded); loaded.clear();
        writeFile(path, "invalid playlist"); QVERIFY(!Utils::loadPlaylist(path, loaded));
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
