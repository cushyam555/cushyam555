#pragma once
#include "Effect.h"
#include <array>
#include <mutex>
#include <atomic>
#include <functional>
#include <QJsonObject>
namespace bt {
class Engine {
 struct Impl;std::unique_ptr<Impl>d;
public:
 static constexpr double Rate=48000;
 struct Strip {QString name;double volume=0.8,pan=0;bool mute=false,solo=false;std::vector<std::shared_ptr<Effect>> fx;};
 mutable std::mutex mutex;
 std::array<Strip,TrackCount> strips;
 std::array<std::atomic<float>,TrackCount> peaks{};
 std::atomic<double> position{0},cpu{0};std::atomic<bool> playing{false};std::atomic<int> droppedBlocks{0};
 Song song;
 Engine();~Engine();
 bool startAudio(QString &error);void stopAudio();
 void loadSoundfont(const QString&path);QString soundfontPath()const;
 void load(const Song&);void play();void pause();void stop();void seek(double seconds);void setSpeed(double);void setTranspose(int);
 void render(float*stereo,int frames);
 void testNote(int part,int note=60);
 QJsonObject save();QStringList restore(const QJsonObject&);
 std::shared_ptr<Effect> restoreEffect(const QJsonObject&);
};
}
