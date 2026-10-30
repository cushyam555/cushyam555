#include "VstEffect.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace bt {
namespace {
class BtHost final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 text) override {
        std::memset(text, 0, sizeof(String128));
        const char16_t name[] = u"BT Karaoke";
        std::memcpy(text, name, sizeof(name));
        return kResultOk;
    }
};
HostApplication &host() {
    static IPtr<BtHost> h = owned(new BtHost());
    return *h;
}
QString fromString(const TChar *s) {
    return QString::fromUtf16(reinterpret_cast<const ushort *>(s));
}
class Handler final : public IComponentHandler {
    std::atomic<uint32> refs{1};

  public:
    std::function<void(ParamID, ParamValue)> edit;
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *out = static_cast<IComponentHandler *>(this);
            addRef();
            return kResultOk;
        }
        *out = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return ++refs;
    }
    uint32 PLUGIN_API release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    tresult PLUGIN_API beginEdit(ParamID) override {
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override {
        if (edit)
            edit(id, v);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override {
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32) override {
        return kNotImplemented;
    }
};
class EditorWindow final : public QDialog, public IPlugFrame {
    std::shared_ptr<VstEffect> keep;
    IPtr<IPlugView> view;
    std::atomic<uint32> refs{1};
    bool attached = false;

  public:
    EditorWindow(std::shared_ptr<VstEffect> effect, IPtr<IPlugView> v)
        : QDialog(QApplication::activeWindow()), keep(std::move(effect)), view(std::move(v)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(keep->name());
        setAttribute(Qt::WA_NativeWindow);
        ViewRect rect{};
        view->getSize(&rect);
        setFixedSize(std::max(240, rect.right - rect.left), std::max(120, rect.bottom - rect.top));
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **out) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *out = static_cast<IPlugFrame *>(this);
            addRef();
            return kResultOk;
        }
        *out = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return ++refs;
    }
    uint32 PLUGIN_API release() override {
        return --refs;
    }
    tresult PLUGIN_API resizeView(IPlugView *v, ViewRect *r) override {
        if (v != view.get() || !r || r->right <= r->left || r->bottom <= r->top)
            return kInvalidArgument;
        setFixedSize(r->right - r->left, r->bottom - r->top);
        return view->onSize(r);
    }
    bool attach() {
#ifdef _WIN32
        view->setFrame(this);
        attached =
            view->attached(reinterpret_cast<void *>(winId()), kPlatformTypeHWND) == kResultOk;
        return attached;
#else
        return false;
#endif
    }
    ~EditorWindow() override {
        if (attached)
            view->removed();
        view->setFrame(nullptr);
    }
};
} // namespace
QJsonArray scanVst3(const QString &path) {
    std::string error;
    auto module = VST3::Hosting::Module::create(path.toStdString(), error);
    if (!module)
        throw std::runtime_error(error);
    QJsonArray results;
    for (const auto &c : module->getFactory().classInfos())
        if (c.category() == kVstAudioEffectClass)
            results.append(
                QJsonObject{{"path", path},
                            {"id", QString::fromStdString(c.ID().toString())},
                            {"name", QString::fromStdString(c.name())},
                            {"vendor", QString::fromStdString(c.vendor())},
                            {"category", QString::fromStdString(c.subCategoriesString())}});
    return results;
}
struct VstEffect::Impl {
    QString path, id, title;
    double rate;
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    IPtr<IAudioProcessor> processor;
    IPtr<Handler> handler;
    std::array<float, 1024> dry{};
    HostProcessData data;
    ProcessContext context{};
    EventList events{256};
    ParameterChanges changes{1024}, outputs{1024};
    std::vector<ParameterInfo> info;
    std::vector<double> pending;
    std::vector<bool> dirty;
    std::mutex mutex;
    Impl(QString p, QString uid, double sr) : path(std::move(p)), id(std::move(uid)), rate(sr) {
        std::string error;
        module = VST3::Hosting::Module::create(path.toStdString(), error);
        if (!module)
            throw std::runtime_error(error);
        auto factory = module->getFactory();
        factory.setHostContext(&host());
        PluginContextFactory::instance().setPluginContext(&host());
        bool found = false;
        for (const auto &c : factory.classInfos())
            if (QString::fromStdString(c.ID().toString()) == id &&
                c.category() == kVstAudioEffectClass) {
                provider = owned(new PlugProvider(factory, c, true));
                title = QString::fromStdString(c.name());
                found = true;
                break;
            }
        if (!found || !provider->initialize())
            throw std::runtime_error("VST3 initialization failed");
        component = provider->getComponentPtr();
        controller = provider->getControllerPtr();
        processor = FUnknownPtr<IAudioProcessor>(component);
        if (!processor || processor->canProcessSampleSize(kSample32) != kResultTrue)
            throw std::runtime_error("VST3 must support float32 processing");
        int in = component->getBusCount(kAudio, kInput),
            out = component->getBusCount(kAudio, kOutput);
        if (out < 1)
            throw std::runtime_error("VST3 has no audio output");
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        if (in > 0)
            processor->setBusArrangements(&stereo, 1, &stereo, 1);
        else
            processor->setBusArrangements(nullptr, 0, &stereo, 1);
        BusInfo bus{};
        component->getBusInfo(kAudio, kOutput, 0, bus);
        if (bus.channelCount < 1 || bus.channelCount > 2)
            throw std::runtime_error("Only mono/stereo VST3 outputs are supported");
        for (auto dir : {kInput, kOutput})
            for (int i = 0; i < component->getBusCount(kAudio, dir); ++i)
                component->activateBus(kAudio, dir, i, i == 0);
        for (int i = 0; i < component->getBusCount(kEvent, kInput); ++i)
            component->activateBus(kEvent, kInput, i, i == 0);
        ProcessSetup setup{kRealtime, kSample32, 512, sr};
        if (processor->setupProcessing(setup) != kResultOk)
            throw std::runtime_error("VST3 processing setup rejected");
        if (!data.prepare(*component, 512, kSample32))
            throw std::runtime_error("VST3 audio buffer allocation failed");
        if (controller) {
            for (int i = 0; i < controller->getParameterCount(); ++i) {
                ParameterInfo param{};
                if (controller->getParameterInfo(i, param) == kResultOk) {
                    info.push_back(param);
                    pending.push_back(controller->getParamNormalized(param.id));
                    dirty.push_back(false);
                }
            }
            handler = owned(new Handler());
            handler->edit = [this](ParamID pid, double v) {
                std::lock_guard<std::mutex> lock(mutex);
                for (size_t i = 0; i < info.size(); ++i)
                    if (info[i].id == pid) {
                        pending[i] = v;
                        dirty[i] = true;
                        break;
                    }
            };
            controller->setComponentHandler(handler);
        }
        changes.setMaxParameters(int32(info.size()));
        context.sampleRate = sr;
        context.tempo = 120;
        context.timeSigNumerator = 4;
        context.timeSigDenominator = 4;
        component->setActive(true);
        processor->setProcessing(true);
    }
    ~Impl() {
        if (controller)
            controller->setComponentHandler(nullptr);
        if (processor)
            processor->setProcessing(false);
        if (component)
            component->setActive(false);
        data.unprepare();
    }
};
VstEffect::VstEffect(const QString &p, const QString &i, double sr) : d(new Impl(p, i, sr)) {}
VstEffect::~VstEffect() = default;
QString VstEffect::name() const {
    return d->title;
}
std::vector<Parameter> VstEffect::parameters() const {
    std::vector<Parameter> r;
    if (d->controller)
        for (const auto &i : d->info)
            r.push_back({fromString(i.title), d->controller->getParamNormalized(i.id)});
    return r;
}
void VstEffect::setParameter(int index, double v) {
    if (index < 0 || index >= int(d->info.size()))
        return;
    v = std::clamp(v, 0., 1.);
    d->controller->setParamNormalized(d->info[size_t(index)].id, v);
    std::lock_guard<std::mutex> lock(d->mutex);
    d->pending[size_t(index)] = v;
    d->dirty[size_t(index)] = true;
}
void VstEffect::process(float *b, int frames, const std::vector<MidiEvent> &midi, double bpm,
                        double position) {
    auto &x = *d;
    if (bypass)
        std::copy_n(b, frames * 2, x.dry.data());
    x.events.clear();
    x.changes.clearQueue();
    x.outputs.clearQueue();
    if (x.mutex.try_lock()) {
        for (size_t i = 0; i < x.info.size(); ++i)
            if (x.dirty[i]) {
                int32 index = 0;
                auto *q = x.changes.addParameterData(x.info[i].id, index);
                if (q)
                    q->addPoint(0, x.pending[i], index);
                x.dirty[i] = false;
            }
        x.mutex.unlock();
    }
    for (const auto &m : midi) {
        if (x.component->getBusCount(kEvent, kInput) == 0)
            break;
        int kind = m.status & 0xf0;
        if (kind != 0x90 && kind != 0x80)
            continue;
        Event e{};
        e.busIndex = 0;
        e.sampleOffset = 0;
        e.ppqPosition = position * bpm / 60.;
        if (kind == 0x90 && m.data2) {
            e.type = Event::kNoteOnEvent;
            e.noteOn.channel = 0;
            e.noteOn.pitch = int16(m.data1);
            e.noteOn.velocity = m.data2 / 127.f;
            e.noteOn.noteId = -1;
        } else {
            e.type = Event::kNoteOffEvent;
            e.noteOff.channel = 0;
            e.noteOff.pitch = int16(m.data1);
            e.noteOff.velocity = 0;
            e.noteOff.noteId = -1;
        }
        x.events.addEvent(e);
    }
    for (int bus = 0; bus < x.data.numInputs; ++bus) {
        auto &in = x.data.inputs[bus];
        in.silenceFlags = 0;
        for (int c = 0; c < in.numChannels; ++c)
            for (int f = 0; f < frames; ++f)
                in.channelBuffers32[c][f] =
                    bus == 0 ? (in.numChannels == 1 ? (b[2 * f] + b[2 * f + 1]) * .5f
                                                    : b[2 * f + std::min(c, 1)])
                             : 0;
    }
    for (int bus = 0; bus < x.data.numOutputs; ++bus) {
        auto &out = x.data.outputs[bus];
        out.silenceFlags = 0;
        for (int c = 0; c < out.numChannels; ++c)
            std::fill_n(out.channelBuffers32[c], frames, 0);
    }
    x.data.numSamples = frames;
    x.data.processMode = kRealtime;
    x.data.symbolicSampleSize = kSample32;
    x.data.inputEvents = &x.events;
    x.data.inputParameterChanges = &x.changes;
    x.data.outputParameterChanges = &x.outputs;
    x.context.tempo = bpm;
    x.context.projectTimeSamples = int64(position * x.rate);
    x.context.projectTimeMusic = position * bpm / 60.;
    x.context.state = (transportPlaying ? ProcessContext::kPlaying : 0) |
                      ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid |
                      ProcessContext::kTimeSigValid;
    x.data.processContext = &x.context;
    if (x.processor->process(x.data) != kResultOk)
        return;
    auto &out = x.data.outputs[0];
    for (int f = 0; f < frames; ++f) {
        b[2 * f] = out.channelBuffers32[0][f];
        b[2 * f + 1] = out.channelBuffers32[std::min(1, out.numChannels - 1)][f];
    }
    if (bypass)
        std::copy_n(x.dry.data(), frames * 2, b);
}
QJsonObject VstEffect::save() const {
    QJsonObject r{
        {"type", "VST3"}, {"path", d->path}, {"id", d->id}, {"name", d->title}, {"bypass", bypass}};
    QJsonArray values;
    for (auto p : parameters())
        values.append(p.value);
    r["parameters"] = values;
    MemoryStream stream;
    if (d->component->getState(&stream) == kResultOk)
        r["state"] =
            QString::fromLatin1(QByteArray(stream.getData(), int(stream.getSize())).toBase64());
    return r;
}
void VstEffect::restore(const QJsonObject &r) {
    auto bytes = QByteArray::fromBase64(r["state"].toString().toLatin1());
    if (!bytes.isEmpty()) {
        MemoryStream stream(bytes.data(), bytes.size());
        d->component->setState(&stream);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        if (d->controller)
            d->controller->setComponentState(&stream);
    }
    auto values = r["parameters"].toArray();
    for (int i = 0; i < values.size(); ++i)
        setParameter(i, values[i].toDouble());
}

void VstEffect::reset() {
    if (d->processor) {
        d->processor->setProcessing(false);
        d->processor->setProcessing(true);
    }
}
bool VstEffect::showEditor() {
#ifdef _WIN32
    if (!d->controller)
        return false;
    auto v = owned(d->controller->createView(ViewType::kEditor));
    if (!v || v->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue)
        return false;
    auto *w = new EditorWindow(shared_from_this(), v);
    if (!w->attach()) {
        delete w;
        return false;
    }
    w->show();
    return true;
#else
    return false;
#endif
}
} // namespace bt
