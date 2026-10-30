#ifndef CONFIG_H
#define CONFIG_H

#include <QDir>



#ifdef Q_PROCESSOR_X86_64
#define VST_CHECKER_NAME "VSTChecker_x64.exe"
#else
#define VST_CHECKER_NAME "VSTChecker_x86.exe"
#endif


#define TEMP_DIR_PATH           QDir::tempPath() + "/BTKaraoke"
#define TEMP_MIDI_DIR_PATH      TEMP_DIR_PATH + "/temp.mid"

#define ALL_DATA_DIR_PATH       (qEnvironmentVariableIsSet("BT_KARAOKE_DATA_DIR") ? qEnvironmentVariable("BT_KARAOKE_DATA_DIR") : QDir::homePath() + "/.BTKaraoke")

#define DATABASE_DIR_PATH       ALL_DATA_DIR_PATH + "/Data"
#define DATABASE_FILE_PATH      DATABASE_DIR_PATH + "/Database.db3"


#define CONFIG_DIR_PATH         ALL_DATA_DIR_PATH + "/Config"
#define CONFIG_APP_FILE_PATH    CONFIG_DIR_PATH + "/BTKaraoke.conf"
#define CONFIG_SYNTH_FILE_PATH  CONFIG_DIR_PATH + "/SynthMixer.conf"


#define HANDY_PLAYLIST_FILTER_TEXT  "Handy playlist file (*.handypl)"
#define HANDY_PLAYLIST_HEADTEXT     "Handy Karaoke playlist file"
#define HANDY_PLAYLIST_VERSION      "Version="
#define HANDY_PLAYLIST_VERSION_1    "Version=1"
#define HANDY_PLAYLIST_NAME         "Name="
#define HANDY_PLAYLIST_COUNT        "Count="
#define HANDY_PLAYLIST_SONGLIST     "[Song list]"
#define HANDY_PLAYLIST_SONG         "Song="
#define HANDY_PLAYLIST_END_SONGLIST "[End song list]"



#endif // CONFIG_H
