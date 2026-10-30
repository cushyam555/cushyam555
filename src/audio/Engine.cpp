#include "Engine.h"
#include "plugins/VstEffect.h"
#include <QFile>
#include <QJsonArray>
#include <algorithm>
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
    ma_device device{};
    bool initialized = false;
    std::mutex deviceMutex, errorMutex;
    std::atomic<bool> closing{false};
    QString lastAudioError;
    QString sfPath;
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
        for (int p = 0; p < PartCount; ++p) {
            if (synth[p])
                tsf_channel_sounds_off_all(synth[p], 0);
            for (int note = 0; note < 128; ++note)
                pending[p].push_back({0, 0, 0x80, note, 0});
        }
        auditions.clear();
    }
    void send(int p, const MidiEvent &e) {
        if (p < 0 || p >= PartCount)
            return;
        auto event = e;
        int kind = e.status & 240;
        if (p < 15 && (kind == 0x80 || kind == 0x90))
            event.data1 = std::clamp(e.data1 + transpose, 0, 127);
        pending[p].push_back(event);
        tsf *s = synth[p];
        if (!s)
            return;
        if (kind == 0x90 && e.data2)
            tsf_channel_note_on(s, 0, event.data1, e.data2 / 127.f);
        else if (kind == 0x80 || kind == 0x90)
            tsf_channel_note_off(s, 0, event.data1);
        else if (kind == 0xc0)
            tsf_channel_set_presetnumber(s, 0, e.data1, p >= 15);
        else if (kind == 0xb0)
            tsf_channel_midi_control(s, 0, e.data1, e.data2);
        else if (kind == 0xe0)
            tsf_channel_set_pitchwheel(s, 0, e.data1 | (e.data2 << 7));
    }
    void event(const MidiEvent &e) {
        int channel = e.status & 15, kind = e.status & 240;
        // Splitting drum voices must preserve the normal closed/open hi-hat choke.
        if (channel == 9 && kind == 0x90 && e.data2 && (e.data1 == 42 || e.data1 == 44)) {
            send(18, {e.time, e.tick, 0x80, 46, 0});
            if (synth[18])
                tsf_channel_sounds_off_all(synth[18], 0);
        }
        if (channel == 9 && kind != 0x80 && kind != 0x90) {
            for (int p = 15; p < PartCount; ++p)
                send(p, e);
        } else
            send(partFor(channel, e.data1), e);
    }
};
Engine::Engine() : d(new Impl) {
    for (int i = 0; i < TrackCount; ++i)
        strips[i].name = partName(i);
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
        tsf_channel_set_presetnumber(copies[i], 0, 0, i >= 15);
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
}
void Engine::stop() {
    playing = false;
    seek(0);
}
void Engine::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mutex);
    d->suspended = false;
    d->panic();
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
    int n = part == 15   ? 36
            : part == 16 ? 38
            : part == 17 ? 42
            : part == 18 ? 46
            : part == 19 ? 45
            : part == 20 ? 49
            : part == 21 ? 39
            : part >= 22 ? 60
                         : note;
    d->send(part, {0, 0, 0x90, n, 105});
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
                d->send(it->part, {0, 0, 0x80, it->note, 0});
                it = d->auditions.erase(it);
            } else
                ++it;
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
                out[2 * (done + f)] += a;
                out[2 * (done + f) + 1] += b;
                peak = std::max(peak, std::max(std::abs(a), std::abs(b)));
            }
            peaks[p] = std::max(peak, peaks[p].load() * .9f);
        }
        auto &master = strips[MasterPart];
        for (auto &fx : master.fx) {
            fx->transportPlaying = playing.load();
            fx->process(out + 2 * done, n, d->empty, song.bpm * d->speed, pos);
        }
        float peak = 0;
        for (int i = 2 * done; i < 2 * (done + n); ++i) {
            float v = master.mute
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
                                  {"fx", effects}});
    }
    return {{"schema", 1}, {"soundfont", d->sfPath}, {"song", song.path}, {"tracks", tracks}};
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
    if (o["schema"].toInt() != 1)
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
    auto tracks = o["tracks"].toArray();
    for (int i = 0; i < std::min(int(tracks.size()), TrackCount); ++i) {
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
        strips[i].fx = std::move(effects);
    }
    return warnings;
}
} // namespace bt
