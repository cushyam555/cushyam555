# BT Karaoke Studio 0.3.0

แอป Windows x64 ที่เขียนใหม่สำหรับ BT Karaoke โค้ดแอป MIT ไม่ใช้ซอร์สหรือ BASS
จาก HandyKaraoke; ไลบรารี Qt, VST3 SDK, miniaudio และ TinySoundFont มีใบอนุญาตแยกใน THIRD_PARTY.md

## มิกเซอร์ 128 เสียงตาม SoundFont

- ช่องหลักคือ **SoundFont Program 001–128** ไม่ใช่ MIDI channel 1–16
- โหลด `.sf2` แล้วชื่อช่องเปลี่ยนเป็นชื่อ preset จาก bank 0 ของ SoundFont นั้น
  ช่องที่ SoundFont ไม่มีจะแสดง `Not present in bank 0` และไม่แอบใช้เสียงอื่นแทน
- MIDI Program Change เป็นตัวเลือกช่องเสียง เช่น หลาย MIDI channels ที่ใช้ Grand Piano
  จะรวมเข้าช่อง Grand Piano เดียวกัน ส่วนเสียงเก่าที่ยังค้างหลังเปลี่ยน program จะได้รับ note-off ถูกช่อง
- Bank variants ของ program เดียวกันใช้ strip ร่วมกัน; ชื่อที่แสดงอ้างอิง bank 0
- กลอง GM 35–81 แยก 47 ชิ้นในตัวกรอง Drums โดยรักษา hi-hat choke
- ทุกเสียง กลอง บัส และ Master ใส่ FX ต่อกันได้ ไม่มีเพดานจำนวนตายตัว
- FX ในตัว: EQ, Compressor, Gate, Saturation, Delay, Reverb, Limiter, Gain และรองรับ VST3 x64 mono/stereo
- ลากเรียง FX, bypass, duplicate, แก้ parameter, เปิด native editor บน Windows, บันทึก chain preset
- เริ่มต้น **5 บัสกรุ๊ป** กด **＋ Bus** เพิ่มได้ แต่ละบัสมี FX, gain/pan/mute/solo/meter
- เลือก output ของแต่ละเสียงไป Master หรือบัสใดก็ได้; กลองเริ่มต้นไป Bus 1
- Post-fader Send ไป Bus 5 สำหรับ Reverb/Delay ร่วมกัน (ตั้ง return FX เป็น wet 100% ตามต้องการ)
- บัสออก Master เท่านั้น เพื่อป้องกัน feedback; Master อยู่ด้านขวาตลอด
- ตัวกรอง **SoundFont • 128 sounds / Drums / Buses / All** และช่องค้นหาชื่อเสียง/เลขโปรแกรม
- คลิกขวาชื่อช่องหรือบัสเพื่อเปลี่ยนชื่อ

จำนวน FX/บัสขึ้นกับ CPU/RAM จริง ปลั๊กอิน DSP ทำงานใน process แอป ปลั๊กอินเสียอาจทำให้แอปปิด
ยังไม่รองรับ VST2/32-bit, AU/AAX, sidechain, plugin delay compensation, multi-output VST หรือ CC mapping ทุกกรณี

## คลัง NCN ในโปรแกรม

กด **คลัง NCN…** ในหน้า Library:

1. **นำเข้าชุด NCN จากโฟลเดอร์…** เลือกรากที่มี `Song`, `Lyrics`, `Cursor`
   โปรแกรมคัดลอกไฟล์คู่กันเข้าที่เก็บ NCN ของ BT โดยรักษาโฟลเดอร์ย่อย และไม่ลบต้นฉบับ
2. หรือ **เพิ่มไฟล์เพลง NCN…** เลือก `.mid` ใน Song, `.lyr` ใน Lyrics หรือ `.cur` ใน Cursor
   โดยต้องมีไฟล์คู่กันในชุด NCN เดียวกัน
