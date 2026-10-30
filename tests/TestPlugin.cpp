// Original integration-test plugin; excluded from the application installer.
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
const FUID processorId(0x17125281, 0x22784451, 0x77721818, 0x57291511);
const FUID controllerId(0x17125282, 0x22784451, 0x77721818, 0x57291512);
class BTTestGain final : public AudioEffect {
    float gain = .5f;

  public:
    BTTestGain() {
        setControllerClass(controllerId);
    }
    static FUnknown *create(void *) {
        return static_cast<IAudioProcessor *>(new BTTestGain());
    }
    tresult PLUGIN_API initialize(FUnknown *context) override {
        auto r = AudioEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioInput(STR16("In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Out"), SpeakerArr::kStereo);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *s) override {
        return s->write(&gain, sizeof(gain), nullptr);
    }
    tresult PLUGIN_API setState(IBStream *s) override {
        return s->read(&gain, sizeof(gain), nullptr);
    }
    tresult PLUGIN_API process(ProcessData &d) override {
        if (d.inputParameterChanges)
            for (int p = 0; p < d.inputParameterChanges->getParameterCount(); ++p) {
                auto *q = d.inputParameterChanges->getParameterData(p);
                int32 sample = 0;
                ParamValue value;
                if (q && q->getParameterId() == 0 && q->getPointCount() > 0 &&
                    q->getPoint(q->getPointCount() - 1, sample, value) == kResultOk)
                    gain = float(value);
            }
        if (d.symbolicSampleSize != kSample32 || d.numInputs < 1 || d.numOutputs < 1)
            return kResultFalse;
        for (int c = 0; c < d.outputs[0].numChannels; ++c)
            for (int f = 0; f < d.numSamples; ++f)
                d.outputs[0].channelBuffers32[c][f] = d.inputs[0].channelBuffers32[c][f] * gain;
        return kResultOk;
    }
};
class BTTestController final : public EditController {
  public:
    static FUnknown *create(void *) {
        return static_cast<IEditController *>(new BTTestController());
    }
    tresult PLUGIN_API initialize(FUnknown *c) override {
        auto r = EditController::initialize(c);
        if (r == kResultOk)
            parameters.addParameter(STR16("Gain"), STR16(""), 0, .5, ParameterInfo::kCanAutomate,
                                    0);
        return r;
    }
    tresult PLUGIN_API setComponentState(IBStream *s) override {
        float value = .5f;
        auto r = s->read(&value, sizeof(value), nullptr);
        setParamNormalized(0, value);
        return r;
    }
};
bool InitModule() {
    return true;
}
bool DeinitModule() {
    return true;
}
BEGIN_FACTORY_DEF("BT Studio", "https://github.com/cushyam555/cushyam555", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(processorId), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "BT Integration Gain", 0, "Fx", "0.1.0", kVstVersionString, BTTestGain::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(controllerId), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "BT Integration Gain Controller", 0, "", "0.1.0",
           kVstVersionString, BTTestController::create)
END_FACTORY
