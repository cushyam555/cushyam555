#pragma once
#include "core/Midi.h"
#include <QJsonObject>
#include <memory>
#include <vector>
namespace bt {
struct Parameter {
    QString name;
    double value = 0.5;
};
class Effect {
  public:
    virtual ~Effect() = default;
    bool bypass = false;
    bool transportPlaying = false;
    virtual QString name() const = 0;
    virtual void process(float *stereo, int frames, const std::vector<MidiEvent> &events,
                         double bpm, double position) = 0;
    virtual std::vector<Parameter> parameters() const = 0;
    virtual void setParameter(int index, double value) = 0;
    virtual QJsonObject save() const = 0;
    virtual void reset() {}
    virtual bool showEditor() {
        return false;
    }
};
std::shared_ptr<Effect> builtin(const QString &type, double rate);
QStringList builtinNames();
} // namespace bt
