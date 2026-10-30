# BT Karaoke Studio 0.4.0

แอปคาราโอเกะ Windows x64 โค้ดแอปเขียนใหม่เป็น MIT; Qt, VST3 SDK, miniaudio,
TinySoundFont มีใบอนุญาตแยกใน THIRD_PARTY.md

## สิ่งที่ปรับในรุ่นนี้

- SoundFont **000–127 รวม 128 เสียง** ชื่อจาก bank 0 ของ `.sf2`; ช่องที่ไม่มี preset จะแสดงว่าไม่มี
- ปิดเมโลดี้ด้วยช่องติ๊ก **CH9 คงที่** (MIDI channel 9, ไม่ใช่ช่องกลอง CH10)
  ไม่มีตัวเลือกเปลี่ยน channel และการติ๊กปิดไม่ตัดโน้ตค้างของช่องดนตรีอื่น
- หน้านำเข้า/คลังรองรับ **NCN และ KAR**; NCN ต้องมี Song/Lyrics/Cursor คู่กัน
- KEY เพิ่ม/ลดทีละ 1: **Insert / Delete**; TEMPO เพิ่ม/ลดทีละ 1%: **Page Up / Page Down**
  ขอบเขต KEY −12 ถึง +12 และ TEMPO 50–150%; ขณะพิมพ์ในช่องข้อความ Delete ยังแก้ข้อความตามปกติ
- เนื้อเพลงปรับระยะห่างบรรทัดบน–ล่างได้ 0–200 logical pixels นอกเหนือจากตำแหน่งบน/กลาง/ล่าง
- จอร้องใช้ CPU วาดจาก text layout ที่ cache ไว้ และไฮไลต์แบบค่อย ๆ ไหลระหว่างเวลาตัวอักษร/ข้อความ
  ไม่สร้าง HTML เนื้อเพลงทั้งเพลงใหม่ทุกเฟรม เมื่อเปลี่ยนหน้า/ขนาด/ฟอนต์จึงจัด layout ใหม่
- ระดับทรัพยากร **พื้นฐาน / ขั้นกลาง / สูงสุด** ตั้งใน Lyrics / appearance และจำค่าหลังเปิดใหม่

| ระดับ | เป้าหมายภาพเนื้อเพลง | รีเฟรชมิกเซอร์ | Voice limit ต่อเสียง SoundFont |
|---|---:|---:|---:|
| พื้นฐาน | ประมาณ 30 FPS | 10 Hz | 64 |
| ขั้นกลาง | ประมาณ 60 FPS | 25 Hz | 128 |
| สูงสุด | ประมาณ 120 FPS | 40 Hz | 256 |

FPS จริงขึ้นกับ Windows, จอภาพ, CPU, SoundFont และปลั๊กอิน ไม่ใช่การรับรองความลื่นบนทุกพีซี
รุ่นนี้ใช้ CPU renderer; ไม่ต้องเริ่ม OpenGL/GPU driver เพิ่ม เมื่อเนื้อเพลงยาวเกินพื้นที่จะลดขนาดวาดให้พอดี
การปรับระดับไม่ได้เปิด/ปิด FX ของผู้ใช้และไม่ได้เปลี่ยนจำนวนบัส

## เพิ่มเพลงและการจัดเก็บ

1. กด **เพิ่มเพลง NCN / KAR** หรือ **คลัง NCN… → นำเข้าชุด NCN จากโฟลเดอร์…**
2. เลือกที่เก็บเพลงของคุณเอง เช่นโฟลเดอร์บนไดรฟ์เพลง **นอกโฟลเดอร์ข้อมูล/ติดตั้ง BT Karaoke**
   โปรแกรมไม่สร้างที่เก็บเพลงอัตโนมัติใน Data อีกต่อไป
3. โปรแกรมวิเคราะห์และแสดง **จำนวนเพลงที่นำเข้าได้ / ชื่อไฟล์หรือรายการซ้ำ / ไฟล์ไม่ผ่าน** ก่อนคัดลอก
   กด **เริ่มนำเข้า** หลังตรวจสรุปได้ หรือยกเลิกโดยยังไม่เขียนไฟล์เพลง
4. เมื่อพบไฟล์ปลายทางหรือรายการเดิม จะถาม **ข้าม / ทับไฟล์** ก่อนดำเนินการ
   มีช่องใช้คำตอบเดียวกันกับรายการซ้ำที่เหลือ และยกเลิกระหว่างนำเข้าได้
