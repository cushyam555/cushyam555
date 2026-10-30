#include "Engine.h"
#include "plugins/VstEffect.h"
#include <QFile>
#include <QJsonArray>
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#define TSF_IMPLEMENTATION
#include <tsf.h>
namespace bt {
struct Engine::Impl {
    Engine *owner = nullptr;
    MidiOutput midi;
    OutputMode outputMode = OutputMode::Internal;
    bool melodyOff = false;
    int melody = 3;
    std::vector<std::array<float, 512>> buses;
    std::array<int, 16> programs{}, pitch{};
    std::array<std::array<int, 128>, 16> controllers{};
    std::array<std::bitset<PartCount>, 16> used;
    std::array<std::array<std::vector<int>, 128>, 16> held;
    ma_device device{};
    bool initialized = false;
    std::mutex deviceMutex, errorMutex;
    std::atomic<bool> closing{false};
    QString lastAudioError;
    QString sfPath;
    QStringList presetLabels;
    tsf *base = nullptr;
    std::array<tsf *, PartCount> synth{};
    size_t cursor = 0;
    double speed = 1;
    int transpose = 0;
    std::array<float, 1024> scratch{};
    std::array<std::vector<MidiEvent>, PartCount> pending;
    std::vector<MidiEvent> empty;
    struct Audition {
        int part, note;
        double end;
    };
    std::vector<Audition> auditions;
    double wall = 0;
    bool suspended = false;
    Impl() {
        for (auto &p : pending)
            p.reserve(256);
        auditions.reserve(128);
    }
    ~Impl() {
        for (auto *s : synth)
            if (s)
                tsf_close(s);
        if (base)
            tsf_close(base);
    }
    void panic() {
        midi.panic();
        for (int p = 0; p < PartCount; ++p) {
            if (synth[p])
                for (int ch = 0; ch < 16; ++ch)
                    tsf_channel_sounds_off_all(synth[p], ch);
            pending[p].clear();
        }
        for (int ch = 0; ch < 16; ++ch)
            for (int note = 0; note < 128; ++note) {
                for (int p : held[ch][note])
                    pending[p].push_back(
                        {0, 0, 0x80 | ch, std::clamp(note + (ch == 9 ? 0 : transpose), 0, 127), 0});
                held[ch][note].clear();
            }
        for (const auto &a : auditions)
            pending[a.part].push_back(
                {0, 0, 0x80 | (a.part >= DrumFirst ? 9 : 0),
                 std::clamp(a.note + (a.part >= DrumFirst ? 0 : transpose), 0, 127), 0});
        auditions.clear();
    }
    void send(int p, const MidiEvent &e) {
        if (p < 0 || p >= PartCount)
            return;
        auto event = e;
        int kind = e.status & 240;
        int channel = e.status & 15;
        if (p < DrumFirst && (kind == 0x80 || kind == 0x90))
            event.data1 = std::clamp(e.data1 + transpose, 0, 127);
        pending[p].push_back(event);
        tsf *s = synth[p];
        if (!s)
            return;
        if (kind == 0x90 && e.data2) {
            if (tsf_channel_set_presetnumber(s, channel, p < DrumFirst ? p : programs[channel],
                                             p >= DrumFirst))
                tsf_channel_note_on(s, channel, event.data1, e.data2 / 127.f);
        } else if (kind == 0x80 || kind == 0x90)
            tsf_channel_note_off(s, channel, event.data1);
        else if (kind == 0xc0)
            tsf_channel_set_presetnumber(s, channel, p < DrumFirst ? p : e.data1, p >= DrumFirst);
        else if (kind == 0xb0)
            tsf_channel_midi_control(s, channel, e.data1, e.data2);
        else if (kind == 0xe0)
            tsf_channel_set_pitchwheel(s, channel, e.data1 | (e.data2 << 7));
    }
    void resetChannels() {
        programs.fill(0);
        pitch.fill(8192);
        for (auto &cc : controllers) {
            cc.fill(0);
            cc[7] = 100;
            cc[10] = 64;
            cc[11] = 127;
        }
        for (auto &u : used)
            u.reset();
        for (auto *sampler : synth)
            if (sampler) {
                for (int ch = 0; ch < 16; ++ch) {
                    tsf_channel_midi_control(sampler, ch, 121, 0);
                    tsf_channel_set_bank(sampler, ch, 0);
                    tsf_channel_set_pitchwheel(sampler, ch, 8192);
                }
            }
    }
    void event(const MidiEvent &e) {
        int channel = e.status & 15, kind = e.status & 240;
        bool noteOn = kind == 0x90 && e.data2;
        bool noteOff = kind == 0x80 || (kind == 0x90 && !e.data2);
        int part = channel == 9 ? partFor(channel, e.data1) : programs[channel];
        if (kind == 0xc0) {
            programs[channel] = e.data1;
            part = channel == 9 ? DrumFirst : e.data1;
        }
        if (kind == 0xb0) {
            if (e.data1 == 121) {
                controllers[channel].fill(0);
                controllers[channel][7] = 100;
                controllers[channel][10] = 64;
                controllers[channel][11] = 127;
            }
            controllers[channel][e.data1] = e.data2;
        }
        if (kind == 0xe0)
            pitch[channel] = e.data1 | (e.data2 << 7);
        bool anySolo = false, busSolo = false;
        for (int p = 0; p < PartCount; ++p)
            anySolo |= owner->strips[p].solo;
        for (int b = BusFirst; b < owner->trackCount(); ++b)
            busSolo |= owner->strips[b].solo;
        auto &strip = owner->strips[part];
        bool muted = strip.mute || (anySolo && !strip.solo) || owner->strips[MasterPart].mute;
        if (strip.output >= BusFirst && strip.output < owner->trackCount())
            muted |=
                owner->strips[strip.output].mute || (busSolo && !owner->strips[strip.output].solo);
        else
            muted |= busSolo;
        bool blocked = melodyOff && channel == melody;
        if (outputMode != OutputMode::Internal && !(noteOn && (muted || blocked))) {
            auto event = e;
            if (channel != 9 && (noteOn || noteOff))
                event.data1 = std::clamp(e.data1 + transpose, 0, 127);
            midi.send(event);
        }
        if (noteOn) {
            if (blocked)
                return;
            if (channel == 9 && (e.data1 == 42 || e.data1 == 44)) {
                int open = partFor(9, 46);
                send(open, {e.time, e.tick, 0x89, 46, 0});
                if (synth[open])
                    tsf_channel_sounds_off_all(synth[open], 9);
            }
            used[channel].set(part);
            held[channel][e.data1].push_back(part);
            if (auto *sampler = synth[part]) {
                // Each program strip keeps independent MIDI channel controllers/voices.
                for (int cc : {0, 32, 7, 10, 11, 64})
                    tsf_channel_midi_control(sampler, channel, cc, controllers[channel][cc]);
                tsf_channel_set_presetnumber(sampler, channel, programs[channel], channel == 9);
                tsf_channel_set_pitchwheel(sampler, channel, pitch[channel]);
            }
            send(part, e);
        } else if (noteOff) {
            // A program change must never send an old note-off into the new program.
            auto &parts = held[channel][e.data1];
            if (!parts.empty()) {
                send(parts.front(), e);
                parts.erase(parts.begin());
            }
        } else {
            if (kind == 0xb0 && (e.data1 == 120 || e.data1 == 123)) {
                for (int note = 0; note < 128; ++note) {
                    for (int p : held[channel][note])
                        send(p, {e.time, e.tick, 0x80 | channel, note, 0});
                    held[channel][note].clear();
                }
            }
            if (channel == 9) {
                for (int p = DrumFirst; p < PartCount; ++p)
                    send(p, e);
            } else {
                used[channel].set(part);
                for (int p = 0; p < ProgramCount; ++p)
                    if (used[channel].test(p)) {
                        if (kind != 0xc0 || p == part)
                            send(p, e);
                    }
            }
        }
    }
};
Engine::Engine() : d(new Impl), strips(TrackCount), peaks(TrackCount) {
    d->buses.resize(BusCount);
    d->resetChannels();
    d->owner = this;
    for (int i = 0; i < TrackCount; ++i)
        strips[i].name = partName(i);
    for (int b = BusFirst; b < trackCount(); ++b)
        strips[b].volume = 1;
    for (int p = DrumFirst; p < PartCount; ++p)
        strips[p].output = BusFirst;
    strips[MasterPart].volume = .8;
    strips[MasterPart].fx.push_back(builtin("Limiter", Rate));
}
Engine::~Engine() {
    stopAudio();
}
bool Engine::startAudio(QString &error) {
    std::lock_guard<std::mutex> deviceLock(d->deviceMutex);
    if (d->closing) {
        error = "Audio startup was cancelled";
        return false;
    }
    if (d->initialized)
        return true;
    auto config = ma_device_config_init(ma_device_type_playback);
    config.sampleRate = unsigned(Rate);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.periodSizeInFrames = 256;
    config.pUserData = this;
    config.dataCallback = [](ma_device *dev, void *out, const void *, ma_uint32 count) {
        static_cast<Engine *>(dev->pUserData)->render(static_cast<float *>(out), int(count));
    };
#ifdef _WIN32
    const ma_backend backends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm};
    auto result = ma_device_init_ex(backends, 3, nullptr, &config, &d->device);
#else
    auto result = ma_device_init(nullptr, &config, &d->device);
#endif
    if (result != MA_SUCCESS) {
        error = QString("Audio output unavailable (%1). You can still edit and test offline.")
                    .arg(result);
        return false;
    }
    d->initialized = true;
    if (d->closing) {
        ma_device_uninit(&d->device);
        d->initialized = false;
        return false;
    }
    if (ma_device_start(&d->device) != MA_SUCCESS) {
        ma_device_uninit(&d->device);
        d->initialized = false;
        error = "Cannot start audio output";
        return false;
    }
    return true;
}
void Engine::startAudioAsync(std::function<bool(QString &)> initialize) {
    auto state = audioState.load();
    if (state == AudioState::Starting || state == AudioState::Ready)
        return;
    auto self = shared_from_this();
    d->closing = false;
    audioState = AudioState::Starting;
    try {
        std::thread([self, initialize = std::move(initialize)] {
            QString error;
            bool ready = false;
            try {
                ready = initialize ? initialize(error) : self->startAudio(error);
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
            {
                std::lock_guard<std::mutex> lock(self->d->errorMutex);
                self->d->lastAudioError = error;
            }
            self->audioState = self->d->closing ? AudioState::Offline
                               : ready          ? AudioState::Ready
                                                : AudioState::Failed;
            // Keep the engine alive until initialization finishes, even if its window closed.
        }).detach();
    } catch (const std::exception &e) {
        {
            std::lock_guard<std::mutex> lock(d->errorMutex);
            d->lastAudioError = QString::fromUtf8(e.what());
        }
        audioState = AudioState::Failed;
    }
}
QString Engine::audioError() const {
    std::lock_guard<std::mutex> lock(d->errorMutex);
    return d->lastAudioError;
}
void Engine::stopAudio() {
    d->closing = true;
    // A driver may be stuck in initialization. Closing the UI must not wait for it.
    std::unique_lock<std::mutex> lock(d->deviceMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    if (d->initialized) {
        ma_device_uninit(&d->device);
        d->initialized = false;
    }
    audioState = AudioState::Offline;
}

void Engine::loadSoundfont(const QString &path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly) || file.size() > 0x7fffffff)
        throw std::runtime_error("Cannot read SoundFont");
    auto bytes = file.readAll();
    tsf *font = tsf_load_memory(bytes.constData(), int(bytes.size()));
    if (!font)
        throw std::runtime_error("Cannot load SoundFont (.sf2)");
    tsf_set_output(font, TSF_STEREO_INTERLEAVED, int(Rate), -8);
    std::array<tsf *, PartCount> copies{};
    for (int i = 0; i < PartCount; ++i) {
        copies[i] = tsf_copy(font);
        if (!copies[i]) {
            for (auto *s : copies)
                if (s)
                    tsf_close(s);
            tsf_close(font);
            throw std::runtime_error("Insufficient memory for SoundFont");
        }
        tsf_set_max_voices(copies[i], 128);
        for (int ch = 0; ch < 16; ++ch)
            tsf_channel_set_presetnumber(copies[i], ch, i < DrumFirst ? i : 0, i >= DrumFirst);
    }
    std::lock_guard<std::mutex> lock(mutex);
    d->panic();
    for (auto *s : d->synth)
        if (s)
            tsf_close(s);
    if (d->base)
        tsf_close(d->base);
    d->base = font;
    d->synth = copies;
    d->sfPath = path;
    d->presetLabels.clear();
    for (int p = 0; p < ProgramCount; ++p) {
        int index = tsf_get_presetindex(font, 0, p);
        QString name = index >= 0 ? QString::fromUtf8(tsf_get_presetname(font, index))
                                  : "Not present in bank 0";
        d->presetLabels << QString("%1 • %2").arg(p + 1, 3, 10, QChar('0')).arg(name);
        strips[p].name = d->presetLabels.last();
    }
    d->resetChannels();
    for (const auto &e : song.events) {
        if (e.time > position.load())
            break;
        int kind = e.status & 240;
        if (kind != 0x90 && kind != 0x80)
            d->event(e);
    }
}
QString Engine::soundfontPath() const {
    std::lock_guard<std::mutex> lock(mutex);
    return d->sfPath;
}
void Engine::load(const Song &s) {
    std::lock_guard<std::mutex> lock(mutex);
    playing = false;
    d->suspended = false;
    d->panic();
    d->resetChannels();
    song = s;
    position = 0;
    d->cursor = 0;
    for (auto &strip : strips)
        for (auto &fx : strip.fx)
            fx->reset();
}
void Engine::play() {
    std::lock_guard<std::mutex> lock(mutex);
    d->suspended = false;
    playing = true;
}
void Engine::pause() {
    std::lock_guard<std::mutex> lock(mutex);
    playing = false;
    d->suspended = true;
    d->midi.panic();
}
void Engine::stop() {
    playing = false;
    seek(0);
}
void Engine::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mutex);
    d->suspended = false;
    d->panic();
    d->resetChannels();
    position = std::clamp(seconds, 0., song.duration);
    d->cursor = 0;
    while (d->cursor < size_t(song.events.size()) && song.events[int(d->cursor)].time < position) {
        const auto &e = song.events[int(d->cursor++)];
        int kind = e.status & 240;
        if (kind != 0x90 && kind != 0x80)
            d->event(e);
    }
    for (auto &strip : strips)
        for (auto &fx : strip.fx)
            fx->reset();
}
void Engine::setSpeed(double speed) {
    std::lock_guard<std::mutex> lock(mutex);
    d->speed = std::clamp(speed, .5, 1.5);
}
void Engine::setTranspose(int semitones) {
    std::lock_guard<std::mutex> lock(mutex);
    d->panic();
    d->transpose = std::clamp(semitones, -12, 12);
}
void Engine::testNote(int part, int note) {
    std::lock_guard<std::mutex> lock(mutex);
    if (part < 0 || part >= PartCount)
        return;
    if (d->suspended) {
        d->panic();
        d->suspended = false;
    }
    int n = part >= DrumFirst ? drumNote(part) : note;
    int channel = part >= DrumFirst ? 9 : 0;
    if (d->synth[part])
        tsf_channel_set_presetnumber(d->synth[part], channel, part < DrumFirst ? part : 0,
                                     part >= DrumFirst);
    d->send(part, {0, 0, 0x90 | channel, n, 105});
    d->auditions.push_back({part, n, d->wall + .35});
}
void Engine::render(float *out, int frames) {
    auto start = std::chrono::steady_clock::now();
    std::fill_n(out, frames * 2, 0);
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        ++droppedBlocks;
        return;
    }
    if (d->suspended) {
        cpu = 0;
        return;
    }
    int done = 0;
    while (done < frames) {
        int n = std::min(256, frames - done);
        double pos = position.load();
        if (playing) {
            while (d->cursor < size_t(song.events.size()) &&
                   song.events[int(d->cursor)].time <= pos + 1e-7)
                d->event(song.events[int(d->cursor++)]);
            if (d->cursor < size_t(song.events.size())) {
                double until = (song.events[int(d->cursor)].time - pos) * Rate / d->speed;
                if (until > 0)
                    n = std::max(1, std::min(n, int(std::ceil(until))));
            }
        }
        for (auto it = d->auditions.begin(); it != d->auditions.end();)
            if (it->end <= d->wall) {
                d->send(it->part, {0, 0, 0x80 | (it->part >= DrumFirst ? 9 : 0), it->note, 0});
                it = d->auditions.erase(it);
            } else
                ++it;
        for (auto &bus : d->buses)
            bus.fill(0);
        bool busSolo = false;
        for (int b = BusFirst; b < trackCount(); ++b)
            busSolo |= strips[b].solo;
        bool anySolo = false;
        for (int i = 0; i < PartCount; ++i)
            anySolo |= strips[i].solo;
        for (int p = 0; p < PartCount; ++p) {
            auto &strip = strips[p];
            auto *buffer = d->scratch.data();
            std::fill_n(buffer, n * 2, 0);
            if (d->synth[p])
                tsf_render_float(d->synth[p], buffer, n, 0);
            for (auto &fx : strip.fx) {
                fx->transportPlaying = playing.load();
                fx->process(buffer, n, d->pending[p], song.bpm * d->speed, pos);
            }
            d->pending[p].clear();
            float peak = 0;
            bool mute = strip.mute || (anySolo && !strip.solo);
            float l = float(strip.volume * (strip.pan > 0 ? 1 - strip.pan : 1)),
                  r = float(strip.volume * (strip.pan < 0 ? 1 + strip.pan : 1));
            for (int f = 0; f < n; ++f) {
                float a = mute ? 0 : buffer[2 * f] * l, b = mute ? 0 : buffer[2 * f + 1] * r;
                if (strip.output >= BusFirst && strip.output < trackCount()) {
                    auto &bus = d->buses[strip.output - BusFirst];
                    bus[2 * f] += a;
                    bus[2 * f + 1] += b;
                } else if (!busSolo) {
                    out[2 * (done + f)] += a;
                    out[2 * (done + f) + 1] += b;
                }
                if (strip.fxSend > 0) {
                    auto &send = d->buses[BusCount - 1];
                    send[2 * f] += a * float(strip.fxSend);
                    send[2 * f + 1] += b * float(strip.fxSend);
                }
                peak = std::max(peak, std::max(std::abs(a), std::abs(b)));
            }
            peaks[p] = std::max(peak, peaks[p].load() * .9f);
        }
        for (int b = BusFirst; b < trackCount(); ++b) {
            auto &strip = strips[b];
            auto *buffer = d->buses[b - BusFirst].data();
            for (auto &fx : strip.fx) {
                fx->transportPlaying = playing.load();
                fx->process(buffer, n, d->empty, song.bpm * d->speed, pos);
            }
            float peak = 0;
            bool muted = strip.mute || (busSolo && !strip.solo);
            float l = float(strip.volume * (strip.pan > 0 ? 1 - strip.pan : 1));
            float r = float(strip.volume * (strip.pan < 0 ? 1 + strip.pan : 1));
            for (int f = 0; f < n; ++f) {
                float a = muted ? 0 : buffer[2 * f] * l, c = muted ? 0 : buffer[2 * f + 1] * r;
                out[2 * (done + f)] += a;
                out[2 * (done + f) + 1] += c;
                peak = std::max(peak, std::max(std::abs(a), std::abs(c)));
            }
            peaks[b] = std::max(peak, peaks[b].load() * .9f);
        }
        auto &master = strips[MasterPart];
        for (auto &fx : master.fx) {
            fx->transportPlaying = playing.load();
            fx->process(out + 2 * done, n, d->empty, song.bpm * d->speed, pos);
        }
        float peak = 0;
        for (int i = 2 * done; i < 2 * (done + n); ++i) {
            float v = (master.mute || d->outputMode == OutputMode::Midi)
                          ? 0
                          : out[i] * float(master.volume *
                                           ((i % 2 == 0 && master.pan > 0)   ? 1 - master.pan
                                            : (i % 2 == 1 && master.pan < 0) ? 1 + master.pan
                                                                             : 1));
            out[i] = std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0;
            peak = std::max(peak, std::abs(out[i]));
        }
        peaks[MasterPart] = std::max(peak, peaks[MasterPart].load() * .9f);
        d->wall += n / Rate;
        if (playing) {
            position = pos + n / Rate * d->speed;
            if (position >= song.duration)
                playing = false;
        }
        done += n;
    }
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    cpu = elapsed / (frames / Rate) * 100.;
}
void Engine::setOutputMode(OutputMode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    d->panic();
    d->outputMode = mode;
}
bool Engine::openMidiOutput(int port, QString &error) {
    std::lock_guard<std::mutex> lock(mutex);
    return d->midi.open(port, error);
}
void Engine::closeMidiOutput() {
    std::lock_guard<std::mutex> lock(mutex);
    d->midi.close();
    d->outputMode = OutputMode::Internal;
}
void Engine::setMelody(bool muted, int channel) {
    std::lock_guard<std::mutex> lock(mutex);
    d->panic();
    d->melodyOff = muted;
    d->melody = std::clamp(channel, 0, 15);
}
bool Engine::melodyMuted() const {
    std::lock_guard<std::mutex> lock(mutex);
    return d->melodyOff;
}
int Engine::melodyChannel() const {
    std::lock_guard<std::mutex> lock(mutex);
    return d->melody;
}
void Engine::panic() {
    std::lock_guard<std::mutex> lock(mutex);
    d->panic();
}
void Engine::setMute(int part, bool muted) {
    std::lock_guard<std::mutex> lock(mutex);
    if (part < 0 || part >= trackCount())
        return;
    strips[part].mute = muted;
    d->midi.panic();
}
void Engine::setSolo(int part, bool solo) {
    std::lock_guard<std::mutex> lock(mutex);
    if (part < 0 || part >= trackCount())
        return;
    strips[part].solo = solo;
    d->midi.panic();
}
bool Engine::setRoute(int part, int output) {
    std::lock_guard<std::mutex> lock(mutex);
    if (part < 0 || part >= PartCount ||
        (output != MasterPart && (output < BusFirst || output >= trackCount())))
        return false;
    strips[part].output = output;
    d->midi.panic();
    return true;
}
int Engine::addBus() {
    std::lock_guard<std::mutex> lock(mutex);
    int index = trackCount();
    Strip strip;
    strip.name = partName(index);
    strip.volume = 1;
    d->buses.emplace_back();
    strips.push_back(std::move(strip));
    peaks.emplace_back(0);
    return index;
}
QStringList Engine::presetNames() const {
    std::lock_guard<std::mutex> lock(mutex);
    return d->presetLabels;
}
QJsonObject Engine::save() {
    std::lock_guard<std::mutex> lock(mutex);
    QJsonArray tracks;
    for (auto &s : strips) {
        QJsonArray effects;
        for (auto &f : s.fx)
            effects.append(f->save());
        tracks.append(QJsonObject{{"name", s.name},
                                  {"volume", s.volume},
                                  {"pan", s.pan},
                                  {"mute", s.mute},
                                  {"solo", s.solo},
                                  {"output", s.output},
                                  {"send", s.fxSend},
                                  {"fx", effects}});
    }
    return {{"schema", 3},      {"soundfont", d->sfPath},      {"song", song.path},
            {"tracks", tracks}, {"melodyMuted", d->melodyOff}, {"melodyChannel", d->melody}};
}
std::shared_ptr<Effect> Engine::restoreEffect(const QJsonObject &o) {
    std::shared_ptr<Effect> f;
    if (o["type"].toString() == "VST3") {
        auto p = std::make_shared<VstEffect>(o["path"].toString(), o["id"].toString(), Rate);
        p->restore(o);
        f = p;
    } else {
        f = builtin(o["type"].toString(), Rate);
        if (!f)
            throw std::runtime_error("Unknown effect");
        auto values = o["values"].toArray();
        for (int i = 0; i < values.size(); ++i)
            f->setParameter(i, values[i].toDouble());
    }
    f->bypass = o["bypass"].toBool();
    return f;
}
QStringList Engine::restore(const QJsonObject &o) {
    if (o["schema"].toInt() < 1 || o["schema"].toInt() > 3)
        throw std::runtime_error("Unsupported project format");
    playing = false;
    QStringList warnings;
    if (!o["soundfont"].toString().isEmpty())
        try {
            loadSoundfont(o["soundfont"].toString());
        } catch (const std::exception &e) {
            warnings << e.what();
        }
    if (!o["song"].toString().isEmpty())
        try {
            load(readSong(o["song"].toString()));
        } catch (const std::exception &e) {
            warnings << e.what();
        }
    setMelody(o["melodyMuted"].toBool(), o["melodyChannel"].toInt(3));
    auto tracks = o["tracks"].toArray();
    if (o["schema"].toInt() < 3) {
        auto legacy = tracks;
        int oldMaster = o["schema"].toInt() == 1 ? 23 : 62;
        int oldBusFirst = oldMaster + 1;
        int oldBusCount = std::max(0, int(legacy.size()) - oldBusFirst);
        tracks = QJsonArray();
        for (int p = 0; p < BusFirst + std::max(BusCount, oldBusCount); ++p)
            tracks.append(QJsonObject{{"volume", p >= BusFirst ? 1. : .8}});
        auto translate = [&](int old, int target) {
            if (old >= legacy.size())
                return;
            auto t = legacy[old].toObject();
            int output = t["output"].toInt(oldMaster);
            t["output"] = output >= oldBusFirst ? BusFirst + output - oldBusFirst : MasterPart;
            tracks[target] = t;
        };
        translate(oldMaster, MasterPart);
        for (int p = 0; p < oldBusCount; ++p)
            translate(oldBusFirst + p, BusFirst + p);
        for (int p = DrumFirst; p < PartCount; ++p) {
            int old = p - DrumFirst + 15;
            if (oldMaster == 23 && old >= 23) {
                int note = drumNote(p);
                old = note == 35                                                             ? 15
                      : note == 40                                                           ? 16
                      : note == 44                                                           ? 17
                      : (note == 41 || note == 43 || note == 47 || note == 48 || note == 50) ? 19
                      : (note == 51 || note == 52 || note == 55 || note == 57 || note == 59) ? 20
                      : note == 37                                                           ? 21
                                                                                             : 22;
            }
            translate(old, p);
        }
        // Channel-based mixes have no exact program-based equivalent. Keep the first
        // channel mapped to each initial program and report the conversion explicitly.
        std::array<bool, ProgramCount> assigned{};
        for (int ch = 0; ch < 16; ++ch)
            if (ch != 9) {
                int program = 0;
                for (const auto &e : song.events)
                    if ((e.status & 15) == ch && (e.status & 240) == 0xc0) {
                        program = e.data1;
                        break;
                    }
                if (!assigned[program]) {
                    translate(ch < 9 ? ch : ch - 1, program);
                    assigned[program] = true;
                }
            }
        warnings << "Converted channel mixer to program mixer. Review melodic FX/routing; channels "
                    "sharing a program now share one strip.";
    }
    int count = std::max(TrackCount, int(tracks.size()));
    while (trackCount() < count)
        addBus();
    {
        std::lock_guard<std::mutex> lock(mutex);
        strips.resize(count);
        peaks.resize(count);
        d->buses.resize(count - BusFirst);
    }
    for (int i = 0; i < std::min(int(tracks.size()), trackCount()); ++i) {
        auto t = tracks[i].toObject();
        std::vector<std::shared_ptr<Effect>> effects;
        for (auto v : t["fx"].toArray())
            try {
                effects.push_back(restoreEffect(v.toObject()));
            } catch (const std::exception &e) {
                warnings << QString("%1: %2").arg(partName(i), e.what());
            }
        std::lock_guard<std::mutex> lock(mutex);
        strips[i].name = t["name"].toString(partName(i));
        strips[i].volume = std::clamp(t["volume"].toDouble(.8), 0., 1.5);
        strips[i].pan = std::clamp(t["pan"].toDouble(), -1., 1.);
        strips[i].mute = t["mute"].toBool();
        strips[i].solo = t["solo"].toBool();
        int output = t["output"].toInt(MasterPart);
        strips[i].output =
            (i < PartCount && output >= BusFirst && output < trackCount()) ? output : MasterPart;
        strips[i].fxSend = i < PartCount ? std::clamp(t["send"].toDouble(), 0., 1.) : 0;
        strips[i].fx = std::move(effects);
    }
    return warnings;
}
} // namespace bt
