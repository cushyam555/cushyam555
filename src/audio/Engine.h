#pragma once
#include "Effect.h"
#include "MidiOutput.h"
#include <QJsonObject>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
namespace bt {
class Engine : public std::enable_shared_from_this<Engine> {
    struct Impl;
    std::unique_ptr<Impl> d;

  public:
    static constexpr double Rate = 48000;
    struct Strip {
        QString name;
        double volume = 0.8, pan = 0;
        bool mute = false, solo = false;
        int output = MasterPart;
        double fxSend = 0;
        std::vector<std::shared_ptr<Effect>> fx;
    };
    mutable std::mutex mutex;
    std::vector<Strip> strips;
    std::deque<std::atomic<float>> peaks;
    std::atomic<double> position{0}, cpu{0};
    std::atomic<bool> playing{false};
    std::atomic<int> droppedBlocks{0};
    Song song;
    enum class OutputMode { Internal, Midi, Both };
    void setOutputMode(OutputMode mode);
    bool openMidiOutput(int port, QString &error);
    void closeMidiOutput();
    void setMelody(bool muted, int channel);
    void panic();
    bool melodyMuted() const;
    int melodyChannel() const;
    void setMute(int part, bool mute);
    void setSolo(int part, bool solo);
    bool setRoute(int part, int output);
    int addBus();
    int trackCount() const {
        return int(strips.size());
    }
    QStringList presetNames() const;
    Engine();
    ~Engine();
    enum class AudioState { Offline, Starting, Ready, Failed };
    std::atomic<AudioState> audioState{AudioState::Offline};
    bool startAudio(QString &error);
    void startAudioAsync(std::function<bool(QString &)> initialize = {});
    QString audioError() const;
    void stopAudio();
    void loadSoundfont(const QString &path);
    QString soundfontPath() const;
    void load(const Song &);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setSpeed(double);
    void setTranspose(int);
    void render(float *stereo, int frames);
    void testNote(int part, int note = 60);
    QJsonObject save();
    QStringList restore(const QJsonObject &);
    std::shared_ptr<Effect> restoreEffect(const QJsonObject &);
};
} // namespace bt
