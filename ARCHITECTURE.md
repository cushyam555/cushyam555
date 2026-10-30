# BT Karaoke Studio 0.4.1 architecture

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

The song root is selected by the user outside app data/install directories, with Song/Lyrics/Cursor directories for NCN and KAR for KAR files.
Folder import preflights/counts songs before changes, preserves relative paths, copies all
three through staging and backup/promote/rollback. Existing targets prompt Skip or Overwrite. Registered external NCN roots can be indexed
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

## 0.4 changes

Program labels use the native 000–127 indexing; project `programBase: 0` distinguishes
new names from the old automatic one-based prefix. Melody is permanently MIDI channel
9 (zero-based 8). Muting only releases that channel's held notes rather than calling
the global panic. Insert/Delete change key by one semitone, PageUp/PageDown change
speed by one percent; text editing, modal dialogs and popups retain normal key handling.

Import boundaries accept NCN triplets or KAR only. SongImport performs a read-only
preflight and returns metadata/destination/conflict flags. The UI displays counts
before writing, then requests Skip/Overwrite on conflicts, optionally for the remaining
batch. Copying is restricted to user-selected paths outside application data/install
folders. Existing older managed data is not moved or deleted. NCN replacement stages
all new files, backs up old targets, promotes the set and restores on failure. Recovery
backups remain available if restoration itself fails. SQLite still stores only metadata.

LyricCanvas replaces rebuilding a full-song QLabel HTML string on each refresh. It
precomputes timed text rows at song load, caches QTextLayout shaping/wrapping for the
current pair of rows, and repaints a fractional highlight clip using QPainter CPU
raster rendering. Line gap is independent of text height. Transport time interpolation
uses a monotonic clock, follows speed and reanchors on pause/seek; paused text avoids
redundant painting. Layout is recreated only on page/font/size changes. Profiles target
30/60/120 lyric FPS, 10/25/40 mixer Hz and 64/128/256 sampler voices per program. Actual
frame rate is host/display dependent; there is no new GPU driver initialization.

## 0.4.1 song-database workflow

The primary Add Songs action opens a two-path NCN/KAR database configuration, matching
the reference application's folder-selection and Update Database workflow. This path
indexes files in place. The prior copy/import workflow is an explicitly separate,
collapsed extra tool. Persisted NCN/KAR roots are distinct from the copy destination.

DatabaseScan validates NCN root structure, counts candidate files, scans metadata and
identifies repeated ids within each type. The UI shows progress/current file and a
pre-commit summary. Duplicate resolution changes only the proposed index. A worker
uses its own uniquely named Qt SQL connection for the full replacement transaction;
the UI connection is never used from another thread. Cancellation rolls back to the
previous index. Source files remain unchanged. Queue entries are remapped by path
following commit, retaining those still in the selected source folders.
