# BT Karaoke Studio 0.2 architecture

Application source is independent of HandyKaraoke. The old implementation is
preserved in the legacy-handykaraoke branch, not compiled into this application.

## Signal path

MIDI/KAR/NCN file -> bounded parser and tempo-to-seconds conversion -> transport
scheduler -> per-part SoundFont instances -> ordered FX chains -> strip gain/pan
and mute/solo -> direct master or group bus A–F (plus post-fader send to F)
-> ordered bus FX -> bus gain/pan/mute/solo -> stereo sum -> master FX -> master gain -> output safety clamp
-> miniaudio default output device.

There are 15 melodic MIDI parts and 47 independent GM drum-note parts (35–81).
The first eight legacy part indices retain their representative note identities;
remaining GM notes occupy indices 23–61. Master is 62 and buses A–F are 63–68.
Hi-hat choke is explicitly preserved across closed/pedal/open channels.
Bus outputs are fixed to Master, so the graph is acyclic. Direct channels are
excluded when any group bus is soloed. Post-fader sends to Bus F remain independent
of group bus gain. Set return FX to fully wet for conventional send/return use. TSF copies share the loaded sample data
but have independent voices so Kick FX cannot process Snare audio.

VST3 instruments replace the signal at their position in the chain and receive
note events. Effect and instrument classes use the official VST3 hosting APIs.
Mono output is duplicated to stereo; unsupported output layouts are rejected.

## Ownership and threading

The UI constructs effects and plugin instances. The audio callback renders at
48 kHz, stereo float32, subdividing driver buffers into blocks of at most 256
samples. The host prepares VST3 buffers for up to 512 samples.

Engine graph/state edits use a mutex; the callback uses try_lock and emits silence
rather than waiting during a graph edit. This is an initial design, not a hard
real-time guarantee. Large state changes can briefly interrupt playback. Plugin
parameter edits use a separate short lock and pending-value array.

FX chains are dynamically sized vectors, with no app-level FX count cap. Actual
limits are CPU, memory, and the plugins themselves. Bypassed VST3 instances still
process note/control state while their output is discarded, avoiding hung notes.
Built-in FX bypasses skip their processing.

Plugin discovery launches the same executable in --scan-vst3 mode, writes a JSON
result file, and terminates scans after 12 seconds. Discovery is isolated; active
plugin DSP is in-process and a faulty plugin can crash the host. Preset files are
trusted user files and may load the plugin paths they contain.

## Persistence

.btproject and .btchain are schema-versioned JSON. QSaveFile commits atomically.
Plugin component state is base64 and normalized controller values are also saved
so offline edits survive before an audio callback has applied them. Songs,
SoundFonts, and third-party plugins remain external references.

## Extension boundaries

No VST2/32-bit bridge, AU/AAX, sidechain routing, plugin delay compensation,
dynamic I/O reconfiguration, or complete MIDI-controller mapping is implemented
in 0.1. No recording, microphone processing, MP4/MP3 player or QR request server
is claimed. The tests include an original gain VST3 module, not a commercial
plugin-compatibility certification. Qt and the audio/hosting SDKs keep their own
licenses; see THIRD_PARTY.md.

## Startup lifecycle (0.1.1)

The Windows entry point is a small, statically linked native launcher. It starts
BTKaraokeApp.exe, preserves the exit code, and reports startup errors without
requiring Qt. The Qt process logs before constructing QApplication.

MainWindow is shown before a zero-delay task launches a separate audio-probe
process. A probe crash/failure/12-second timeout leaves the UI offline. After a
successful probe, audio initializes on a worker thread. MainWindow and that
worker share ownership of Engine; closing the window requests cancellation and
never waits on a driver initialization lock. This preserves engine lifetime
while a worker finishes. Windows audio initialization excludes the silent null
backend, so a missing audio device is reported rather than presented as ready.

Safe mode skips automatic audio initialization. Startup tests exercise both the
normal and safe paths, and unit tests inject delayed/failed initialization and
verify engine lifetime when its window owner releases it during initialization.

## External MIDI (0.2)

A WinMM worker owns the MIDI device and performs driver calls outside the audio
callback. A bounded 8192-message queue resets on overflow. Transport events keep
the source MIDI channel and apply melodic transpose. Melody and mute/solo filter
note-on; note-off always passes. Stop, seek, transpose, melody/mute/solo and port
changes reset pending notes. Reset also sends sustain-off and all-notes-off.
The miniaudio render callback remains the transport clock, including MIDI-only
mode; an active output device is required. MIDI-only mode mutes final PCM output.
There is no SysEx, MIDI Clock or audio-to-loopMIDI conversion. UI mixer gain/pan
and audio FX are not converted into external MIDI automation. External output
cannot be tested end-to-end without an installed loopback port and receiving app.
Project schema 2 stores routing, send, names and melody settings; it migrates old
24-strip schema 1 projects, replicating each legacy drum-group chain per GM note.
Ports are intentionally reconnected by the user per session.
