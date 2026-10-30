#include "MainWindow.h"
#include "plugins/VstEffect.h"
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include <thread>
namespace bt {
QString theme() {
    return R"(
* { font-family:'Segoe UI','Noto Sans Thai','Tahoma'; font-size:13px; color:#e8ecf6; }
QMainWindow,QDialog {background:#0c101a;} QWidget#mixerSurface {background:#0c101a;} QWidget#panel {background:#131a28;border:1px solid #252e42;border-radius:14px;}
QLabel#brand {font-size:26px;font-weight:800;} QLabel#heading {font-size:22px;font-weight:700;} QLabel#muted {color:#93a2bc;}
QPushButton {background:#202a3d;border:1px solid #34425e;border-radius:8px;padding:9px 14px;} QPushButton:hover {background:#344466;} QPushButton:checked {background:#4c45a5;border-color:#a096ff;} QPushButton#primary {background:#7264ef;border:0;font-weight:700;} QPushButton:disabled {color:#6b768c;background:#171e2b;}
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox {background:#151d2c;border:1px solid #34425e;border-radius:8px;padding:8px;}
QListWidget {background:#101725;border:1px solid #26324a;border-radius:10px;outline:0;padding:5px;} QListWidget::item {padding:12px;border-bottom:1px solid #1e2a3e;} QListWidget::item:selected {background:#34315e;border-radius:6px;}
QScrollArea {border:0;background:transparent;} QScrollBar:horizontal {height:10px;background:#111827;} QScrollBar:vertical {width:10px;background:#111827;} QScrollBar::handle {background:#465576;border-radius:4px;min-height:25px;min-width:25px;}
QSlider::groove:horizontal {height:5px;background:#27344c;border-radius:2px;} QSlider::handle:horizontal {background:#9c91ff;width:14px;margin:-5px 0;border-radius:7px;} QSlider::groove:vertical {width:6px;background:#27344c;} QSlider::handle:vertical {height:18px;background:#a498ff;margin:0 -5px;border-radius:4px;}
QProgressBar {border:0;background:#0b111d;border-radius:3px;color:transparent;} QProgressBar::chunk {background:#52d7b1;border-radius:3px;}
QComboBox QAbstractItemView,QAbstractItemView,QTextEdit,QPlainTextEdit {background:#101725;color:#e8ecf6;selection-background-color:#51499c;selection-color:white;}
QHeaderView::section,QTabBar::tab {background:#202a3d;color:#e8ecf6;padding:8px;} QTabBar::tab:selected {background:#51499c;}
QGroupBox {border:1px solid #34425e;border-radius:8px;margin-top:15px;padding-top:15px;color:#e8ecf6;}
QDialogButtonBox {background:#0c101a;} QCheckBox,QRadioButton {color:#e8ecf6;}
QMenu {background:#1a2234;border:1px solid #394767;} QMenu::item {padding:9px 22px;} QMenu::item:selected {background:#51499c;} QToolTip {background:#26314b;color:white;border:1px solid #7364ef;}
)";
}
namespace {
QPushButton *button(const QString &text, QBoxLayout *layout, std::function<void()> fn,
                    QObject *context) {
    auto *b = new QPushButton(text);
    layout->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, context, std::move(fn));
    return b;
}
QString timeText(double seconds) {
    int n = std::max(0, int(seconds));
    return QString("%1:%2").arg(n / 60, 2, 10, QChar('0')).arg(n % 60, 2, 10, QChar('0'));
}
QString songKey(const QString &path) {
    auto key = QFileInfo(path).canonicalFilePath();
#ifdef _WIN32
    return key.toCaseFolded();
#else
    return key;
#endif
}
QString registry() {
    auto dir = applicationDataPath();
    QDir().mkpath(dir);
    return dir + "/plugins.json";
}
void error(QWidget *w, const std::exception &e) {
    QMessageBox::warning(w, "BT Karaoke", QString::fromUtf8(e.what()));
}
struct LibraryJob {
    std::atomic<bool> cancel{false}, done{false};
    std::atomic<int> position{0}, total{0};
    std::mutex mutex;
    QString file, error;
    DatabaseScan scan;
    bool success = false;
    bool progress(int value, int maximum, const QString &path) {
        position = value;
        total = maximum;
        {
            std::lock_guard<std::mutex> lock(mutex);
            file = path;
        }
        return !cancel;
    }
};
bool libraryTask(QWidget *parent, QProgressBar *bar, QLabel *file,
                 const std::function<bool(LibraryJob &)> &work, DatabaseScan *result = nullptr) {
    auto job = std::make_shared<LibraryJob>();
    QProgressDialog busy("กำลังปรับปรุงฐานข้อมูลเพลง…", "ยกเลิก", 0, 0, parent);
    busy.setWindowModality(Qt::ApplicationModal);
    busy.setMinimumDuration(0);
    busy.setAutoClose(false);
    busy.setAutoReset(false);
    QEventLoop loop;
    QObject::connect(&busy, &QProgressDialog::canceled, &loop, [job] { job->cancel = true; });
    std::thread worker;
    try {
        worker = std::thread([job, work] {
            try {
                job->success = work(*job);
            } catch (const std::exception &e) {
                job->error = QString::fromUtf8(e.what());
            } catch (...) {
                job->error = "ไม่สามารถปรับปรุงฐานข้อมูลได้";
            }
            job->done = true;
        });
    } catch (const std::exception &e) {
        error(parent, e);
        return false;
    }
    QTimer poll;
    poll.setInterval(30);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (busy.wasCanceled())
            job->cancel = true;
        int total = job->total.load(), position = job->position.load();
        bar->setRange(0, total);
        bar->setValue(position);
        QString name;
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            name = job->file;
        }
        file->setText(name.isEmpty() ? QString("กำลังนับเพลง…") : QFileInfo(name).fileName());
        file->setToolTip(name);
        busy.setLabelText(file->text());
        if (job->done)
            loop.quit();
    });
    poll.start();
    loop.exec();
    poll.stop();
    worker.join();
    busy.close();
    bar->setRange(0, 100);
    bar->setValue(job->success ? 100 : 0);
    if (!job->error.isEmpty() && !job->cancel)
        QMessageBox::warning(parent, "ฐานข้อมูลเพลง", job->error);
    if (!job->success) {
        file->setText(job->cancel ? "ยกเลิกแล้ว — ฐานข้อมูลเดิมยังอยู่" : "ไม่สำเร็จ — ฐานข้อมูลเดิมยังอยู่");
        return false;
    }
    if (result)
        *result = std::move(job->scan);
    return true;
}
} // namespace
MainWindow::MainWindow(bool offline) {
    setWindowTitle("BT Karaoke • Studio 0.4.1");
    QSettings preferences;
    storageRoot = preferences.value("NCN/storage").toString();
    if (!songStorageAllowed(storageRoot, applicationDataPath()))
        storageRoot.clear();
    ncnRoots = preferences.value("NCN/roots").toStringList();
    ncnDatabaseRoot = preferences.value("database/NCN").toString();
    karDatabaseRoot = preferences.value("database/KAR").toString();
    if (ncnDatabaseRoot.isEmpty()) {
        if (validNcnRoot(storageRoot))
            ncnDatabaseRoot = storageRoot;
        else
            for (const auto &root : ncnRoots)
                if (validNcnRoot(root)) {
                    ncnDatabaseRoot = root;
                    break;
                }
    }
    lyricStyle.restore(
        QJsonDocument::fromJson(preferences.value("lyrics/style").toByteArray()).object());

    resize(1440, 900);
    setMinimumSize(1050, 720);
    setStyleSheet(theme());
    QFile registryFile(registry());
    if (registryFile.open(QFile::ReadOnly))
        plugins = QJsonDocument::fromJson(registryFile.readAll()).array();
    auto *root = new QWidget;
    setCentralWidget(root);
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(22, 18, 22, 18);
    outer->setSpacing(16);
    auto *top = new QHBoxLayout;
    outer->addLayout(top);
    auto *brand = new QLabel("BT <span style='color:#a397ff'>Karaoke</span>");
    brand->setObjectName("brand");
    top->addWidget(brand);
    auto *tag = new QLabel("STUDIO  /  LIVE PERFORMANCE");
    tag->setObjectName("muted");
    top->addWidget(tag);
    top->addStretch();
    cpu = new QLabel("DSP 0%");
    top->addWidget(cpu);
    button("Open project", top, [this] { openProject(); }, this);
    button("Save project", top, [this] { saveProject(); }, this);
    button(
        "SoundFont…", top,
        [this] {
            auto p =
                QFileDialog::getOpenFileName(this, "Select SoundFont", {}, "SoundFont (*.sf2)");
            if (!p.isEmpty())
                try {
                    engine.loadSoundfont(p);
                    rebuildMixer();
                    status->setText("SoundFont • " + QFileInfo(p).fileName());
                } catch (const std::exception &e) {
                    error(this, e);
                }
        },
        this);
    button(
        "Audio", top,
        [this] {
            if (engine.audioState == Engine::AudioState::Ready)
                QMessageBox::information(this, "Audio",
                                         "The default system audio output is ready.");
            else
                enableAudio();
        },
        this);
    auto *nav = new QHBoxLayout;
    outer->addLayout(nav);
    pages = new QStackedWidget;
    int index = 0;
    for (auto name : {"Library && queue", "Mixer && FX", "VST3 plugins"}) {
        int page = index++;
        auto *b = button(name, nav, [this, page] { pages->setCurrentIndex(page); }, this);
        b->setMinimumWidth(160);
    }
    nav->addStretch();
    button("Stage / second display", nav, [this] { showStage(); }, this);
    auto *midiControls = new QHBoxLayout;
    outer->addLayout(midiControls);
    button("Lyrics / appearance", midiControls, [this] { lyricsSettings(); }, this);
    button("MIDI / loopMIDI", midiControls, [this] { midiSettings(); }, this);
    button("Panic", midiControls, [this] { engine.panic(); }, this);
    melodyMute = new QCheckBox("ปิดเมโลดี้ (CH9)");
    melodyMute->setObjectName("melodyMute");
    midiControls->addWidget(melodyMute);
    connect(melodyMute, &QCheckBox::toggled, this, [this](bool off) { engine.setMelody(off); });
    midiControls->addStretch();
    outer->addWidget(pages, 1);
    auto *libraryPage = new QWidget;
    pages->addWidget(libraryPage);
    auto *libLayout = new QHBoxLayout(libraryPage);
    libLayout->setContentsMargins(0, 0, 0, 0);
    auto *left = new QVBoxLayout;
    libLayout->addLayout(left, 3);
    auto *heading = new QLabel("Your music. Your stage.");
    heading->setObjectName("heading");
    left->addWidget(heading);
    auto *search = new QLineEdit;
    search->setPlaceholderText("ค้นหารหัสเพลง / ชื่อ / ศิลปิน (แสดงสูงสุด 1,000 รายการ)…");
    left->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &s) { refreshLibrary(s); });
    songs = new QListWidget;
    left->addWidget(songs, 1);
    connect(songs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *i) {
        auto *item = new QListWidgetItem(i->text());
        item->setData(Qt::UserRole, i->data(Qt::UserRole));
        queue->addItem(item);
    });
    auto *libButtons = new QHBoxLayout;
    left->addLayout(libButtons);
    button(
        "＋ เพิ่มเพลง / ฐานข้อมูล", libButtons, [this] { manageNcn(); }, this)
        ->setObjectName("primary");

    button(
        "Add to queue", libButtons,
        [this] {
            auto *i = songs->currentItem();
            if (i) {
                auto *q = new QListWidgetItem(i->text());
                q->setData(Qt::UserRole, i->data(Qt::UserRole));
                queue->addItem(q);
            }
        },
        this);
    auto *right = new QVBoxLayout;
    libLayout->addLayout(right, 2);
    auto *qheading = new QLabel("ON DECK  •  คิวเพลง");
    qheading->setObjectName("heading");
    right->addWidget(qheading);
    queue = new QListWidget;
    queue->setDragDropMode(QAbstractItemView::InternalMove);
    right->addWidget(queue, 1);
    connect(queue, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *i) {
        int row = i->data(Qt::UserRole).toInt();
        delete queue->takeItem(queue->row(i));
        loadRow(row);
        engine.play();
    });
    auto *qb = new QHBoxLayout;
    right->addLayout(qb);
    button(
        "Play selected", qb,
        [this] {
            auto *i = queue->currentItem();
            if (i) {
                int row = i->data(Qt::UserRole).toInt();
                delete queue->takeItem(queue->row(i));
                loadRow(row);
                engine.play();
            }
        },
        this);
    button("Remove", qb, [this] { delete queue->takeItem(queue->currentRow()); }, this);
    auto *hint = new QLabel("ลากเปลี่ยนคิวได้ • ดับเบิลคลิกเพื่อเล่น\nNCN / KAR "
                            "files\nเพิ่ม SoundFont ก่อนเล่นเสียง MIDI");
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    right->addWidget(hint);
    auto *mix = new QWidget;
    pages->addWidget(mix);
    mixerPage(mix);
    auto *plugPage = new QWidget;
    pages->addWidget(plugPage);
    auto *pl = new QVBoxLayout(plugPage);
    auto *ph = new QLabel("Your VST3 rack");
    ph->setObjectName("heading");
    pl->addWidget(ph);
    pl->addWidget(new QLabel(
        "VST3 x64 • mono/stereo effects and note-driven instruments • native editor on Windows"));
    pluginList = new QListWidget;
    pl->addWidget(pluginList, 1);
    for (auto p : plugins) {
        auto o = p.toObject();
        pluginList->addItem(o["name"].toString() + "  /  " + o["vendor"].toString());
    }
    auto *pb = new QHBoxLayout;
    pl->addLayout(pb);
    button("Scan VST3 folder…", pb, [this] { scanPlugins(); }, this)->setObjectName("primary");
    button(
        "Add selected to current strip", pb,
        [this] {
            int row = pluginList->currentRow();
            if (row < 0 || row >= plugins.size())
                return;
            auto p = plugins[row].toObject();
            try {
                auto fx = std::make_shared<VstEffect>(p["path"].toString(), p["id"].toString(),
                                                      Engine::Rate);
                {
                    std::lock_guard<std::mutex> lock(engine.mutex);
                    engine.strips[selected].fx.push_back(fx);
                }
                rebuildChain();
                pages->setCurrentIndex(1);
            } catch (const std::exception &e) {
                error(this, e);
            }
        },
        this);
    auto *transport = new QWidget;
    transport->setObjectName("panel");
    outer->addWidget(transport);
    auto *tl = new QVBoxLayout(transport);
    auto *line = new QHBoxLayout;
    tl->addLayout(line);
    now = new QLabel("Ready for your first song");
    now->setObjectName("heading");
    line->addWidget(now, 1);
    clock = new QLabel("00:00 / 00:00");
    line->addWidget(clock);
    seek = new QSlider(Qt::Horizontal);
    seek->setRange(0, 1000);
    tl->addWidget(seek);
    connect(seek, &QSlider::sliderReleased, this,
            [this] { engine.seek(engine.song.duration * seek->value() / 1000.); });
    auto *controls = new QHBoxLayout;
    tl->addLayout(controls);
    button(
        "▶ Play", controls,
        [this] {
            if (engine.song.path.isEmpty() && songs->currentItem())
                if (!loadRow(songs->currentItem()->data(Qt::UserRole).toInt()))
                    return;
            engine.play();
        },
        this)
        ->setObjectName("primary");
    button("Ⅱ Pause", controls, [this] { engine.pause(); }, this);
    button("■ Stop", controls, [this] { engine.stop(); }, this);
    button("Next →", controls, [this] { next(); }, this);
    controls->addSpacing(16);
    controls->addWidget(new QLabel("KEY"));
    auto *key = new QSpinBox;
    keyControl = key;
    key->setObjectName("keyControl");
    key->setSingleStep(1);
    key->setToolTip("Insert: +1 / Delete: -1");
    key->setRange(-12, 12);
    controls->addWidget(key);
    connect(key, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) { engine.setTranspose(v); });
    controls->addWidget(new QLabel("TEMPO"));
    auto *tempo = new QSpinBox;
    tempoControl = tempo;
    tempo->setObjectName("tempoControl");
    tempo->setSingleStep(1);
    tempo->setToolTip("Page Up: +1% / Page Down: -1%");
    tempo->setRange(50, 150);
    tempo->setValue(100);
    tempo->setSuffix("%");
    controls->addWidget(tempo);
    connect(tempo, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) { engine.setSpeed(v / 100.); });
    controls->addStretch();
    status = new QLabel("Add a SoundFont • import songs • build your FX chains");
    status->setObjectName("muted");
    tl->addWidget(status);
    try {
        songStore = std::make_unique<SongLibrary>(applicationDataPath() + "/library.sqlite");
        reloadLibrary();
    } catch (const std::exception &e) {
        qWarning("Song library: %s", e.what());
    }
    rebuildChain();
    auto *timer = new QTimer(this);
    refreshTimer = timer;
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    applyPerformance();
    qApp->installEventFilter(this);
    status->setWordWrap(true);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status->setMaximumHeight(40);
    if (!offline)
        QTimer::singleShot(0, this, [this] { enableAudio(); });
    else
        status->setText("Safe mode • audio is disabled. Click Audio to enable it.");
}
void MainWindow::enableAudio() {
    if (audioProbe || engine.audioState == Engine::AudioState::Starting)
        return;
    audioAttempted = true;
    status->setText("Checking audio… The window remains available.");
    qInfo("BT_AUDIO_PROBE requested after UI construction");
    auto *probe = new QProcess(this);
    audioProbe = probe;
    connect(probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, probe](int code, QProcess::ExitStatus result) {
                if (audioProbe != probe)
                    return;
                audioProbe = nullptr;
                auto output = QString::fromUtf8(probe->readAllStandardError());
                const bool timeout = probe->property("timedOut").toBool();
                probe->deleteLater();
                if (code == 0 && result == QProcess::NormalExit) {
                    status->setText("Starting audio in the background…");
                    qInfo("BT_AUDIO_START background initialization requested");
                    engine.startAudioAsync();
                } else {
                    status->setText(timeout ? "Audio driver check timed out. Safe mode remains "
                                              "available. Click Audio to retry."
                                            : "Audio is unavailable. You can use the mixer "
                                              "offline; click Audio to retry.");
                    qWarning().noquote() << "BT_AUDIO_PROBE failed" << code << output.right(2000);
                }
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || audioProbe != probe)
            return;
        audioProbe = nullptr;
        status->setText("Audio check could not start. The window remains available.");
        qWarning() << "BT_AUDIO_PROBE failed to start" << probe->errorString();
        probe->deleteLater();
    });
    QTimer::singleShot(12000, probe, [probe] {
        if (probe->state() != QProcess::NotRunning) {
            probe->setProperty("timedOut", true);
            probe->kill();
        }
    });
    probe->start(QCoreApplication::applicationFilePath(), {"--audio-probe"});
}
MainWindow::~MainWindow() {
    if (audioProbe) {
        audioProbe->kill();
        audioProbe->waitForFinished(1000);
    }
    if (stage)
        delete stage;
    engine.stopAudio();
}
void MainWindow::selectPage(int index) {
    pages->setCurrentIndex(index);
}
bool MainWindow::startupSettled() const {
    return audioAttempted && !audioProbe && engine.audioState != Engine::AudioState::Starting;
}
bool MainWindow::smokeReady() const {
    return chain && meters.size() == size_t(engine.trackCount()) && meters[0] && songStore &&
           centralWidget() && engine.strips[MasterPart].fx.size() == 1;
}
bool MainWindow::verifyControlsUi() {
    keyControl->setValue(0);
    tempoControl->setValue(100);
    keyControl->setFocus();
    auto send = [this](int key) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(keyControl, &press);
    };
    send(Qt::Key_Insert);
    if (keyControl->value() != 1)
        return false;
    send(Qt::Key_Delete);
    if (keyControl->value() != 0)
        return false;
    send(Qt::Key_PageUp);
    if (tempoControl->value() != 101)
        return false;
    send(Qt::Key_PageDown);
    if (tempoControl->value() != 100)
        return false;
    keyControl->setValue(12);
    send(Qt::Key_Insert);
    if (keyControl->value() != 12)
        return false;
    keyControl->setValue(0);
    melodyMute->click();
    bool good = engine.melodyMuted() && engine.melodyChannel() == 8;
    melodyMute->click();
    return good;
}
bool MainWindow::verifyImportUi() {
    QTemporaryDir fixture;
    if (!fixture.isValid())
        return false;
    storageRoot = fixture.path() + "/songs";
    auto file = fixture.path() + "/test.kar";
    QByteArray track = QByteArray::fromHex("00903c648360803c0000ff2f00");
    QByteArray bytes = QByteArray::fromHex("4d546864000000060000000101e04d54726b");
    for (int i = 24; i >= 0; i -= 8)
        bytes.append(char(track.size() >> i));
    bytes += track;
    auto write = [&](const QByteArray &data) {
        QFile f(file);
        return f.open(QFile::WriteOnly) && f.write(data) == data.size();
    };
    if (!write(bytes))
        return false;
    bool replace = false;
    int decisions = 0;
    QTimer responder;
    responder.setInterval(10);
    connect(&responder, &QTimer::timeout, this, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        QString want = box->windowTitle() == "ผลวิเคราะห์ก่อนนำเข้า" ? QString("เริ่มนำเข้า")
                       : replace                                 ? QString("ทับไฟล์")
                                                                 : QString("ข้าม");
        for (auto *button : box->buttons())
            if (button->text() == want) {
                ++decisions;
                button->click();
                break;
            }
    });
    responder.start();
    runImport({file}, true);
    auto target = QDir(storageRoot).filePath("KAR/test.kar");
    auto read = [&] {
        QFile f(target);
        if (!f.open(QFile::ReadOnly))
            return QByteArray();
        return f.readAll();
    };
    if (read() != bytes || library.size() != 1)
        return false;
    if (!write(bytes + "updated"))
        return false;
    runImport({file}, true);
    if (read() != bytes)
        return false;
    replace = true;
    runImport({file}, true);
    responder.stop();
    return read() == bytes + "updated" && decisions == 5 && library.size() == 1 &&
           songStorageAllowed(target, applicationDataPath());
}
bool MainWindow::verifyDatabaseUi() {
    QTemporaryDir fixture;
    QByteArray track = QByteArray::fromHex("00903c648360803c0000ff2f00");
    QByteArray midi = QByteArray::fromHex("4d546864000000060000000101e04d54726b");
    for (int i = 24; i >= 0; i -= 8)
        midi.append(char(track.size() >> i));
    midi += track;
    auto ncn = fixture.path() + "/NCN", kar = fixture.path() + "/KAR";
    for (const auto &dir : {ncn + "/Song", ncn + "/Lyrics", ncn + "/Cursor", kar})
        if (!QDir().mkpath(dir))
            return false;
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile f(path);
        return f.open(QFile::WriteOnly) && f.write(bytes) == bytes.size();
    };
    auto song = kar + "/test.kar", old = fixture.path() + "/old.kar";
    if (!write(ncn + "/Song/001.mid", midi) ||
        !write(ncn + "/Lyrics/001.lyr", "Title\nArtist\nC\n\nAB") ||
        !write(ncn + "/Cursor/001.cur", QByteArray::fromHex("00001800")) || !write(song, midi) ||
        !write(old, midi))
        return false;
    rememberSong(readSong(old));
    rememberSong(readSong(song));
    for (int i = 0; i < library.size(); ++i) {
        auto *entry = new QListWidgetItem(library[i].title, queue);
        entry->setData(Qt::UserRole, i);
    }
    ncnDatabaseRoot = ncn;
    karDatabaseRoot = kar;
    bool started = false, good = false;
    QTimer responder;
    responder.setInterval(15);
    connect(&responder, &QTimer::timeout, this, [&] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (box->windowTitle() == "ผลวิเคราะห์ฐานข้อมูลเพลง")
                for (auto *b : box->buttons())
                    if (b->text() == "เริ่มปรับปรุง") {
                        b->click();
                        break;
                    }
            return;
        }
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "songDatabaseDialog")
            return;
        auto *button = dialog->findChild<QPushButton *>("updateSongDatabase");
        if (!started) {
            started = true;
            QTimer::singleShot(0, button, [button] { button->click(); });
            return;
        }
        if (!button->isEnabled())
            return;
        auto *ncnField = dialog->findChild<QLineEdit *>("ncnDatabasePath"),
             *karField = dialog->findChild<QLineEdit *>("karDatabasePath");
        auto *bar = dialog->findChild<QProgressBar *>("databaseProgress");
        good = library.size() == 2 && queue->count() == 1 && bar->value() == 100 &&
               ncnField->text() == ncn && karField->text() == kar;
        if (good) {
            int row = queue->item(0)->data(Qt::UserRole).toInt();
            good = row >= 0 && row < library.size() && library[row].path == song;
        }
        dialog->accept();
    });
    responder.start();
    manageNcn();
    responder.stop();
    QFile unchanged(ncn + "/Song/001.mid");
    if (!unchanged.open(QFile::ReadOnly))
        return false;
    return good && unchanged.readAll() == midi && QFileInfo::exists(old) &&
           !QFileInfo::exists(applicationDataPath() + "/NCN");
}
bool MainWindow::verifyMixerUi() {
    int before = engine.trackCount();
    auto *add = findChild<QPushButton *>("addBusButton");
    if (!add)
        return false;
    add->click();
    QApplication::processEvents();
    int bus = engine.trackCount() - 1;
    if (engine.trackCount() != before + 1 || meters.size() != size_t(engine.trackCount()) ||
        selected != bus)
        return false;
    int option = routeControls[127]->findData(bus);
    if (option < 0)
        return false;
    routeControls[127]->setCurrentIndex(option);
    {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (engine.strips[127].output != bus)
            return false;
        engine.strips[bus].fx.push_back(builtin("Gain", Engine::Rate));
    }
    rebuildChain();
    pages->setCurrentIndex(1);
    return chain->count() == 1 && meters[bus] && selectButtons[bus]->isVisible();
}
void MainWindow::importSongs() {
    importPaths(QFileDialog::getOpenFileNames(this, "เพิ่มเพลง NCN / KAR", {},
                                              "NCN / KAR (*.mid *.midi *.lyr *.cur *.kar)"));
}
void MainWindow::savePreferences() {
    QSettings settings;
    settings.setValue("database/NCN", ncnDatabaseRoot);
    settings.setValue("database/KAR", karDatabaseRoot);
    settings.setValue("NCN/storage", storageRoot);
    settings.setValue("NCN/roots", ncnRoots);
    settings.setValue("lyrics/style",
                      QJsonDocument(lyricStyle.save()).toJson(QJsonDocument::Compact));
}
void MainWindow::reloadLibrary() {
    if (songStore) {
        auto stored = songStore->entries();
        library.clear();
        for (auto &song : stored) {
            auto files = ncnFiles(song.path);
            if (QFileInfo(song.path).suffix().compare("kar", Qt::CaseInsensitive) == 0 ||
                (!files.lyrics.isEmpty() && !files.cursor.isEmpty()))
                library.push_back(std::move(song));
        }
    }
    libraryMap.clear();
    for (int i = 0; i < library.size(); ++i)
        libraryMap.insert(songKey(library[i].path), i);
    refreshLibrary();
}
bool MainWindow::rememberSong(Song song) {
    if (!songStore)
        throw std::runtime_error("Song library database is unavailable");
    songStore->put(song);
    QVector<MidiEvent>().swap(song.events);
    QVector<Lyric>().swap(song.lyrics);
    auto key = songKey(song.path);
    int index = libraryMap.value(key, -1);
    if (index >= 0) {
        library[index] = std::move(song);
        return false;
    }
    libraryMap.insert(key, library.size());
    library.push_back(std::move(song));
    return true;
}
void MainWindow::manageNcn() {
    QDialog dialog(this);
    dialog.setWindowTitle("ฐานข้อมูลเพลง");
    dialog.setObjectName("songDatabaseDialog");
    dialog.resize(760, 440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *locations = new QGroupBox("ที่เก็บเพลง");
    auto *grid = new QGridLayout(locations);
    layout->addWidget(locations);
    auto *ncn = new QLineEdit(ncnDatabaseRoot), *kar = new QLineEdit(karDatabaseRoot);
    ncn->setObjectName("ncnDatabasePath");
    kar->setObjectName("karDatabasePath");
    ncn->setReadOnly(true);
    kar->setReadOnly(true);
    ncn->setPlaceholderText("เลือกโฟลเดอร์ที่มี Song / Lyrics / Cursor");
    kar->setPlaceholderText("เลือกโฟลเดอร์เพลง KAR");
    auto *pickNcn = new QPushButton("…"), *pickKar = new QPushButton("…");
    pickNcn->setObjectName("selectNcnPath");
    pickKar->setObjectName("selectKarPath");
    grid->addWidget(new QLabel("NCN :"), 0, 0);
    grid->addWidget(ncn, 0, 1);
    grid->addWidget(pickNcn, 0, 2);
    grid->addWidget(new QLabel("KAR :"), 1, 0);
    grid->addWidget(kar, 1, 1);
    grid->addWidget(pickKar, 1, 2);
    auto choose = [this, &dialog](QLineEdit *edit, bool isNcn) {
        auto path = QFileDialog::getExistingDirectory(
            &dialog, isNcn ? "เลือกที่เก็บไฟล์เพลง NCN" : "เลือกที่เก็บไฟล์เพลง KAR", edit->text());
        if (path.isEmpty())
            return;
        if (!songStorageAllowed(path, applicationDataPath()) ||
            !songStorageAllowed(path, QCoreApplication::applicationDirPath())) {
            QMessageBox::warning(&dialog, "ที่เก็บเพลง",
                                 "เลือกโฟลเดอร์เพลงนอกข้อมูลและโฟลเดอร์ติดตั้ง BT Karaoke");
            return;
        }
        if (isNcn && !validNcnRoot(path)) {
            QMessageBox::warning(&dialog, "ที่เก็บเพลงไม่ถูกต้อง",
                                 "ต้องมีโฟลเดอร์ Song, Lyrics และ Cursor อยู่ในที่เก็บ NCN");
            return;
        }
        edit->setText(path);
        if (isNcn)
            ncnDatabaseRoot = path;
        else
            karDatabaseRoot = path;
        savePreferences();
    };
    connect(pickNcn, &QPushButton::clicked, &dialog, [=] { choose(ncn, true); });
    connect(pickKar, &QPushButton::clicked, &dialog, [=] { choose(kar, false); });
    auto *group = new QGroupBox("ปรับปรุงฐานข้อมูลเพลง");
    auto *updates = new QVBoxLayout(group);
    layout->addWidget(group);
    auto *progress = new QProgressBar;
    progress->setObjectName("databaseProgress");
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(true);
    progress->setFormat("%v / %m (%p%)");
    progress->setStyleSheet(
        "QProgressBar {color:#e8ecf6;min-height:24px;border:1px solid #34425e;border-radius:4px;} "
        "QProgressBar::chunk {background:#51499c;}");
    auto *line = new QHBoxLayout;
    line->addWidget(new QLabel("ความคืบหน้า :"));
    line->addWidget(progress, 1);
    updates->addLayout(line);
    auto *file = new QLabel("พร้อมปรับปรุงฐานข้อมูล");
    file->setObjectName("databaseCurrentFile");
    file->setWordWrap(true);
    updates->addWidget(file);
    auto *update = new QPushButton("ปรับปรุงฐานข้อมูล");
    update->setObjectName("updateSongDatabase");
    updates->addWidget(update);
    auto *count = new QLabel(QString("จำนวนเพลงทั้งหมด : %1 เพลง").arg(library.size()));
    count->setObjectName("databaseSongCount");
    layout->addWidget(count);
    auto *info =
        new QLabel("เลือกที่เก็บ NCN / KAR แล้วกดปรับปรุง เพื่ออ่านเพลงเข้าฐานข้อมูล\nเพลงอยู่ในตำแหน่งเดิม "
                   "โปรแกรมไม่คัดลอก ย้าย หรือแก้ไฟล์เพลงในขั้นตอนนี้");
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *more = new QToolButton;
    more->setText("เครื่องมือเพิ่มเติม ▸");
    more->setCheckable(true);
    layout->addWidget(more);
    auto *extras = new QWidget;
    auto *extraLayout = new QVBoxLayout(extras);
    button("คัดลอกไฟล์เพลงเพิ่มเติม…", extraLayout, [this] { importSongs(); }, &dialog);
    extras->hide();
    layout->addWidget(extras);
    connect(more, &QToolButton::toggled, extras, &QWidget::setVisible);
    auto *close = new QPushButton("ปิด");
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(update, &QPushButton::clicked, &dialog, [&, this] {
        for (auto *b : {pickNcn, pickKar, update, close})
            b->setEnabled(false);
        more->setEnabled(false);
        if (rebuildSongDatabase(ncn->text(), kar->text(), progress, file, &dialog))
            count->setText(QString("จำนวนเพลงทั้งหมด : %1 เพลง").arg(library.size()));
        for (auto *b : {pickNcn, pickKar, update, close})
            b->setEnabled(true);
        more->setEnabled(true);
    });
    dialog.exec();
}
bool MainWindow::rebuildSongDatabase(const QString &ncn, const QString &kar, QProgressBar *progress,
                                     QLabel *file, QWidget *parent) {
    if (!songStore) {
        QMessageBox::warning(parent, "ฐานข้อมูลเพลง", "ฐานข้อมูลไม่พร้อมใช้งาน");
        return false;
    }
    DatabaseScan scanned;
    if (!libraryTask(
            parent, progress, file,
            [ncn, kar](LibraryJob &job) {
                job.scan = scanDatabase(ncn, kar, [&job](int done, int total, const QString &name) {
                    return job.progress(done, total, name);
                });
                return !job.scan.canceled;
            },
            &scanned))
        return false;
    QMessageBox summary(parent);
    summary.setWindowTitle("ผลวิเคราะห์ฐานข้อมูลเพลง");
    int ncnCount = 0;
    for (const auto &item : scanned.plan.items)
        if (item.ncn)
            ++ncnCount;
    summary.setText(QString("พบไฟล์เพลง %1 ไฟล์\nอ่านได้: NCN %2 / KAR %3\nรหัสซ้ำ %4 รายการ / อ่านไม่ได้ "
                            "%5 ไฟล์\n\nเมื่อเริ่ม ฐานข้อมูลจะมีเฉพาะเพลงจากที่เก็บที่เลือก ไฟล์เพลงยังอยู่ที่เดิม")
                        .arg(scanned.discovered)
                        .arg(ncnCount)
                        .arg(scanned.plan.items.size() - ncnCount)
                        .arg(scanned.plan.duplicates)
                        .arg(scanned.plan.errors.size()));
    summary.setDetailedText(scanned.plan.errors.mid(0, 30).join('\n'));
    auto *start = summary.addButton("เริ่มปรับปรุง", QMessageBox::AcceptRole);
    summary.addButton(QMessageBox::Cancel);
    summary.exec();
    if (summary.clickedButton() != start) {
        file->setText("ยกเลิกแล้ว — ฐานข้อมูลเดิมยังอยู่");
        return false;
    }
    QVector<Song> accepted;
    QHash<QString, int> keys;
    int all = 0, skipped = 0;
    for (const auto &item : scanned.plan.items) {
        auto key = databaseSongKey(item);
        int existing = keys.value(key, -1);
        if (existing >= 0) {
            int choice = all;
            if (!choice) {
                QMessageBox question(parent);
                question.setWindowTitle("พบรหัสเพลงซ้ำ");
                question.setText(item.song.id + " • " + item.song.title);
                question.setInformativeText(
                    "เลือกข้าม หรือใช้รายการนี้ทับรายการรหัสเดียวกันในฐานข้อมูล (ไม่แก้ไฟล์เพลง)");
                question.setDetailedText("รายการก่อนหน้า: " + accepted[existing].path +
                                         "\nรายการนี้: " + item.song.path);
                auto *skip = question.addButton("ข้าม", QMessageBox::RejectRole),
                     *replace = question.addButton("ทับรายการ", QMessageBox::AcceptRole);
                question.addButton(QMessageBox::Cancel);
                auto *apply = new QCheckBox("ใช้กับรหัสซ้ำที่เหลือ");
                question.setCheckBox(apply);
                question.setDefaultButton(skip);
                question.exec();
                if (question.clickedButton() == skip)
                    choice = 1;
                else if (question.clickedButton() == replace)
                    choice = 2;
                else {
                    file->setText("ยกเลิกแล้ว — ฐานข้อมูลเดิมยังอยู่");
                    return false;
                }
                if (apply->isChecked())
                    all = choice;
            }
            if (choice == 1) {
                ++skipped;
                continue;
            }
            accepted[existing] = item.song;
        } else {
            keys.insert(key, accepted.size());
            accepted.push_back(item.song);
        }
    }
    QStringList queued;
    for (int i = 0; i < queue->count(); ++i) {
        int row = queue->item(i)->data(Qt::UserRole).toInt();
        if (row >= 0 && row < library.size())
            queued << library[row].path;
    }
    const auto databaseFile = applicationDataPath() + "/library.sqlite";
    if (!libraryTask(parent, progress, file, [accepted, databaseFile](LibraryJob &job) {
            SongLibrary writer(databaseFile);
            writer.replaceAll(accepted, [&job](int done, int total, const QString &name) {
                return job.progress(done, total, name);
            });
            return true;
        }))
        return false;
    ncnDatabaseRoot = ncn;
    karDatabaseRoot = kar;
    savePreferences();
    reloadLibrary();
    queue->clear();
    for (const auto &path : queued) {
        int index = libraryMap.value(songKey(path), -1);
        if (index >= 0) {
            auto *entry = new QListWidgetItem(library[index].title, queue);
            entry->setData(Qt::UserRole, index);
        }
    }
    file->setText(QString("ปรับปรุงเสร็จแล้ว %1 เพลง • ข้ามรหัสซ้ำ %2 • อ่านไม่ได้ %3")
                      .arg(library.size())
                      .arg(skipped)
                      .arg(scanned.plan.errors.size()));
    status->setText(file->text());
    return true;
}
bool MainWindow::chooseStorage() {
    if (songStorageAllowed(storageRoot, applicationDataPath()) &&
        songStorageAllowed(storageRoot, QCoreApplication::applicationDirPath()))
        return true;
    auto root = QFileDialog::getExistingDirectory(
        this, "เลือกที่เก็บเพลงนอกโฟลเดอร์ข้อมูล BT Karaoke",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    if (root.isEmpty())
        return false;
    if (!songStorageAllowed(root, applicationDataPath()) ||
        !songStorageAllowed(root, QCoreApplication::applicationDirPath())) {
        QMessageBox::warning(this, "ที่เก็บเพลง",
                             "กรุณาเลือกโฟลเดอร์เพลงนอกข้อมูลและโฟลเดอร์ติดตั้ง BT Karaoke");
        return false;
    }
    storageRoot = root;
    savePreferences();
    return true;
}
void MainWindow::importPaths(const QStringList &paths) {
    runImport(paths, true);
}
void MainWindow::runImport(const QStringList &paths, bool copy) {
    if (paths.isEmpty())
        return;
    if (copy && !chooseStorage())
        return;
    QProgressDialog analysis("กำลังวิเคราะห์จำนวนเพลงและชื่อซ้ำ…", "ยกเลิก", 0, paths.size(), this);
    analysis.setWindowModality(Qt::ApplicationModal);
    auto plan = analyzeImport(paths, storageRoot, copy, [&](int i, int) {
        analysis.setValue(i);
        return !analysis.wasCanceled();
    });
    bool canceled = analysis.wasCanceled();
    analysis.close();
    if (canceled)
        return;
    for (auto &item : plan.items)
        if (!item.duplicate && libraryMap.contains(songKey(item.destination))) {
            item.duplicate = true;
            ++plan.duplicates;
        }
    if (plan.items.isEmpty()) {
        QMessageBox::information(this, "วิเคราะห์เพลง",
                                 "ไม่พบเพลง NCN / KAR ที่นำเข้าได้\n" +
                                     plan.errors.mid(0, 20).join('\n'));
        return;
    }
    QMessageBox summary(this);
    summary.setWindowTitle("ผลวิเคราะห์ก่อนนำเข้า");
    summary.setText(QString("พบเพลง %1 เพลง\nชื่อไฟล์/รายการซ้ำ %2 เพลง\nไฟล์ที่ไม่ผ่าน %3 ไฟล์\n\n%4")
                        .arg(plan.items.size())
                        .arg(plan.duplicates)
                        .arg(plan.errors.size())
                        .arg(copy ? "ที่เก็บ: " + QDir::toNativeSeparators(storageRoot)
                                  : "เพิ่มรายการจากที่เก็บเดิม โดยไม่คัดลอกไฟล์"));
    summary.setDetailedText(plan.errors.mid(0, 30).join('\n'));
    auto *start = summary.addButton("เริ่มนำเข้า", QMessageBox::AcceptRole);
    summary.addButton(QMessageBox::Cancel);
    summary.exec();
    if (summary.clickedButton() != start)
        return;
    enum Choice { Ask, Skip, Overwrite };
    Choice all = Ask;
    int added = 0, updated = 0, skipped = 0, failed = 0;
    QStringList errors;
    QProgressDialog progress("กำลังนำเข้าเพลง…", "ยกเลิก", 0, plan.items.size(), this);
    progress.setWindowModality(Qt::ApplicationModal);
    try {
        if (!songStore)
            throw std::runtime_error("Library unavailable");
        songStore->beginBatch();
    } catch (const std::exception &e) {
        error(this, e);
        return;
    }
    for (int i = 0; i < plan.items.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled())
            break;
        auto item = plan.items[i];
        bool duplicate = item.duplicate || libraryMap.contains(songKey(item.destination));
        if (copy && QFileInfo::exists(item.destination))
            duplicate = true;
        Choice choice = Overwrite;
        if (duplicate) {
            choice = all;
            if (choice == Ask) {
                QMessageBox question(this);
                question.setWindowTitle("พบชื่อไฟล์เพลงซ้ำ");
                question.setText(item.song.id + " • " + item.song.title + "\n" +
                                 QDir::toNativeSeparators(item.destination));
                question.setInformativeText(copy ? "ต้องการข้าม หรือทับไฟล์เพลงและไฟล์คู่กัน?"
                                                 : "ต้องการข้าม หรือทับข้อมูลรายการเดิมในคลัง?");
                auto *skip = question.addButton("ข้าม", QMessageBox::RejectRole);
                auto *replace =
                    question.addButton(copy ? "ทับไฟล์" : "ทับรายการ", QMessageBox::AcceptRole);
                question.addButton(QMessageBox::Cancel);
                auto *apply = new QCheckBox("ใช้กับรายการซ้ำที่เหลือ");
                question.setCheckBox(apply);
                question.setDefaultButton(skip);
                question.exec();
                if (question.clickedButton() == skip)
                    choice = Skip;
                else if (question.clickedButton() == replace)
                    choice = Overwrite;
                else
                    break;
                if (apply->isChecked())
                    all = choice;
            }
        }
        if (choice == Skip) {
            ++skipped;
            continue;
        }
        try {
            auto song = item.song;
            if (copy)
                song.path = item.ncn ? SongLibrary::copyNcn(song.path, storageRoot,
                                                            duplicate && choice == Overwrite)
                                     : SongLibrary::copyKar(song.path, item.destination,
                                                            duplicate && choice == Overwrite);
            if (rememberSong(std::move(song)))
                ++added;
            else
                ++updated;
        } catch (const std::exception &e) {
            ++failed;
            if (errors.size() < 30)
                errors << item.song.id + ": " + e.what();
        }
    }
    try {
        songStore->endBatch();
    } catch (const std::exception &e) {
        error(this, e);
    }
    progress.close();
    refreshLibrary();
    status->setText(QString("เพิ่ม %1 • ทับ/อัปเดต %2 • ข้าม %3 • ไม่สำเร็จ %4")
                        .arg(added)
                        .arg(updated)
                        .arg(skipped)
                        .arg(failed));
    if (!errors.isEmpty())
        QMessageBox::warning(this, "ผลนำเข้า", errors.join('\n'));
}
void MainWindow::rebuildMixer() {
    int page = pages->currentIndex();
    auto *old = pages->widget(1);
    pages->removeWidget(old);
    delete old;
    auto *mix = new QWidget;
    pages->insertWidget(1, mix);
    selected = std::clamp(selected, 0, engine.trackCount() - 1);
    mixerPage(mix);
    rebuildChain();
    pages->setCurrentIndex(page);
}
void MainWindow::midiSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("MIDI / loopMIDI output");
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel("สร้างพอร์ตใน loopMIDI ก่อน แล้วเลือกพอร์ตด้านล่าง\n"
                            "MIDI ส่งโน้ตไปยังโปรแกรมเสียงภายนอก ไม่ส่งเสียงหรือ FX\n"
                            "ต้องมีอุปกรณ์เสียงพร้อมทำงานเพื่อเดินเวลาเล่นเพลง");
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *ports = new QComboBox;
    ports->addItems(MidiOutput::ports());
    layout->addWidget(ports);
    auto *mode = new QComboBox;
    mode->addItems({"External MIDI only", "Internal audio + external MIDI"});
    layout->addWidget(mode);
    button(
        "Refresh ports", layout,
        [ports] {
            ports->clear();
            ports->addItems(MidiOutput::ports());
        },
        &dialog);
    button(
        "Connect", layout,
        [this, ports, mode, &dialog] {
            if (ports->currentIndex() < 0)
                return;
            QString message;
            if (!engine.openMidiOutput(ports->currentIndex(), message)) {
                QMessageBox::warning(&dialog, "MIDI output", message);
                return;
            }
            engine.setOutputMode(mode->currentIndex() == 0 ? Engine::OutputMode::Midi
                                                           : Engine::OutputMode::Both);
            engine.seek(engine.position.load());
            status->setText("MIDI → " + ports->currentText());
            dialog.accept();
        },
        &dialog);
    button(
        "Disconnect / use internal audio", layout,
        [this, &dialog] {
            engine.closeMidiOutput();
            status->setText("Internal audio");
            dialog.accept();
        },
        &dialog);
    dialog.exec();
}
void MainWindow::refreshLibrary(const QString &filter) {
    songs->clear();
    int shown = 0;
    for (int i = 0; i < library.size(); ++i) {
        const auto &s = library[i];
        QString text = s.id + "  /  " + s.title + "  •  " + s.artist + "  /  " + s.key + "  /  " +
                       QString::number(s.bpm, 'f', 0) + " BPM";
        if (!filter.isEmpty() && !text.contains(filter, Qt::CaseInsensitive))
            continue;
        if (shown++ >= 1000)
            break;
        auto *item = new QListWidgetItem(text, songs);
        item->setData(Qt::UserRole, i);
    }
}
bool MainWindow::loadRow(int row) {
    if (row < 0 || row >= library.size())
        return false;
    try {
        if (!supportedSong(library[row].path))
            throw std::runtime_error("รองรับ NCN / KAR เท่านั้น");
        engine.load(readSong(library[row].path));
        syncLyrics();
    } catch (const std::exception &e) {
        error(this, e);
        return false;
    }
    now->setText(library[row].title);
    if (engine.soundfontPath().isEmpty())
        status->setText("เลือก SoundFont เพื่อให้ MIDI มีเสียง หรือเพิ่ม VST3 instrument ในช่องมิกเซอร์");
    return true;
}
void MainWindow::next() {
    if (!queue->count())
        return;
    auto *i = queue->takeItem(0);
    bool loaded = loadRow(i->data(Qt::UserRole).toInt());
    delete i;
    if (loaded)
        engine.play();
}
void MainWindow::mixerPage(QWidget *w) {
    const int count = engine.trackCount();
    meters.assign(count, nullptr);
    selectButtons.assign(count, nullptr);
    muteButtons.assign(count, nullptr);
    soloButtons.assign(count, nullptr);
    volumeControls.assign(count, nullptr);
    panControls.assign(count, nullptr);
    fxSummaries.assign(count, nullptr);
    auto *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *left = new QVBoxLayout;
    layout->addLayout(left, 3);
    auto *head = new QHBoxLayout;
    left->addLayout(head);
    auto *t = new QLabel("128 SOUNDFONT SOUNDS");
    t->setObjectName("heading");
    head->addWidget(t);
    auto *filter = new QComboBox;
    filter->addItems({"All channels", "Drums only", "SoundFont • 128 sounds", "Buses only"});
    filter->setMaximumWidth(190);
    head->addStretch();
    auto *addBusButton = button(
        "＋ Bus", head,
        [this] {
            try {
                selected = engine.addBus();
                QTimer::singleShot(0, this, [this] { rebuildMixer(); });
            } catch (const std::exception &e) {
                error(this, e);
            }
        },
        this);
    addBusButton->setObjectName("addBusButton");
    head->addWidget(filter);
    auto *soundSearch = new QLineEdit;
    soundSearch->setPlaceholderText("ค้นหาเสียง / โปรแกรม…");
    soundSearch->setMaximumWidth(180);
    head->addWidget(soundSearch);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *mixArea = new QHBoxLayout;
    mixArea->addWidget(scroll, 1);
    left->addLayout(mixArea, 1);
    auto *strips = new QWidget;
    strips->setObjectName("mixerSurface");
    auto *row = new QHBoxLayout(strips);
    std::vector<QWidget *> widgets(count);
    for (int p = 0; p < count; ++p) {
        auto *strip = new QWidget;
        strip->setObjectName("panel");
        strip->setFixedWidth(158);
        widgets[p] = strip;
        if (p == MasterPart)
            mixArea->addWidget(strip);
        else
            row->addWidget(strip);
        auto *v = new QVBoxLayout(strip);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(5);
        auto *select = new QPushButton(QString(partName(p)).replace(" • ", "\n"));
        select->setContextMenuPolicy(Qt::CustomContextMenu);
        select->setToolTip("Right-click to rename this channel / bus");
        connect(select, &QWidget::customContextMenuRequested, this, [this, p] {
            QString old;
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                old = engine.strips[p].name;
            }
            bool ok = false;
            auto name = QInputDialog::getText(this, "Rename channel / bus", "Name",
                                              QLineEdit::Normal, old, &ok)
                            .trimmed();
            if (ok && !name.isEmpty()) {
                std::lock_guard<std::mutex> lock(engine.mutex);
                engine.strips[p].name = name.left(80);
            }
        });
        select->setCheckable(true);
        select->setChecked(p == selected);
        select->setMinimumHeight(54);
        selectButtons[p] = select;
        v->addWidget(select);
        connect(select, &QPushButton::clicked, this, [this, p] {
            selected = p;
            for (int i = 0; i < int(meters.size()); ++i)
                selectButtons[i]->setChecked(i == p);
            rebuildChain();
        });
        auto *tag =
            new QLabel(p == MasterPart  ? "BUS / STEREO"
                       : p >= BusFirst  ? "GROUP / FX RETURN"
                       : p >= DrumFirst ? "DRUM NOTE"
                                        : QString("SF2 PROGRAM %1").arg(p, 3, 10, QChar('0')));
        tag->setObjectName("muted");
        tag->setAlignment(Qt::AlignCenter);
        v->addWidget(tag);
        auto *summary = new QLabel("No FX");
        summary->setWordWrap(true);
        summary->setMaximumHeight(65);
        summary->setObjectName("muted");
        fxSummaries[p] = summary;
        v->addWidget(summary);
        auto *add = new QPushButton("＋ FX");
        v->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this, p] {
            selected = p;
            rebuildChain();
            addEffect();
        });
        auto *meterLine = new QHBoxLayout;
        v->addLayout(meterLine, 1);
        auto *gain = new QSlider(Qt::Vertical);
        volumeControls[p] = gain;
        gain->setRange(0, 150);
        gain->setValue(80);
        meterLine->addWidget(gain);
        auto *meter = new QProgressBar;
        meter->setOrientation(Qt::Vertical);
        meter->setRange(0, 100);
        meter->setTextVisible(false);
        meter->setFixedWidth(14);
        meterLine->addWidget(meter);
        meters[p] = meter;
        connect(gain, &QSlider::valueChanged, this, [this, p](int n) {
            std::lock_guard<std::mutex> lock(engine.mutex);
            engine.strips[p].volume = n / 100.;
        });
        auto *pan = new QSlider(Qt::Horizontal);
        panControls[p] = pan;
        pan->setRange(-100, 100);
        pan->setToolTip("Pan");
        v->addWidget(pan);
        connect(pan, &QSlider::valueChanged, this, [this, p](int n) {
            std::lock_guard<std::mutex> lock(engine.mutex);
            engine.strips[p].pan = n / 100.;
        });
        auto *ms = new QHBoxLayout;
        v->addLayout(ms);
        auto *m = new QPushButton("M");
        muteButtons[p] = m;
        m->setCheckable(true);
        ms->addWidget(m);
        auto *s = new QPushButton("S");
        soloButtons[p] = s;
        s->setCheckable(true);
        s->setEnabled(p != MasterPart);
        ms->addWidget(s);
        connect(m, &QPushButton::toggled, this, [this, p](bool v) { engine.setMute(p, v); });
        connect(s, &QPushButton::toggled, this, [this, p](bool v) { engine.setSolo(p, v); });
        if (p < PartCount) {
            auto *route = new QComboBox;
            routeControls[p] = route;
            route->addItem("→ Master", MasterPart);
            for (int b = BusFirst; b < count; ++b)
                route->addItem("→ " + partName(b), b);
            route->setToolTip("Group output (post FX / fader)");
            v->addWidget(route);
            connect(route, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this, p, route](int i) { engine.setRoute(p, route->itemData(i).toInt()); });
            auto *send = new QSlider(Qt::Horizontal);
            send->setRange(0, 100);
            send->setToolTip("Post-fader send → Bus 5 (0–100%)");
            sendControls[p] = send;
            v->addWidget(new QLabel("Send → Bus 5"));
            v->addWidget(send);
            connect(send, &QSlider::valueChanged, this, [this, p](int value) {
                std::lock_guard<std::mutex> lock(engine.mutex);
                engine.strips[p].fxSend = value / 100.;
            });
        } else {
            v->addWidget(new QLabel(p == MasterPart ? "Stereo output" : "→ Master"));
        }
        auto *audition = new QPushButton(p == MasterPart ? "Master FX" : "♪ Audition");
        v->addWidget(audition);
        connect(audition, &QPushButton::clicked, this, [this, p] {
            if (p < PartCount)
                engine.testNote(p);
            else {
                selected = p;
                rebuildChain();
            }
        });
    }
    row->addStretch();
    scroll->setWidget(strips);
    auto filterStrips = [this, widgets, filter, soundSearch] {
        int f = filter->currentIndex();
        auto search = soundSearch->text();
        for (int i = 0; i < int(widgets.size()); ++i) {
            bool category = f == 0 || (f == 1 && i >= DrumFirst && i < PartCount) ||
                            (f == 2 && i < ProgramCount) || (f == 3 && i >= BusFirst);
            QString name;
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                name = engine.strips[i].name;
            }
            widgets[i]->setVisible(
                i == MasterPart ||
                (category && (search.isEmpty() || name.contains(search, Qt::CaseInsensitive) ||
                              QString::number(i).contains(search))));
        }
    };
    connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [filterStrips](int) { filterStrips(); });
    connect(soundSearch, &QLineEdit::textChanged, this,
            [filterStrips](const QString &) { filterStrips(); });
    filter->setCurrentIndex(selected >= BusFirst                            ? 3
                            : selected >= DrumFirst && selected < PartCount ? 1
                                                                            : 2);
    auto *fxPanel = new QWidget;
    fxPanel->setObjectName("panel");
    fxPanel->setMinimumWidth(330);
    fxPanel->setMaximumWidth(430);
    layout->addWidget(fxPanel, 1);
    auto *fx = new QVBoxLayout(fxPanel);
    chainTitle = new QLabel;
    chainTitle->setObjectName("heading");
    fx->addWidget(chainTitle);
    auto *hint = new QLabel("Signal flows top → bottom\nDrag to reorder • double-click to edit");
    hint->setObjectName("muted");
    fx->addWidget(hint);
    chain = new QListWidget;
    chain->setDragDropMode(QAbstractItemView::InternalMove);
    fx->addWidget(chain, 1);
    connect(chain, &QListWidget::itemDoubleClicked, this, [this] { editEffect(); });
    connect(chain->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (refreshing)
            return;
        std::lock_guard<std::mutex> lock(engine.mutex);
        auto old = engine.strips[selected].fx;
        std::vector<std::shared_ptr<Effect>> ordered;
        for (int i = 0; i < chain->count(); ++i) {
            auto id = chain->item(i)->data(Qt::UserRole).toULongLong();
            for (auto &f : old)
                if (reinterpret_cast<quintptr>(f.get()) == id) {
                    ordered.push_back(f);
                    break;
                }
        }
        if (ordered.size() == old.size())
            engine.strips[selected].fx = std::move(ordered);
    });
    button("＋ Add FX", fx, [this] { addEffect(); }, this)->setObjectName("primary");
    auto *edit = new QHBoxLayout;
    fx->addLayout(edit);
    button("Edit", edit, [this] { editEffect(); }, this);
    button(
        "Bypass", edit,
        [this] {
            int i = chain->currentRow();
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                auto &list = engine.strips[selected].fx;
                if (i >= 0 && i < int(list.size()))
                    list[size_t(i)]->bypass = !list[size_t(i)]->bypass;
            }
            rebuildChain();
        },
        this);
    button(
        "Remove", edit,
        [this] {
            int i = chain->currentRow();
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                auto &list = engine.strips[selected].fx;
                if (i >= 0 && i < int(list.size()))
                    list.erase(list.begin() + i);
            }
            rebuildChain();
        },
        this);
    button(
        "Duplicate selected FX", fx,
        [this] {
            QJsonObject state;
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                int row = chain->currentRow();
                auto &list = engine.strips[selected].fx;
                if (row < 0 || row >= int(list.size()))
                    return;
                state = list[size_t(row)]->save();
            }
            try {
                auto copy = engine.restoreEffect(state);
                {
                    std::lock_guard<std::mutex> lock(engine.mutex);
                    engine.strips[selected].fx.push_back(copy);
                }
                rebuildChain();
            } catch (const std::exception &e) {
                error(this, e);
            }
        },
        this);
    auto *presets = new QHBoxLayout;
    fx->addLayout(presets);
    button("Save chain", presets, [this] { saveChain(); }, this);
    button("Load chain", presets, [this] { loadChain(); }, this);
}
void MainWindow::rebuildChain() {
    refreshing = true;
    chain->clear();
    chainTitle->setText(partName(selected) + " / FX");
    {
        std::lock_guard<std::mutex> lock(engine.mutex);
        for (auto &f : engine.strips[selected].fx) {
            auto *i = new QListWidgetItem((f->bypass ? "○  " : "●  ") + f->name(), chain);
            i->setData(Qt::UserRole, qulonglong(reinterpret_cast<quintptr>(f.get())));
        }
    }
    refreshing = false;
}
void MainWindow::addEffect() {
    QMenu menu;
    for (auto &t : builtinNames())
        menu.addAction(t, [this, t] {
            auto f = builtin(t, Engine::Rate);
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                engine.strips[selected].fx.push_back(f);
            }
            rebuildChain();
        });
    if (!plugins.isEmpty()) {
        auto *vst = menu.addMenu("VST3");
        for (auto value : plugins) {
            auto p = value.toObject();
            vst->addAction(p["name"].toString(), [this, p] {
                try {
                    auto f = std::make_shared<VstEffect>(p["path"].toString(), p["id"].toString(),
                                                         Engine::Rate);
                    {
                        std::lock_guard<std::mutex> lock(engine.mutex);
                        engine.strips[selected].fx.push_back(f);
                    }
                    rebuildChain();
                } catch (const std::exception &e) {
                    error(this, e);
                }
            });
        }
    }
    menu.addSeparator();
    menu.addAction("Scan VST3 folder…", [this] { scanPlugins(); });
    menu.exec(QCursor::pos());
}
void MainWindow::editEffect() {
    std::shared_ptr<Effect> f;
    {
        std::lock_guard<std::mutex> lock(engine.mutex);
        int row = chain->currentRow();
        auto &list = engine.strips[selected].fx;
        if (row < 0 || row >= int(list.size()))
            return;
        f = list[size_t(row)];
    }
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(f->name());
    d->resize(520, 480);
    auto *l = new QVBoxLayout(d);
    auto *native = new QPushButton("Open native VST3 editor");
    native->setEnabled(bool(std::dynamic_pointer_cast<VstEffect>(f)));
    l->addWidget(native);
    connect(native, &QPushButton::clicked, d, [this, f] {
        if (!f->showEditor())
            QMessageBox::information(
                this, "Plugin editor",
                "Native editor is unavailable; use the parameter controls below.");
    });
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    l->addWidget(scroll, 1);
    auto *body = new QWidget;
    auto *form = new QFormLayout(body);
    auto parameters = f->parameters();
    for (size_t i = 0; i < parameters.size(); ++i) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(0, 1000);
        s->setValue(int(parameters[i].value * 1000));
        form->addRow(parameters[i].name, s);
        connect(s, &QSlider::valueChanged, d, [this, f, i](int v) {
            std::lock_guard<std::mutex> lock(engine.mutex);
            f->setParameter(int(i), v / 1000.);
        });
    }
    scroll->setWidget(body);
    d->show();
}
void MainWindow::scanPlugins() {
    QString base =
#ifdef _WIN32
        qEnvironmentVariable("CommonProgramFiles") + "/VST3";
#else
        QDir::homePath() + "/.vst3";
#endif
    auto folder = QFileDialog::getExistingDirectory(this, "Scan trusted VST3 plugins", base);
    if (folder.isEmpty())
        return;
    QStringList paths;
    if (folder.endsWith(".vst3", Qt::CaseInsensitive))
        paths << folder;
    else {
        QDirIterator it(folder, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            auto p = it.next();
            if (p.endsWith(".vst3", Qt::CaseInsensitive))
                paths << p;
        }
    }
    QStringList failed;
    QProgressDialog progress("Scanning plugins in separate processes…", "Cancel", 0, paths.size(),
                             this);
    progress.setWindowModality(Qt::WindowModal);
    for (int i = 0; i < paths.size(); ++i) {
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;
        QTemporaryFile result;
        result.open();
        QString resultPath = result.fileName();
        result.close();
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(),
                      {"--scan-vst3", paths[i], resultPath});
        if (!process.waitForFinished(12000)) {
            process.kill();
            process.waitForFinished();
            failed << QFileInfo(paths[i]).fileName() + " (timeout)";
            continue;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            failed << QFileInfo(paths[i]).fileName() + " (scan failed)";
            continue;
        }
        QFile output(resultPath);
        if (!output.open(QFile::ReadOnly)) {
            failed << paths[i] + " (no scan report)";
            continue;
        }
        auto found = QJsonDocument::fromJson(output.readAll()).array();
        for (auto p : found) {
            bool exists = false;
            for (auto old : plugins)
                if (old.toObject()["id"] == p.toObject()["id"] &&
                    old.toObject()["path"] == p.toObject()["path"])
                    exists = true;
            if (!exists)
                plugins.append(p);
        }
    }
    progress.setValue(paths.size());
    writePlugins();
    pluginList->clear();
    for (auto p : plugins) {
        auto o = p.toObject();
        pluginList->addItem(o["name"].toString() + "  /  " + o["vendor"].toString());
    }
    status->setText(QString("%1 VST3 classes registered").arg(plugins.size()));
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Plugin scan report", failed.join('\n'));
}
void MainWindow::writePlugins() {
    QSaveFile f(registry());
    if (f.open(QFile::WriteOnly)) {
        f.write(QJsonDocument(plugins).toJson());
        f.commit();
    }
}
void MainWindow::saveProject() {
    auto path =
        QFileDialog::getSaveFileName(this, "Save project", projectPath, "BT project (*.btproject)");
    if (path.isEmpty())
        return;
    try {
        auto obj = engine.save();
        QJsonArray files;
        for (auto &s : library)
            files.append(s.path);
        obj["library"] = files;
        obj["lyricsStyle"] = lyricStyle.save();
        QJsonArray queued;
        for (int i = 0; i < queue->count(); ++i)
            queued.append(queue->item(i)->data(Qt::UserRole).toInt());
        obj["queue"] = queued;
        QSaveFile f(path);
        if (!f.open(QFile::WriteOnly) || f.write(QJsonDocument(obj).toJson()) < 0 || !f.commit())
            throw std::runtime_error("Cannot save project");
        projectPath = path;
        status->setText("Project saved");
    } catch (const std::exception &e) {
        error(this, e);
    }
}
void MainWindow::openProject() {
    auto path = QFileDialog::getOpenFileName(this, "Open project", {}, "BT project (*.btproject)");
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QFile::ReadOnly))
        return;
    QJsonParseError parse;
    auto obj = QJsonDocument::fromJson(f.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "Project", "Invalid project JSON");
        return;
    }
    try {
        if (!obj["song"].toString().isEmpty() && !supportedSong(obj["song"].toString()))
            obj["song"] = QString();
        auto warnings = engine.restore(obj);
        if (obj.contains("lyricsStyle")) {
            lyricStyle.restore(obj["lyricsStyle"].toObject());
            savePreferences();
            if (lyrics)
                lyrics->setAppearance(lyricStyle);
        }
        library.clear();
        libraryMap.clear();
        QVector<int> mapping;
        for (auto v : obj["library"].toArray()) {
            try {
                if (!supportedSong(v.toString()))
                    throw std::runtime_error("รองรับ NCN / KAR เท่านั้น");
                auto s = readSong(v.toString());
                mapping.push_back(library.size());
                libraryMap.insert(songKey(s.path), library.size());
                QVector<MidiEvent>().swap(s.events);
                QVector<Lyric>().swap(s.lyrics);
                library.push_back(s);
            } catch (const std::exception &e) {
                mapping.push_back(-1);
                warnings << e.what();
            }
        }
        refreshLibrary();
        queue->clear();
        for (auto v : obj["queue"].toArray()) {
            int original = v.toInt(-1);
            int i = (original >= 0 && original < mapping.size()) ? mapping[original] : -1;
            if (i >= 0 && i < library.size()) {
                auto *q = new QListWidgetItem(library[i].title, queue);
                q->setData(Qt::UserRole, i);
            }
        }
        syncLyrics();
        applyPerformance();
        now->setText(engine.song.title);
        projectPath = path;
        rebuildMixer();
        if (!warnings.isEmpty())
            QMessageBox::warning(this, "Project loaded with missing items", warnings.join('\n'));
    } catch (const std::exception &e) {
        error(this, e);
    }
}
void MainWindow::saveChain() {
    auto path = QFileDialog::getSaveFileName(this, "Save FX chain", {}, "BT FX chain (*.btchain)");
    if (path.isEmpty())
        return;
    QJsonArray list;
    {
        std::lock_guard<std::mutex> lock(engine.mutex);
        for (auto &f : engine.strips[selected].fx)
            list.append(f->save());
    }
    QSaveFile f(path);
    if (f.open(QFile::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"schema", 1}, {"effects", list}}).toJson());
        if (!f.commit())
            QMessageBox::warning(this, "FX chain", "Save failed");
    }
}
void MainWindow::loadChain() {
    auto path = QFileDialog::getOpenFileName(this, "Load FX chain", {}, "BT FX chain (*.btchain)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
        return;
    auto obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj["schema"].toInt() != 1) {
        QMessageBox::warning(this, "FX chain", "Unsupported chain format");
        return;
    }
    try {
        std::vector<std::shared_ptr<Effect>> list;
        for (auto v : obj["effects"].toArray())
            list.push_back(engine.restoreEffect(v.toObject()));
        {
            std::lock_guard<std::mutex> lock(engine.mutex);
            engine.strips[selected].fx = std::move(list);
        }
        rebuildChain();
    } catch (const std::exception &e) {
        error(this, e);
    }
}
void MainWindow::applyPerformance() {
    engine.setPerformance(lyricStyle.performance);
    if (refreshTimer)
        refreshTimer->start(lyricStyle.mixerInterval());
    if (lyrics)
        lyrics->setAppearance(lyricStyle);
}
void MainWindow::syncLyrics() {
    if (!lyrics)
        return;
    std::lock_guard<std::mutex> lock(engine.mutex);
    lyrics->setSong(engine.song.lyrics, engine.song.duration);
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
        !QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
        auto *key = static_cast<QKeyEvent *>(event);
        int step = key->key();
        if (key->modifiers() == Qt::NoModifier &&
            (step == Qt::Key_Insert || step == Qt::Key_Delete || step == Qt::Key_PageUp ||
             step == Qt::Key_PageDown)) {
            auto *focus = QApplication::focusWidget();
            bool editing = qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
                           qobject_cast<QPlainTextEdit *>(focus);
            if (!editing || (keyControl && keyControl->isAncestorOf(focus)) ||
                (tempoControl && tempoControl->isAncestorOf(focus))) {
                if (event->type() == QEvent::KeyPress) {
                    if (step == Qt::Key_Insert)
                        keyControl->stepUp();
                    else if (step == Qt::Key_Delete)
                        keyControl->stepDown();
                    else if (step == Qt::Key_PageUp)
                        tempoControl->stepUp();
                    else
                        tempoControl->stepDown();
                }
                event->accept();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::previewDatabase() {
    manageNcn();
}
void MainWindow::previewSettings() {
    lyricsSettings();
}
void MainWindow::lyricsSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("เนื้อเพลง / สีและการแสดงผล");
    dialog.resize(760, 650);
    auto candidate = lyricStyle;
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *family = new QFontComboBox;
    family->setCurrentFont(candidate.font);
    form->addRow("ฟอนต์เนื้อเพลง", family);
    auto *size = new QSpinBox;
    size->setRange(12, 160);
    size->setValue(candidate.font.pointSize());
    size->setSuffix(" pt");
    form->addRow("ขนาด", size);
    auto *bold = new QCheckBox("ตัวหนา");
    bold->setChecked(candidate.font.bold());
    form->addRow("น้ำหนัก", bold);
    auto *align = new QComboBox;
    align->addItems({"ด้านบน", "กึ่งกลาง", "ด้านล่าง"});
    align->setCurrentIndex(candidate.alignment);
    form->addRow("ตำแหน่ง", align);
    auto *margin = new QSpinBox;
    margin->setRange(0, 120);
    margin->setValue(candidate.margin);
    form->addRow("ระยะขอบ", margin);
    auto *gap = new QSpinBox;
    gap->setRange(0, 200);
    gap->setValue(candidate.lineGap);
    gap->setSuffix(" px");
    form->addRow("ระยะห่างเนื้อเพลงบน–ล่าง", gap);
    auto *performance = new QComboBox;
    performance->addItems({"พื้นฐาน • 30 FPS / 64 voices", "ขั้นกลาง • 60 FPS / 128 voices",
                           "สูงสุด • 120 FPS / 256 voices"});
    performance->setCurrentIndex(candidate.performance);
    form->addRow("การใช้ทรัพยากรคอม", performance);
    auto *offset = new QDoubleSpinBox;
    offset->setRange(-5, 5);
    offset->setSingleStep(.1);
    offset->setValue(candidate.offset);
    offset->setSuffix(" s");
    form->addRow("เลื่อนเวลาเนื้อเพลง (+ เร็วขึ้น)", offset);
    auto *surface = new QWidget;
    auto *previewLayout = new QVBoxLayout(surface);
    auto *preview = new LyricCanvas;
    preview->setSong({{0, "ทดสอบเนื้อเพลงบรรทัดบน\n"}, {2, "เนื้อเพลงบรรทัดล่าง"}}, 4);
    auto previewClock = std::make_shared<QElapsedTimer>();
    previewClock->start();
    preview->setClock([previewClock] { return std::fmod(previewClock->elapsed() / 1000., 4.); },
                      [] { return true; }, [] { return 1.; });
    previewLayout->addWidget(preview);
    surface->setMinimumHeight(200);
    layout->addWidget(surface, 1);
    auto update = [&] {
        candidate.font = family->currentFont();
        candidate.font.setPointSize(size->value());
        candidate.font.setBold(bold->isChecked());
        candidate.alignment = align->currentIndex();
        candidate.margin = margin->value();
        candidate.offset = offset->value();
        candidate.lineGap = gap->value();
        candidate.performance = performance->currentIndex();
        preview->setAppearance(candidate);
    };
    auto colorButton = [&](const QString &title, QColor &color) {
        auto *button = new QPushButton(color.name());
        form->addRow(title, button);
        auto paint = [button, &color] {
            double brightness =
                .2126 * color.redF() + .7152 * color.greenF() + .0722 * color.blueF();
            button->setText(color.name());
            button->setStyleSheet(
                "background:" + color.name() +
                ";color:" + (brightness > .55 ? QString("#000000") : QString("#ffffff")) + ";");
        };
        paint();
        connect(button, &QPushButton::clicked, &dialog, [&dialog, &color, paint, update] {
            auto chosen =
                QColorDialog::getColor(color, &dialog, "เลือกสี", QColorDialog::DontUseNativeDialog);
            if (chosen.isValid()) {
                color = chosen;
                paint();
                update();
            }
        });
    };
    colorButton("สีเนื้อเพลง", candidate.text);
    colorButton("สีไฮไลต์ที่ร้องแล้ว", candidate.sung);
    colorButton("สีพื้นหลังจอร้อง", candidate.background);
    connect(family, &QFontComboBox::currentFontChanged, &dialog,
            [update](const QFont &) { update(); });
    connect(size, qOverload<int>(&QSpinBox::valueChanged), &dialog, [update](int) { update(); });
    connect(bold, &QCheckBox::toggled, &dialog, [update](bool) { update(); });
    connect(align, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [update](int) { update(); });
    connect(margin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [update](int) { update(); });
    connect(gap, qOverload<int>(&QSpinBox::valueChanged), &dialog, [update](int) { update(); });
    connect(performance, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [update](int) { update(); });
    connect(offset, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
            [update](double) { update(); });
    auto *note = new QLabel("หน้าตั้งค่าใช้ธีมมืด; ปุ่มตัวอย่างสีอ่อนใช้ตัวอักษรสีดำเพื่อให้อ่านได้ชัด");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() == QDialog::Accepted) {
        lyricStyle = candidate;
        savePreferences();
        applyPerformance();
        if (lyrics)
            lyrics->setAppearance(lyricStyle);
    }
}
void MainWindow::showStage() {
    if (!stage) {
        stage = new QWidget;
        stage->setWindowTitle("BT Karaoke • Stage");

        auto *l = new QVBoxLayout(stage);
        auto *title = new QLabel("BT KARAOKE  •  LIVE");
        title->setStyleSheet("font-size:18px;color:#a798ff;");
        title->setAlignment(Qt::AlignCenter);
        l->addWidget(title);
        lyrics = new LyricCanvas;
        lyrics->setClock([this] { return engine.position.load(); },
                         [this] { return engine.playing.load(); },
                         [this] { return engine.playbackSpeed.load(); });
        syncLyrics();
        lyrics->setAppearance(lyricStyle);
        l->addWidget(lyrics, 1);
        auto *exit = new QPushButton("Exit fullscreen");
        l->addWidget(exit);
        connect(exit, &QPushButton::clicked, stage, [this] { stage->showNormal(); });
        stage->resize(1280, 720);
    }
    stage->show();
    if (QGuiApplication::screens().size() > 1) {
        stage->windowHandle()->setScreen(QGuiApplication::screens().at(1));
        stage->showFullScreen();
    } else
        stage->raise();
}
void MainWindow::refresh() {
    auto state = engine.audioState.load();
    if (audioAttempted && state != lastAudioState) {
        lastAudioState = state;
        if (state == Engine::AudioState::Ready) {
            status->setText("Audio ready • select a SoundFont and import songs");
            qInfo("BT_AUDIO_START ready");
        } else if (state == Engine::AudioState::Failed) {
            auto error = engine.audioError();
            status->setText(error + " • Click Audio to retry.");
            qWarning().noquote() << "BT_AUDIO_START failed:" << error;
        }
    }

    double pos = engine.position.load();
    clock->setText(timeText(pos) + " / " + timeText(engine.song.duration));
    if (!seek->isSliderDown())
        seek->setValue(engine.song.duration > 0 ? int(pos / engine.song.duration * 1000) : 0);
    cpu->setText(QString("DSP %1%  •  %2 FX").arg(engine.cpu.load(), 0, 'f', 0).arg([this] {
        int count = 0;
        std::lock_guard<std::mutex> lock(engine.mutex);
        for (auto &s : engine.strips)
            count += int(s.fx.size());
        return count;
    }()));
    if (pages->currentIndex() == 1)
        for (int i = 0; i < int(meters.size()); ++i) {
            float peak = engine.peaks[i].load();
            int value =
                peak > 0 ? int(std::clamp((20 * std::log10(peak) + 60) / 60., 0., 1.) * 100) : 0;
            if (meters[i])
                meters[i]->setValue(value);
            double volume, pan, send;
            int output;
            QString channelName;
            QStringList fxNames;
            bool mute, solo;
            {
                std::lock_guard<std::mutex> lock(engine.mutex);
                auto &s = engine.strips[i];
                volume = s.volume;
                pan = s.pan;
                mute = s.mute;
                solo = s.solo;
                send = s.fxSend;
                output = s.output;
                channelName = s.name;
                for (const auto &fx : s.fx)
                    fxNames << (fx->bypass ? "○ " : "● ") + fx->name();
            }
            if (selectButtons[i]) {
                selectButtons[i]->setText(QString(channelName).replace(" • ", "\n"));
                selectButtons[i]->setChecked(i == selected);
                selectButtons[i]->setToolTip(channelName + "\nRight-click to rename");
            }
            if (i == selected)
                chainTitle->setText(channelName + " / FX");
            if (fxSummaries[i]) {
                fxSummaries[i]->setText(fxNames.isEmpty()
                                            ? "No FX"
                                            : fxNames.mid(0, 3).join("\n") +
                                                  (fxNames.size() > 3
                                                       ? QString("\n+%1 FX").arg(fxNames.size() - 3)
                                                       : QString()));
                fxSummaries[i]->setToolTip(fxNames.join("\n"));
            }
            if (i < PartCount) {
                QSignalBlocker routeBlock(routeControls[i]), sendBlock(sendControls[i]);
                routeControls[i]->setCurrentIndex(routeControls[i]->findData(output));
                if (!sendControls[i]->isSliderDown())
                    sendControls[i]->setValue(int(send * 100));
            }
            if (volumeControls[i] && !volumeControls[i]->isSliderDown()) {
                QSignalBlocker block(volumeControls[i]);
                volumeControls[i]->setValue(int(volume * 100));
            }
            if (panControls[i] && !panControls[i]->isSliderDown()) {
                QSignalBlocker block(panControls[i]);
                panControls[i]->setValue(int(pan * 100));
            }
            if (muteButtons[i]) {
                QSignalBlocker block(muteButtons[i]);
                muteButtons[i]->setChecked(mute);
            }
            if (soloButtons[i]) {
                QSignalBlocker block(soloButtons[i]);
                soloButtons[i]->setChecked(solo);
            }
        }
    {
        QSignalBlocker b(melodyMute);
        melodyMute->setChecked(engine.melodyMuted());
    }
    if (wasPlaying && !engine.playing.load() && engine.song.duration > 0 &&
        pos >= engine.song.duration)
        next();
    wasPlaying = engine.playing.load();
}
} // namespace bt