5. NCN เก็บเป็น `Song`, `Lyrics`, `Cursor` พร้อมโฟลเดอร์ย่อยเดิม; KAR เก็บใน `KAR`
   ทับชุด NCN เป็นชุดเดียวกัน โดย stage ไฟล์ใหม่และสำรองไฟล์เดิมไว้จนเปลี่ยนครบ

เลือก **โฟลเดอร์ NCN ที่มีอยู่และอัปเดตคลัง** เพื่อเพิ่มดัชนีจากที่เดิมโดยไม่คัดลอกไฟล์ได้
การนำเข้าแบบไม่คัดลอกถามว่าจะข้ามหรือทับข้อมูลรายการเดิม โดยไม่แก้ไฟล์ต้นฉบับ
ไฟล์ซ้ำหมายถึงปลายทาง/รายการ path เดิมในคลัง ชื่อเพลงเหมือนกันแต่คนละรหัสและคนละไฟล์สามารถอยู่ร่วมกันได้

SQLite จัดเก็บ metadata สำหรับค้นหาใน AppData ส่วน **ไฟล์เพลงอยู่ในที่เก็บที่ผู้ใช้เลือก**
เพลงที่เคยเก็บใน Data จากรุ่นก่อนยังอ่านได้ ไม่ย้ายหรือลบให้โดยอัตโนมัติ
หากอัปเกรดแล้วตำแหน่งเดิมอยู่ใน Data จะให้เลือกตำแหน่งใหม่สำหรับการคัดลอกเพลงครั้งต่อไป

ค้นหารหัสเพลง ชื่อ ศิลปิน หรือคีย์ เพิ่มเข้าคิวและลากเรียงได้ คลังคงอยู่หลังเปิดใหม่
หน้าค้นหาแสดงสูงสุด 1,000 รายการ; ข้อมูล MIDI/เนื้อเพลงเต็มโหลดเฉพาะตอนเล่น
ไม่มีเพลงหรือ SoundFont เชิงพาณิชย์รวมมาให้ ต้องแตกชุด NCN จาก ZIP ก่อนนำเข้า

## มิกเซอร์และ VST3

เสียงตาม MIDI Program Change เข้า strip ของ SoundFont program นั้น ไม่ผูกกับหมายเลข MIDI channel
ช่อง MIDI หลายช่องที่ใช้ program เดียวกันรวมเข้าช่องเสียงเดียว โดยเก็บ controller/voice แยกตาม channel
Bank variants ใช้ strip ของ program ร่วมกัน; ชื่อที่แสดงอ้างอิง bank 0
กลอง GM 35–81 แยกอีก 47 ชิ้นในตัวกรอง Drums พร้อม hi-hat choke

ทุกเสียง กลอง บัส และ Master มี FX chain แบบเพิ่มต่อได้ ไม่มีเพดานจำนวนตายตัว
เริ่ม 5 บัส กด **＋ Bus** เพิ่มได้ แต่ละช่องเลือกออก Master/บัส และมี post-fader Send ไป Bus 5
บัสมี FX/gain/pan/mute/solo/meter และออก Master เท่านั้นเพื่อป้องกัน feedback
มี FX ในตัว EQ/Compressor/Gate/Saturation/Delay/Reverb/Limiter/Gain และ VST3 mono/stereo x64
ลากเรียง, duplicate, bypass และบันทึก chain/project ได้ ปลั๊กอิน native editor ใช้ได้บน Windows

จำนวน FX/บัสขึ้นกับ CPU/RAM จริง สแกน VST3 แยก subprocess พร้อม timeout แต่ active DSP อยู่ในแอป
ปลั๊กอินเสียอาจปิดแอปได้ ยังไม่มี VST2/32-bit, AU/AAX, sidechain, PDC, multi-output หรือ CC mapping ครบทุกกรณี

## เนื้อเพลงและ loopMIDI

กด **Lyrics / appearance** ตั้งฟอนต์ ขนาด ตัวหนา สีข้อความ/ไฮไลต์/พื้นหลัง ตำแหน่ง ระยะขอบ
ระยะบรรทัด offset เวลา และระดับทรัพยากร พร้อมตัวอย่าง 2 บรรทัด กด Save แล้วใช้จอ Stage / second display
หน้าตั้งค่าเป็น dark palette; ปุ่มตัวอย่างสีอ่อนใช้ตัวอักษรดำ หน้าต่างเลือกไฟล์ native ใช้สี Windows

สร้างพอร์ตใน loopMIDI → กด MIDI / loopMIDI → Refresh → เลือกพอร์ต → Connect
loopMIDI ส่งข้อมูล MIDI ไม่ส่งเสียงหลัง FX; เลือก external-only หรือ internal+external ได้
ต้องมี Audio output พร้อมทำงานเพื่อเป็น playback clock ไม่มี MIDI input, SysEx หรือ MIDI Clock

