#include "MidiOutput.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <mmsystem.h>
#include <windows.h>
#endif
namespace bt {
struct MidiOutput::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<unsigned long> queue;
    std::thread worker;
    bool stop = true, reset = false;
#ifdef _WIN32
    HMIDIOUT handle = nullptr;
#endif
};
MidiOutput::MidiOutput() : d(new Impl) {}
MidiOutput::~MidiOutput() {
    close();
}
QStringList MidiOutput::ports() {
    QStringList names;
#ifdef _WIN32
    for (UINT i = 0; i < midiOutGetNumDevs(); ++i) {
        MIDIOUTCAPSW caps{};
        names << (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR
                      ? QString::fromWCharArray(caps.szPname)
                      : QString("MIDI port %1").arg(i));
    }
#endif
    return names;
}
bool MidiOutput::open(int port, QString &error) {
    close();
#ifdef _WIN32
    auto result = midiOutOpen(&d->handle, UINT(port), 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        wchar_t text[256]{};
        midiOutGetErrorTextW(result, text, 256);
        error = QString::fromWCharArray(text);
        return false;
    }
    d->stop = false;
    d->worker = std::thread([this] {
        for (;;) {
            std::unique_lock<std::mutex> lock(d->mutex);
            d->wake.wait(lock, [this] { return d->stop || d->reset || !d->queue.empty(); });
            if (d->stop)
                break;
            if (d->reset) {
                d->reset = false;
                lock.unlock();
                midiOutReset(d->handle);
                for (int ch = 0; ch < 16; ++ch) {
                    midiOutShortMsg(d->handle, (0xb0 | ch) | (64 << 8));
                    midiOutShortMsg(d->handle, (0xb0 | ch) | (123 << 8));
                }
            } else {
                auto msg = d->queue.front();
                d->queue.pop_front();
                lock.unlock();
                midiOutShortMsg(d->handle, msg);
            }
        }
        midiOutReset(d->handle);
    });
    return true;
#else
    Q_UNUSED(port);
    error = "External MIDI output is available in the Windows build.";
    return false;
#endif
}
void MidiOutput::close() {
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->stop = true;
        d->queue.clear();
        d->reset = false;
    }
    d->wake.notify_one();
    if (d->worker.joinable())
        d->worker.join();
#ifdef _WIN32
    if (d->handle) {
        midiOutClose(d->handle);
        d->handle = nullptr;
    }
#endif
}
void MidiOutput::send(const MidiEvent &e) {
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->stop)
        return;
    if (d->queue.size() >= 8192) {
        d->queue.clear();
        d->reset = true;
    } else
        d->queue.push_back((e.status & 255) | ((e.data1 & 127) << 8) | ((e.data2 & 127) << 16));
    d->wake.notify_one();
}
void MidiOutput::panic() {
    std::lock_guard<std::mutex> lock(d->mutex);
    d->queue.clear();
    d->reset = true;
    d->wake.notify_one();
}
} // namespace bt
