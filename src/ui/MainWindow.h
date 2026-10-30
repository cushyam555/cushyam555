#pragma once
#include "Appearance.h"
#include "LyricCanvas.h"
#include "audio/Engine.h"
#include "core/DatabaseScan.h"
#include "core/SongImport.h"
#include "core/SongLibrary.h"
#include <QHash>
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
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTimer;
namespace bt {
class MainWindow final : public QMainWindow {
    std::shared_ptr<Engine> engineOwner = std::make_shared<Engine>();
    Engine &engine = *engineOwner;
    Engine::AudioState lastAudioState = Engine::AudioState::Offline;
    bool audioAttempted = false;
    QProcess *audioProbe = nullptr;
    void enableAudio();
    QVector<Song> library;
    QHash<QString, int> libraryMap;
    QJsonArray plugins;
    int selected = 0;
    std::unique_ptr<SongLibrary> songStore;
    QString storageRoot;
    QStringList ncnRoots;
    QString ncnDatabaseRoot, karDatabaseRoot;
    LyricsStyle lyricStyle;
    bool refreshing = false;
    QString projectPath;
    QListWidget *songs = nullptr, *queue = nullptr, *chain = nullptr, *pluginList = nullptr;
    QLabel *now = nullptr, *clock = nullptr, *status = nullptr, *cpu = nullptr,
           *chainTitle = nullptr;
    LyricCanvas *lyrics = nullptr;
    QTimer *refreshTimer = nullptr;
    QSpinBox *keyControl = nullptr, *tempoControl = nullptr;
    QSlider *seek = nullptr;
    QStackedWidget *pages = nullptr;
    QWidget *stage = nullptr;
    std::vector<QProgressBar *> meters;
    std::vector<QPushButton *> selectButtons, muteButtons, soloButtons;
    std::vector<QSlider *> volumeControls, panControls;
    bool wasPlaying = false;
    std::array<QComboBox *, PartCount> routeControls{};
    std::array<QSlider *, PartCount> sendControls{};
    std::vector<QLabel *> fxSummaries;
    QCheckBox *melodyMute = nullptr;

    void importSongs();
    void importPaths(const QStringList &paths);
    void runImport(const QStringList &paths, bool copy);
    bool chooseStorage();
    void applyPerformance();
    void syncLyrics();
    bool eventFilter(QObject *, QEvent *) override;
    void manageNcn();
    bool rebuildSongDatabase(const QString &, const QString &, QProgressBar *, QLabel *, QWidget *);
    void reloadLibrary();
    bool rememberSong(Song song);
    void rebuildMixer();
    void lyricsSettings();
    void savePreferences();
    void midiSettings();
    void refreshLibrary(const QString &filter = {});
    bool loadRow(int);
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
    bool verifyMixerUi();
    bool verifyControlsUi();
    bool verifyImportUi();
    bool verifyDatabaseUi();
    void previewDatabase();
    bool startupSettled() const;
    void selectPage(int index);
    void previewSettings();
};
QString theme();
} // namespace bt