3. ถ้ามีคลังใหญ่อยู่แล้ว เลือก **เลือกโฟลเดอร์ NCN ที่มีอยู่และอัปเดตคลัง…**
   เพื่อทำดัชนีจากที่เดิมโดยไม่คัดลอก แล้วกด **อัปเดตคลังเพลงจากที่เก็บทั้งหมด** เมื่อมีเพลงใหม่
4. ค้นหาด้วยรหัสเพลง ชื่อเพลง ศิลปิน หรือคีย์ แล้วเพิ่มเข้าคิว/ลากเรียงคิว

SQLite เก็บรหัส/ชื่อ/ศิลปิน/คีย์/BPM/path แบบถาวร เปิดใหม่ยังพบเพลงโดยไม่ต้องเปิด project
ข้อมูล MIDI events และเนื้อเพลงเต็มจะโหลดตอนเล่น ไม่เก็บทุกเพลงเต็มใน RAM
ใช้ transaction ระหว่างนำเข้า ข้ามไฟล์ซ้ำ อัปเดตรายการเดิม และยกเลิกได้
หน้าค้นหาแสดงสูงสุด 1,000 ผลลัพธ์ ให้พิมพ์กรองเมื่อมีเพลงมากกว่านั้น

ที่เก็บเริ่มต้นคือโฟลเดอร์ AppData ของ BT Karaoke ใต้ `NCN/Song`, `NCN/Lyrics`, `NCN/Cursor`
กด **เปิดที่เก็บ NCN** เพื่อดูตำแหน่งจริง หรือเลือกที่เก็บใหม่สำหรับเพลงที่จะนำเข้า
ฐานข้อมูล `library.sqlite` อยู่ใน AppData เดียวกัน เปลี่ยนที่เก็บไม่ย้ายหรือลบเพลงเก่า
ชื่อไฟล์ปลายทางที่มีข้อมูลต่างกันจะถูกรายงานว่า conflict ไม่เขียนทับเงียบ ๆ
ไม่มีเพลงที่มีลิขสิทธิ์รวมมาให้; ไม่รองรับ HNK หรือ ZIP โดยตรง ให้แตก NCN ก่อนนำเข้า

