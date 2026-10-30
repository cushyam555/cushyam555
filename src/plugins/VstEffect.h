#pragma once
#include "audio/Effect.h"
#include <QJsonArray>
namespace bt {
QJsonArray scanVst3(const QString &path);
class VstEffect final : public Effect, public std::enable_shared_from_this<VstEffect> {
    struct Impl;
    std::unique_ptr<Impl> d;

  public:
    VstEffect(const QString &path, const QString &classId, double sampleRate);
    ~VstEffect() override;
    QString name() const override;
    void process(float *, int, const std::vector<MidiEvent> &, double, double) override;
    std::vector<Parameter> parameters() const override;
    void setParameter(int, double) override;
    QJsonObject save() const override;
    void restore(const QJsonObject &);
    void reset() override;
    bool showEditor() override;
};
} // namespace bt
