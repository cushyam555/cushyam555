# BT Karaoke Studio 0.3 architecture

BT application code is independent of HandyKaraoke. Its legacy implementation is
preserved only in a separate branch. Reference inspection of Handy's NCN behavior
informed the import workflow, not the implementation or application license.

## Signal graph

MIDI/KAR/NCN -> scheduler -> channel program/bank/controller state -> 128 SoundFont
program strips + 47 independent GM drum-note strips -> ordered FX -> gain/pan and
mute/solo -> chosen group bus or Master, plus post-fader send to Bus 5 -> bus FX ->
bus gain/pan/mute/solo -> Master FX/gain -> finite clamp -> miniaudio output.

Program strips occupy 0–127. GM drum notes occupy 128–174; the first eight preserve
legacy representative note order and remaining GM notes follow. Master is 175.
Dynamic buses start at 176, with five created for a new engine. Bus output is fixed
to Master, so arbitrary feedback cannot be created. The UI provides an Add Bus action.
Strips are dynamic vectors, peaks are a deque of atomics, and bus scratch buffers
grow under the graph mutex. There is no fixed bus/FX count ceiling in the UI.

Each program has a TSF copy sharing sample data, with separate MIDI channel state
and voices. A program change routes subsequent notes to the appropriate program
strip; outstanding note assignments are tracked per source channel/note in FIFO
order, so note-offs reach the original sound even after a program change or an
overlapping same-pitch note. Controllers reach programs used by that channel.
Bank variants share their program strip; strip names come from SoundFont bank 0.
Missing presets do not fall back silently to another melodic program. Closed and
pedal hi-hat explicitly choke the open-hat part. VST note events preserve MIDI
channel identity when several source channels share one program strip.

## Audio and threading

Output is 48 kHz stereo float, rendered in chunks of at most 256 frames. Plugin
buffers allow up to 512 frames. UI graph edits use a mutex; render uses try_lock
and outputs silence during an edit. This design is not a hard real-time guarantee.
Plugin parameter edits use a short separate mutex and pending-value array.
FX chains are vectors; practical limits are CPU, RAM and plugin behavior.

VST3 discovery runs in a subprocess with a 12-second timeout. Active plugin DSP
runs in-process; faulty plugins can crash the app. Mono output is duplicated to
stereo; unsupported layouts are rejected. VST2/32-bit, sidechain, delay compensation,
complete CC mapping and dynamic plugin I/O reconfiguration are not implemented.

## Persistent song library

SongLibrary uses a uniquely named Qt QSQLITE connection owned by the UI thread.
Only metadata is stored/loaded for browsing: id, title, artist, key, BPM, duration
and file path. Events/full timed lyrics load at playback. An in-memory path-to-index
hash preserves queue references while scans append/update songs. Imports use a
SQLite transaction, with cancellation retaining successfully processed entries.
The browser limits displayed results to 1,000; searches inspect the full metadata set.

The managed NCN root defaults to AppData/NCN, with Song/Lyrics/Cursor directories.
Folder import validates MIDI and companions, preserves relative paths, copies all
three using QSaveFile and cleans up newly created files on failure. Existing targets
with different contents are rejected. Registered external NCN roots can be indexed
without copying and rescanned. Original source files are never removed.

library.sqlite and plugin registry live under applicationDataPath(). QSettings keeps
NCN storage roots and lyric preferences. BT_KARAOKE_DATA_DIR can override the data
folder; smoke/startup checks set it to a temporary directory to protect user data.

## Lyrics and appearance

A Fusion dark QPalette defines Window/Base/Text/Button colors for all Qt windows,
plus explicit styles for views, combo popups and dialogs. Color swatches choose
black/white text from their brightness. Native OS file dialogs keep OS styling.
LyricsStyle stores font, point size, weight, background/text/sung colors, vertical
alignment, margins and timing offset; controls provide a live preview. Settings
persist with QSettings and optionally inside project JSON. The stage uses this
style rather than hardcoded lyric colors/font.

## Projects and startup

Schema 3 stores the complete dynamic mixer and lyric preferences. Legacy schemas
1/2 are converted with an explicit warning: channel-based mixes cannot map exactly
to program-based mixes when channels share a program. Drum chains/buses are migrated;
users must review melodic chains and Send routing. QSaveFile saves atomically.
SoundFonts, song files and third-party plugins remain external references.

The Windows executable is a static native launcher that starts BTKaraokeApp.exe,
logs/reports child failures and returns the exit code without depending on Qt.
The Qt window appears before a separate audio probe (12-second timeout). On success,
a worker initializes the device while sharing Engine lifetime with the UI. Failure
leaves the window available. Safe Mode skips automatic audio startup. Tests cover
safe/normal launch, delayed/failed initialization, package install/open/uninstall,
and deliberate missing-Qt failure using a developer-PATH-free environment.

## External MIDI

A WinMM worker owns the port and performs driver calls outside render. A bounded
8192-message queue resets on overflow. Events retain source MIDI channels and apply
melodic transpose. Melody/mute/solo filter note-on; note-off passes. Reset sends
sustain-off/all-notes-off. Stop/seek/transpose/port and mix changes prevent hanging
external notes. Ports are reconnected manually per session. The render callback is
the transport clock even for MIDI-only output, requiring a working audio device.
There is no audio-to-loopMIDI conversion: audio FX, gain/pan and bus audio remain
inside BT. No MIDI input, SysEx or MIDI Clock is claimed. An installed loopMIDI port
and receiving application are required for real end-to-end validation.
