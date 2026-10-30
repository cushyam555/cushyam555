#include "MainWindow.h"
#include "plugins/VstEffect.h"
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtWidgets>
#include <algorithm>
#include <cmath>
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
QString registry() {
    auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/plugins.json";
}
void error(QWidget *w, const std::exception &e) {
    QMessageBox::warning(w, "BT Karaoke", QString::fromUtf8(e.what()));
}
} // namespace
MainWindow::MainWindow(bool offline) {
    setWindowTitle("BT Karaoke • Studio 0.1");
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
    search->setPlaceholderText("ค้นหาชื่อเพลง / ศิลปิน…");
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
        "＋ Import MIDI / KAR / NCN", libButtons, [this] { importSongs(); }, this)
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
    auto *hint = new QLabel("ลากเปลี่ยนคิวได้ • ดับเบิลคลิกเพื่อเล่น\nMIDI / KAR และ NCN companion "
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
                loadRow(songs->currentItem()->data(Qt::UserRole).toInt());
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
    key->setRange(-12, 12);
    controls->addWidget(key);
    connect(key, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) { engine.setTranspose(v); });
    controls->addWidget(new QLabel("TEMPO"));
    auto *tempo = new QSpinBox;
    tempo->setRange(50, 150);
    tempo->setValue(100);
    tempo->setSuffix("%");
    controls->addWidget(tempo);
    connect(tempo, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) { engine.setSpeed(v / 100.); });
    controls->addStretch();
    status = new QLabel("Add a SoundFont • import songs • build your FX chains");
    status->setObjectName("muted");
    controls->addWidget(status);
    rebuildChain();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(40);
    status->setWordWrap(true);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status->setMinimumWidth(100);
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
    return chain && meters[15] && centralWidget() && engine.strips[MasterPart].fx.size() == 1;
}
void MainWindow::importSongs() {
    auto paths =
        QFileDialog::getOpenFileNames(this, "Import MIDI / KAR (NCN: select files inside Song)", {},
                                      "MIDI / Karaoke (*.mid *.midi *.kar);;All files (*)");
    QStringList errors;
    for (auto &p : paths)
        try {
            library.push_back(readSong(p));
        } catch (const std::exception &e) {
            errors << QFileInfo(p).fileName() + ": " + e.what();
        }
    refreshLibrary();
    if (!errors.isEmpty())
        QMessageBox::warning(this, "Import report", errors.join('\n'));
}
void MainWindow::refreshLibrary(const QString &filter) {
    songs->clear();
    for (int i = 0; i < library.size(); ++i) {
        const auto &s = library[i];
        QString text = s.title + "  •  " + s.artist + "  /  " + timeText(s.duration);
        if (!filter.isEmpty() && !text.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(text, songs);
        item->setData(Qt::UserRole, i);
    }
}
void MainWindow::loadRow(int row) {
    if (row < 0 || row >= library.size())
        return;
    engine.load(library[row]);
    now->setText(library[row].title);
    if (engine.soundfontPath().isEmpty())
        status->setText("เลือก SoundFont เพื่อให้ MIDI มีเสียง หรือเพิ่ม VST3 instrument ในช่องมิกเซอร์");
}
void MainWindow::next() {
    if (!queue->count())
        return;
    auto *i = queue->takeItem(0);
    loadRow(i->data(Qt::UserRole).toInt());
    delete i;
    engine.play();
}
void MainWindow::mixerPage(QWidget *w) {
    auto *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *left = new QVBoxLayout;
    layout->addLayout(left, 3);
    auto *head = new QHBoxLayout;
    left->addLayout(head);
    auto *t = new QLabel("MIXER / INSTRUMENT ROUTING");
    t->setObjectName("heading");
    head->addWidget(t);
    auto *filter = new QComboBox;
    filter->addItems({"All channels", "Drums only", "Instruments only"});
    head->addWidget(filter);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    left->addWidget(scroll, 1);
    auto *strips = new QWidget;
    strips->setObjectName("mixerSurface");
    auto *row = new QHBoxLayout(strips);
    std::array<QWidget *, TrackCount> widgets{};
    for (int p = 0; p < TrackCount; ++p) {
        auto *strip = new QWidget;
        strip->setObjectName("panel");
        strip->setFixedWidth(142);
        widgets[p] = strip;
        row->addWidget(strip);
        auto *v = new QVBoxLayout(strip);
        auto *select = new QPushButton(
            p < 15 ? QString("Instrument %1\nMIDI %2").arg(p + 1).arg(p < 9 ? p + 1 : p + 2)
                   : QString(partName(p)).replace(" • ", "\n"));
        select->setCheckable(true);
        select->setChecked(p == selected);
        select->setMinimumHeight(54);
        selectButtons[p] = select;
        v->addWidget(select);
        connect(select, &QPushButton::clicked, this, [this, p] {
            selected = p;
            for (int i = 0; i < TrackCount; ++i)
                selectButtons[i]->setChecked(i == p);
            rebuildChain();
        });
        auto *tag = new QLabel(p == MasterPart ? "BUS / STEREO"
                               : p >= 15       ? "DRUM STEM"
                                               : "MIDI INSTRUMENT");
        tag->setObjectName("muted");
        tag->setAlignment(Qt::AlignCenter);
        v->addWidget(tag);
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
        connect(m, &QPushButton::toggled, this, [this, p](bool v) {
            std::lock_guard<std::mutex> lock(engine.mutex);
            engine.strips[p].mute = v;
        });
        connect(s, &QPushButton::toggled, this, [this, p](bool v) {
            std::lock_guard<std::mutex> lock(engine.mutex);
            engine.strips[p].solo = v;
        });
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
        auto *add = new QPushButton("＋ FX");
        v->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this, p] {
            selected = p;
            rebuildChain();
            addEffect();
        });
    }
    row->addStretch();
    scroll->setWidget(strips);
    connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), this, [widgets](int f) {
        for (int i = 0; i < TrackCount; ++i)
            widgets[i]->setVisible(f == 0 || i == MasterPart || (f == 1 ? i >= 15 : i < 15));
    });
    filter->setCurrentIndex(1);
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
        auto warnings = engine.restore(obj);
        library.clear();
        QVector<int> mapping;
        for (auto v : obj["library"].toArray()) {
            try {
                auto s = readSong(v.toString());
                mapping.push_back(library.size());
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
        now->setText(engine.song.title);
        projectPath = path;
        rebuildChain();
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
void MainWindow::showStage() {
    if (!stage) {
        stage = new QWidget;
        stage->setWindowTitle("BT Karaoke • Stage");
        stage->setStyleSheet("background:#090d18;color:white;");
        auto *l = new QVBoxLayout(stage);
        auto *title = new QLabel("BT KARAOKE  •  LIVE");
        title->setStyleSheet("font-size:18px;color:#a798ff;");
        title->setAlignment(Qt::AlignCenter);
        l->addWidget(title);
        lyrics = new QLabel("Ready when you are");
        lyrics->setWordWrap(true);
        lyrics->setAlignment(Qt::AlignCenter);
        lyrics->setStyleSheet("font-size:48px;font-weight:700;padding:50px;");
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
    for (int i = 0; i < TrackCount; ++i) {
        float peak = engine.peaks[i].load();
        int value =
            peak > 0 ? int(std::clamp((20 * std::log10(peak) + 60) / 60., 0., 1.) * 100) : 0;
        if (meters[i])
            meters[i]->setValue(value);
        double volume, pan;
        bool mute, solo;
        {
            std::lock_guard<std::mutex> lock(engine.mutex);
            auto &s = engine.strips[i];
            volume = s.volume;
            pan = s.pan;
            mute = s.mute;
            solo = s.solo;
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
    if (wasPlaying && !engine.playing.load() && engine.song.duration > 0 &&
        pos >= engine.song.duration)
        next();
    wasPlaying = engine.playing.load();
    if (lyrics) {
        QString text;
        int cursor = 0;
        for (const auto &e : engine.song.lyrics) {
            QString part = e.text;
            part.replace('\\', "\n");
            part.replace('/', "\n");
            part.replace("\r\n", "\n");
            part.replace('\r', "\n");
            text += part;
            if (e.time <= pos)
                cursor = text.size();
        }
        int start = text.lastIndexOf('\n', std::max(0, cursor - 1));
        start = start < 0 ? 0 : start + 1;
        int end = text.indexOf('\n', cursor);
        if (end < 0)
            end = text.size();
        int next = text.indexOf('\n', end + 1);
        if (next < 0)
            next = text.size();
        QString before = text.mid(start, cursor - start), after = text.mid(cursor, next - cursor);
        if (text.isEmpty())
            lyrics->setText("BT Karaoke • Ready for lyrics");
        else
            lyrics->setText("<span style='color:#aa9bff'>" +
                            before.toHtmlEscaped().replace("\n", "<br>") +
                            "</span><span style='color:#f4f5ff'>" +
                            after.toHtmlEscaped().replace("\n", "<br>") + "</span>");
    }
}
} // namespace bt