ได้ตรวจพฤติกรรม HandyKaraoke 2.4.1 จาก
[SongDatabase.cpp](https://github.com/pie62/HandyKaraoke/blob/2.4.1/SongDatabase.cpp) และ
[SettingsDialog.cpp](https://github.com/pie62/HandyKaraoke/blob/2.4.1/SettingsDialog.cpp):
เลือก NCN root → สแกน Song พร้อม LYR/CUR คู่กัน → เก็บ metadata ลง SQLite → ค้นหา/เข้าคิว
BT ใช้ขั้นตอนนี้โดยเขียนระบบจัดเก็บและนำเข้าใหม่ของตัวเอง

## เนื้อเพลงและหน้าตั้งค่า

กด **Lyrics / appearance** เลือกฟอนต์ ขนาด ตัวหนา สีเนื้อเพลง สีไฮไลต์ สีพื้นหลัง
ตำแหน่งบน/กลาง/ล่าง ระยะขอบ และ offset เวลาเนื้อเพลง พร้อม preview แล้ว Save
การตั้งค่าคงอยู่หลังเปิดใหม่ และบันทึกใน project ได้ กด Stage / second display เพื่อเปิดจอร้อง

ทั้งแอปและหน้าตั้งค่าใช้ Fusion dark palette และสีพื้นหลัง/ข้อความที่กำหนดชัดเจน
ปุ่มตัวอย่างสีอ่อนใช้ตัวอักษรดำ สีเข้มใช้ตัวอักษรขาว หน้าต่างเลือกไฟล์แบบ native ใช้สีของ Windows

## เล่นเพลงและ loopMIDI

เลือก SoundFont `.sf2` หรือเพิ่ม VST3 instrument ที่ช่องเสียง แล้วโหลด MIDI format 0/1, KAR หรือ NCN
มีคิวเพลง, auto-next, transpose, tempo, ปิดเมโลดี้พร้อมเลือก MIDI channel (เริ่มต้น channel 4) และ Panic

สร้างพอร์ตใน loopMIDI ก่อน แล้วกด **MIDI / loopMIDI** → Refresh → เลือกพอร์ต → Connect
ให้โปรแกรมเสียงปลายทางเปิดพอร์ตเดียวกันเป็น MIDI input เลือก external-only หรือ internal+external ได้
loopMIDI ส่ง **ข้อมูล MIDI ไม่ใช่เสียงหลัง FX**; fader/pan/FX ในแอปไม่กลายเป็น MIDI automation
ต้องมี Audio output พร้อมทำงานเพื่อเป็น playback clock; ไม่มี MIDI input, SysEx หรือ MIDI Clock
Reset/การเปลี่ยน mute/solo/หยุด/seek จะตัดโน้ตภายนอกที่ค้างอยู่; เชื่อมต่อพอร์ตใหม่ในแต่ละ session

## ดาวน์โหลดและติดตั้ง

จาก [Windows builds](https://github.com/cushyam555/cushyam555/actions) เลือกรอบ 0.3.0 ที่สำเร็จ
และ artifact **BT-Karaoke-Studio-0.3.0-Windows-x64** แตก ZIP แล้วเปิด
`BTKaraoke-0.3.0-Windows-x64-setup.exe` ติดตั้งทับรุ่นเดิมได้ ภายในมี SHA256.txt

ตัวติดตั้งยังไม่มี code signing; ต้องติดตั้งทั้ง `BTKaraoke.exe` (native launcher) และ `BTKaraokeApp.exe`
หากเริ่มเสียงไม่ได้ หน้าต่างยังเปิดให้แก้ตั้งค่าได้ มี **BT Karaoke Studio - Safe Mode** ใน Start menu
Log: `%LOCALAPPDATA%\BTStudio\BTKaraoke\logs` (`startup.log`, `launcher.log`)

## Project และการอัปเกรด

รุ่นนี้ใช้ project schema 3 รวมชื่อเสียง FX บัสที่เพิ่ม เส้นทางมิกเซอร์ และการตั้งค่าเนื้อเพลง
เปิด schema 1/2 ได้แบบแปลงโครงสร้าง: คงกลองและบัสเดิม แล้วนำช่อง MIDI ไปจับกับ program เริ่มต้นของเพลง
ช่อง MIDI ที่ใช้เสียงเดียวกันจะรวมกัน จึงแจ้งเตือนให้ตรวจมิกซ์/FX/routing และ Send ใหม่ก่อนบันทึกเป็นไฟล์ใหม่
ไม่มีทางแปลง channel mixer ไป program mixer แบบตรงกันทุกกรณี รุ่นเก่าเปิด schema 3 ไม่ได้
ไฟล์ project อ้างอิง SoundFont/เพลง/ปลั๊กอินภายนอก ไม่ได้แพ็กรวมไฟล์เหล่านั้น

## Build และตรวจสอบ

CMake 3.25+, C++17, Qt 6 Widgets/Sql/Test (รองรับ Qt 5.15 ด้วย), Git
Dependencies ล็อก commit ไว้ใน CMakeLists.txt; ใช้ QSQLITE driver สำหรับคลังเพลง

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BTKaraoke bt_tests --parallel 3
ctest --test-dir build --output-on-failure
```

Windows ใช้ x64 MSVC Native Tools, Qt 6.8.3, Inno Setup 6 และ `packaging/windows.ps1`
โดยกำหนด `QT_ROOT_DIR`, `VCToolsRedistDir`, `RUNNER_TEMP` ให้เป็นโฟลเดอร์จริงที่เขียนได้
Smoke/startup tests ใช้คลังชั่วคราว ไม่เขียนคลังเพลงของผู้ใช้

Core tests ครอบคลุม program routing/note-off หลัง program change, ชื่อ SF2, บัสเพิ่มและ state round-trip,
FX chain, กลอง, NCN copy/conflict/source removal, SQLite หลังเปิดใหม่, lyric style และ dark palette
รวม VST3 integration module จริง และการเปิดแอปทั้ง safe/normal
ยังต้องลอง SoundFont, เพลง NCN, loopMIDI, จอที่สอง และปลั๊กอินจริงของผู้ใช้บนพีซี
