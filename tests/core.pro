QT += core gui widgets sql testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = bt-core-tests
INCLUDEPATH += .. ../BASS/bass24 ../BASS/bassmidi24 ../BASS/bass_fx24 ../BASS/bassmix24 ../BASS/bass_vst24
SOURCES += core_test.cpp ../SongDatabase.cpp ../Song.cpp ../Midi/MidiFile.cpp ../Midi/MidiEvent.cpp ../Midi/MidiHelper.cpp ../Utils.cpp
HEADERS += ../SongDatabase.h ../Song.h

win32:LIBS += -L$$PWD/../BASS/bass24/x64 -lbass -L$$PWD/../BASS/bassmidi24/x64 -lbassmidi
unix:!macx:LIBS += -L$$PWD/../BASS/bass24-linux/x64 -lbass -L$$PWD/../BASS/bassmidi24-linux/x64 -lbassmidi
DISTFILES += fixtures/test-tone.sf2 fixtures/generate_soundfont.py
