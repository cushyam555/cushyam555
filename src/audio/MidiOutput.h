#pragma once
#include "core/Midi.h"
#include <QStringList>
#include <memory>
namespace bt {
// Windows MIDI output is owned by a worker; driver calls never run in the audio callback.
class MidiOutput {
    struct Impl;
    std::unique_ptr<Impl> d;

  public:
    MidiOutput();
    ~MidiOutput();
    static QStringList ports();
    bool open(int port, QString &error);
    void close();
    void send(const MidiEvent &event);
    void panic();
};
} // namespace bt
