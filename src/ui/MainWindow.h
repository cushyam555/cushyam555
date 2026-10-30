#pragma once
#include "audio/Engine.h"
#include <QJsonArray>
#include <QMainWindow>
class QListWidget;
class QLabel;
class QPushButton;
class QProgressBar;
class QSlider;
class QStackedWidget;
class QLineEdit;
class QProcess;
namespace bt {
class MainWindow final : public QMainWindow {
    std::shared_ptr<Engine> engineOwner = std::make_shared<Engine>();
    Engine &engine = *engineOwner;
    Engine::AudioState lastAudioState = Engine::AudioState::Offline;
    bool audioAttempted = false;
    QProcess *audioProbe = nullptr;
    void enableAudio();
    QVector<Song> library;
    QJsonArray plugins;
    int selected = 15;
    bool refreshing = false;
    QString projectPath;
    QListWidget *songs = nullptr, *queue = nullptr, *chain = nullptr, *pluginList = nullptr;
    QLabel *now = nullptr, *clock = nullptr, *status = nullptr, *cpu = nullptr,
           *chainTitle = nullptr, *lyrics = nullptr;
    QSlider *seek = nullptr;
    QStackedWidget *pages = nullptr;
    QWidget *stage = nullptr;
    std::array<QProgressBar *, TrackCount> meters{};
    std::array<QPushButton *, TrackCount> selectButtons{}, muteButtons{}, soloButtons{};
    std::array<QSlider *, TrackCount> volumeControls{}, panControls{};
    bool wasPlaying = false;
    void importSongs();
    void refreshLibrary(const QString &filter = {});
    void loadRow(int);
    void next();
    void mixerPage(QWidget *);
    void rebuildChain();
    void addEffect();
    void editEffect();
    void scanPlugins();
    void saveProject();
    void openProject();
    void refresh();
    void saveChain();
    void loadChain();
    void showStage();
    void writePlugins();

  public:
    explicit MainWindow(bool offline = false);
    ~MainWindow() override;
    bool smokeReady() const;
    bool startupSettled() const;
    void selectPage(int index);
};
QString theme();
} // namespace bt
