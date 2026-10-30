#include "MainWindow.h"

#include <QApplication>
#include <QSplashScreen>
#include <QDirIterator>
#include <QMetaType>
#include <QStyleFactory>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <QTemporaryDir>
#include <QDebug>
#include <cstdio>
#include <exception>
#include <QPainter>
#include <QLinearGradient>

#include "BASSFX/VSTFX.h"
#include "version.h"
#include "Config.h"
#include "Utils.h"

#ifdef __linux__
#include <QFontDatabase>
#endif

void registerMetaType();

void checkDatabase(QSplashScreen *splash, SongDatabase *db);
void loadSoundfonts(QSplashScreen *splash, MidiSynthesizer *synth);

#ifndef __linux__
void loadVSTi(QSplashScreen *splash, MidiSynthesizer *synth);
void makeVSTList(QSplashScreen *splash, MidiSynthesizer *synth);
#endif

int runApplication(int argc, char *argv[])
{
    QApplication a(argc, argv); 

    QCoreApplication::setApplicationName(VER_PRODUCTNAME_STR);
    QCoreApplication::setApplicationVersion(VER_FILEVERSION_STR);
    QCoreApplication::setOrganizationName(VER_COMPANYNAME_STR);
    QCoreApplication::setOrganizationDomain(VER_COMPANYDOMAIN_STR);

    a.setStyle(QStyleFactory::create("Fusion"));
    QFile theme(":/themes/bt-dark.qss");
    if (theme.open(QFile::ReadOnly))
        a.setStyleSheet(QString::fromUtf8(theme.readAll()));

    const bool smokeTest = a.arguments().contains("--smoke-test");
    QTemporaryDir smokeData;
    if (smokeTest)
        qputenv("BT_KARAOKE_DATA_DIR", smokeData.path().toUtf8());
    // Relative song and style paths must work from any desktop shortcut.
    QDir::setCurrent(QCoreApplication::applicationDirPath());

    registerMetaType();

    QPixmap *pixmap = new QPixmap(640, 300);
    {
        QPainter painter(pixmap);
        QLinearGradient gradient(0, 0, 640, 300);
        gradient.setColorAt(0, QColor("#121826"));
        gradient.setColorAt(1, QColor("#5142b8"));
        painter.fillRect(pixmap->rect(), gradient);
        painter.setPen(Qt::white);
        painter.setFont(QFont("Segoe UI", 38, QFont::Bold));
        painter.drawText(QRect(40, 60, 560, 90), Qt::AlignCenter, "BT Karaoke");
        painter.setFont(QFont("Tahoma", 15));
        painter.drawText(QRect(40, 155, 560, 60), Qt::AlignCenter,
            QString::fromUtf8("ร้องเพลงโปรด ในสไตล์ของคุณ"));
    }
    QSplashScreen *splash = new QSplashScreen(*pixmap);
    splash->show();
    qApp->processEvents();

    splash->showMessage("กำลังเริ่มโปรแกรม...", Qt::AlignBottom|Qt::AlignRight);
    qApp->processEvents();

    { // Config Dir
        QDir dir(TEMP_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(TEMP_DIR_PATH);

        dir.setPath(ALL_DATA_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(ALL_DATA_DIR_PATH);

        dir.setPath(CONFIG_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(CONFIG_DIR_PATH);
    }

    { // set style
        QSettings *s = new QSettings("Style.ini", QSettings::IniFormat);

        #ifdef __linux__
        bool useFusion = s->value("Fusion", true).toBool();
        #else
        bool useFusion = s->value("Fusion", false).toBool();
        #endif

        delete s;

        if (useFusion)
            a.setStyle(QStyleFactory::create("Fusion"));
    }


    // Add font for linux
    #ifdef __linux__
    QFontDatabase::addApplicationFont(":/Fonts/THSarabunNew/THSarabunNew Bold.ttf");
    #endif
    //----------------------------------

    if (smokeTest) { std::fprintf(stderr, "BT startup: constructing main window\n"); std::fflush(stderr); }
    MainWindow w;
    if (smokeTest) { std::fprintf(stderr, "BT startup: main window constructed\n"); std::fflush(stderr); }
    if (!w.database()->isOpenned()) {
        if (!smokeTest)
            QMessageBox::critical(&w, "BT Karaoke",
                "ไม่สามารถเปิดฐานข้อมูลเพลงได้ โปรดตรวจสอบสิทธิ์เขียนโฟลเดอร์ข้อมูลผู้ใช้");
        return 1;
    }
    w.setWindowIcon(QIcon(":/Icons/App/icon.png"));

    checkDatabase(splash, w.database());
    loadSoundfonts(splash, w.midiPlayer()->midiSynthesizer());

    #ifndef __linux__
    loadVSTi(splash, w.midiPlayer()->midiSynthesizer());
    makeVSTList(splash, w.midiPlayer()->midiSynthesizer());
    w.synthMixerDialog()->setVSTVendorMenu();
    #endif
    w.synthMixerDialog()->setFXToSynth();

    if (smokeTest) { std::fprintf(stderr, "BT startup: resources loaded\n"); std::fflush(stderr); }
    w.show();

    splash->finish(&w);

    delete splash;
    delete pixmap;

    if (smokeTest) {
        QTimer::singleShot(500, &a, [&a, &w]() {
            const bool ready = w.isVisible() && w.database()->isOpenned();
            qInfo() << "BT_KARAOKE_UI_SMOKE" << (ready ? "PASS" : "FAIL");
            a.exit(ready ? 0 : 1);
        });
    } else {
        QSettings firstRun(CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        if (!firstRun.value("WelcomeShown", false).toBool()) {
            QTimer::singleShot(0, &w, [&w]() {
                QMessageBox::information(&w, "ยินดีต้อนรับสู่ BT Karaoke",
                    "เริ่มใช้งานโดยกด ตั้งค่า / เพิ่มเพลง บนแถบด้านบน\n\n"
                    "1. เลือกโฟลเดอร์เพลง NCN หรือ KAR/MIDI และปรับปรุงฐานข้อมูล\n"
                    "2. เลือกไฟล์ SoundFont (.sf2) ในการตั้งค่าเสียง\n"
                    "3. พิมพ์ชื่อเพลงเพื่อค้นหา แล้วกด Enter เพื่อเพิ่มคิว\n\n"
                    "โปรแกรมไม่รวมเพลงหรือ SoundFont และยังไม่รองรับ HNK");
                QSettings saved(CONFIG_APP_FILE_PATH, QSettings::IniFormat);
                saved.setValue("WelcomeShown", true);
            });
        }
    }

    return a.exec();
}

void registerMetaType()
{
    qRegisterMetaType<InstrumentType>("InstrumentType");

    //<QList<int>>("QList<int>");
    qRegisterMetaTypeStreamOperators<QList<int>>("QList<int>");

    //qRegisterMetaType<QList<uint>>("QList<uint>");
    qRegisterMetaTypeStreamOperators<QList<uint>>("QList<uint>");

    //qRegisterMetaType<QList<float>>("QList<float>");
    qRegisterMetaTypeStreamOperators<QList<float>>("QList<float>");

    //qRegisterMetaType<QList<bool>>("QList<bool>");
    qRegisterMetaTypeStreamOperators<QList<bool>>("QList<bool>");

    //qRegisterMetaType<QList<QList<float>>>("QList<QList<float>>");
    qRegisterMetaTypeStreamOperators<QList<QList<float>>>("QList<QList<float>>");

    qRegisterMetaTypeStreamOperators<QList<QByteArray>>("QList<QByteArray>>");
}

void checkDatabase(QSplashScreen *splash, SongDatabase *db)
{
    if (db->isNewVersion())
        return;

    splash->showMessage("กำลังปรับปรุงฐานข้อมูลเพลง", Qt::AlignBottom|Qt::AlignRight);
    qApp->processEvents();

    db->updateToNewVersion();
}

void loadSoundfonts(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings settings(CONFIG_APP_FILE_PATH, QSettings::IniFormat);

    synth->setLoadAllSoundfont(settings.value("SynthSoundfontsLoadAll", false).toBool());

    // set soundfont to synth
    QStringList sfList = settings.value("SynthSoundfonts", QStringList()).toStringList();
    settings.beginReadArray("SynthSoundfontsVolume");
    for (int i=0; i<sfList.count(); i++)
    {
        settings.setArrayIndex(i);
        int volume = settings.value("SoundfontVolume", 100).toInt();

        splash->showMessage("กำลังโหลด : " + QFileInfo(sfList.at(i)).fileName(), Qt::AlignBottom|Qt::AlignRight);
        qApp->processEvents();

        if (synth->addSoundfont(sfList.at(i)))
            synth->setSoundfontVolume(i, volume / 100.0f);
    }
    settings.endArray();


    // set Map soundfont
    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        QList<int> sfMap     = settings.value(sfKey).value<QList<int>>();
        QList<int> sfDrumMap = settings.value(drKey).value<QList<int>>();

        if (sfMap.count() == 0)
            sfMap = synth->getMapSoundfontIndex(i);
        if (sfDrumMap.count() == 0)
            sfDrumMap = synth->getDrumMapSfIndex(i);

        synth->setMapSoundfontIndex(i, sfMap, sfDrumMap);
    }
}

#ifndef __linux__

void loadVSTi(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings st(CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);

    st.beginReadArray("VSTiGroup");

    for (int i=0; i<4; i++)
    {
        st.setArrayIndex(i);

        QString      filePath   = st.value("VstiFilePath", "").toString();
        int          program    = st.value("VstiPrograms", 0).toInt();
        QList<float> params     = st.value("VstiParams").value<QList<float>>();
        QByteArray   chunk      = st.value("VstiChunk", QByteArray()).toByteArray();

        if (filePath == "")
            continue;

        splash->showMessage("กำลังโหลด : " + QFileInfo(filePath).fileName(), Qt::AlignBottom|Qt::AlignRight);
        qApp->processEvents();

        DWORD vsti = synth->setVSTiFile(i, filePath);

        if (vsti != 0)
        {
            if (chunk.length() > 0)
                BASS_VST_SetChunk(vsti, false, chunk.constData(), chunk.length());

            BASS_VST_SetProgram(vsti, program);
            FX::setVSTParams(vsti, params);
        }
    }

    st.endArray();
}

void makeVSTList(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings st(CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    QStringList dirs = st.value("VSTDirs", QStringList()).toStringList();

    QStringList vstDirs;
    vstDirs << QDir::currentPath() + "/VST";
    vstDirs += dirs;

    QMap<uint, VSTNamePath> vstList;

    for (const QString &dir : vstDirs)
    {
        QDirIterator it(dir, QStringList() << "*.DLL" << "*.dll",
                        QDir::Files|QDir::NoSymLinks, QDirIterator::Subdirectories);

        while(it.hasNext()) {

            it.next();

            splash->showMessage("กำลังตรวจสอบ : " + it.fileName(), Qt::AlignBottom|Qt::AlignRight);
            qApp->processEvents();

            VSTNamePath info;

            if (!Utils::vstInfo(it.filePath(), &info))
                continue;

            vstList[info.uniqueID] = info;
        }
    }

    synth->setVSTList(vstList);
}

#endif

int main(int argc, char *argv[])
{
    try {
        return runApplication(argc, argv);
    } catch (const RtMidiError &error) {
        std::fprintf(stderr, "BT Karaoke MIDI initialization failed: %s\n", error.getMessage().c_str());
    } catch (const std::exception &error) {
        std::fprintf(stderr, "BT Karaoke initialization failed: %s\n", error.what());
    }
    return 1;
}
