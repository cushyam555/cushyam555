# Third-party components

The application code in src/ and tests/ is newly written for BT Karaoke. No
HandyKaraoke source, BASS binaries, WinSparkle code, or assets are used by this version.
The previous application remains in the separate legacy-handykaraoke branch and
retains its original licenses. Ownership of this application does not confer
ownership of its dependencies or third-party audio plugins.

- Qt 6 (Qt 5.15 also builds): dynamically linked under LGPL-3.0. The LGPL text
  is supplied in LICENSES. Users may replace compatible Qt libraries; there is
  no technical restriction against debugging/relinking with modified Qt.
  Corresponding Qt source: https://code.qt.io/cgit/qt/ (select the shipped version).
  Windows builds use the unmodified Qt 6.8.3 distribution from Qt's public archive.
- Steinberg VST3 SDK 3.8.0, commit 9fad9770f2ae8542ab1a548a68c1ad1ac690abe0:
  MIT. Source: https://github.com/steinbergmedia/vst3sdk
- miniaudio 0.11.22, commit 350784a9467a79d0fa65802132668e5afbcf3777:
  choose MIT from the project's dual public-domain/MIT license.
  Source: https://github.com/mackron/miniaudio
- TinySoundFont, commit 853a0a171759f1ddba0de1442133a75912bbeffa: MIT.
  Source: https://github.com/schellingb/TinySoundFont
- Microsoft Visual C++ redistributable runtime: deployed app-locally under its
  redistribution terms from the Visual Studio build environment.

CMake records the pinned dependencies and copies their original license files
into the installer. Third-party VST3 plugins, songs and production SoundFonts
are not included. VST is a trademark of Steinberg Media Technologies GmbH;
BT Karaoke is an independent host, not a Steinberg product.