## ดาวน์โหลดและติดตั้ง

เลือก [Windows build 0.4.0](https://github.com/cushyam555/cushyam555/actions/runs/41089635094) รุ่น 0.4.0 ที่สำเร็จ
ดาวน์โหลด artifact **BT-Karaoke-Studio-0.4.0-Windows-x64** แตก ZIP แล้วติดตั้ง
`BTKaraoke-0.4.0-Windows-x64-setup.exe` ทับรุ่นเดิมได้ ภายในมี SHA256.txt

มี Safe Mode ใน Start menu และ native launcher แจ้งข้อผิดพลาดตอนเริ่มแอป
ต้องติดตั้งทั้ง BTKaraoke.exe และ BTKaraokeApp.exe; ตัวติดตั้งยังไม่มี code signing
Log: `%LOCALAPPDATA%\BTStudio\BTKaraoke\logs` (`startup.log`, `launcher.log`)

Project schema 3 เก็บมิกเซอร์ บัส FX และ lyric/performance settings พร้อมอ่าน schema 1/2 แบบแปลงและเตือนให้ตรวจมิกซ์
โปรเจกต์เก่าปรับเลขอัตโนมัติเป็น 000–127 และปิดเมโลดี้ที่ CH9 เสมอ
ไฟล์ project อ้างอิง SoundFont/เพลง/ปลั๊กอินภายนอก ไม่ได้แพ็กรวมไฟล์เหล่านั้น

## Build และตรวจสอบ

CMake 3.25+, C++17, Qt 6 Widgets/Sql/Test (หรือ Qt 5.15), Git และ QSQLITE driver
Dependencies ล็อก commit ใน CMakeLists.txt; ไม่มีการปิด TLS

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BTKaraoke bt_tests --parallel 3
ctest --test-dir build --output-on-failure
```

Windows ใช้ x64 MSVC Native Tools, Qt 6.8.3, Inno Setup 6, `packaging/windows.ps1`
และกำหนด QT_ROOT_DIR/VCToolsRedistDir/RUNNER_TEMP เป็นตำแหน่งจริงที่เขียนได้

ทดสอบ parser/NCN, 000–127, CH9 ที่ไม่ตัดช่องอื่น, program-change note-off, FX/bus, VST3 จริง,
lyric interpolation/gap/profile, preflight ที่ไม่เขียนไฟล์, skip/overwrite และ SQLite หลังเปิดใหม่
UI tests กดปุ่ม KEY/TEMPO และทดสอบหน้าถามเริ่มนำเข้า/ข้าม/ทับจริงบนชุดเพลงทดสอบชั่วคราว
Smoke/startup tests ใช้ฐานข้อมูลและ preferences ชั่วคราว ไม่แตะคลังเพลงผู้ใช้
ยังต้องทดสอบเพลง NCN, SoundFont, loopMIDI, จอที่สอง และ VST จริงของผู้ใช้บนพีซี

## ผลตรวจ 0.4.0

[Windows build #17](https://github.com/cushyam555/cushyam555/actions/runs/41089635094)
commit `bfb0b77` พร้อม artifact **BT-Karaoke-Studio-0.4.0-Windows-x64** (43.7 MB)

- Core QtTest 28 รายการผ่าน (รวม init/cleanup) ไม่มีรายการข้าม
- CTest ผ่านครบ 7 ชุด: core, Safe Mode, normal startup, mixer, lyric settings, keyboard controls และ import UI
- UI test กดยืนยันนำเข้า/ข้าม/ทับไฟล์จริง โดยใช้ชุด KAR ชั่วคราว และตรวจ bytes หลังแต่ละคำตอบ
- ตรวจเลข 000/127, อัปเกรดชื่อเดิม, CH9 ที่ไม่ตัดเสียงช่องอื่น, line spacing/profile และ KAR line-marker timing
- ตรวจแพ็กเกจ SQLite, ตัด developer Qt PATH, จำลอง DLL หาย, ติดตั้ง/เปิด/ถอนติดตั้งผ่าน
- Linux Qt 5.15 ผ่านชุดเดียวกัน พร้อมตรวจภาพหน้าตั้งค่าและ renderer จริง

ผลนี้เป็นเครื่องทดสอบ Windows runner ไม่ใช่การรับรอง FPS หรือปลั๊กอินทุกตัวบนพีซีผู้ใช้
โปรดลองชุด NCN, SoundFont และระดับทรัพยากรที่เหมาะกับเครื่องจริง
