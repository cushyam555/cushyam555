# BT Karaoke Studio 0.2.0

โปรเจกต์ใหม่ที่เขียนขึ้นสำหรับ BT Karaoke โดยไม่ใช้ซอร์สหรือ BASS จาก HandyKaraoke
เวอร์ชันเดิมเก็บใน branch `legacy-handykaraoke` และใช้ใบอนุญาตเดิมของตัวเอง
โค้ดแอปใหม่เป็น MIT; ไลบรารีภายนอกมีใบอนุญาตแยก ดู THIRD_PARTY.md

## ฟีเจอร์

- หน้าตาใหม่ธีมมืด คลังเพลง ค้นหา คิวแบบลากเรียง และจอร้องแยก
- อ่าน MIDI format 0/1, KAR และ NCN พร้อม LYR/CUR รวมไดเรกทอรีย่อย
- SoundFont synthesizer แยก 15 MIDI instruments และกลอง GM ทุกโน้ต 35–81 รวม 47 ชิ้นก่อนประมวลผล
- FX Chain แยกทั้ง 62 ช่อง, บัส A–F และ Master รวม 69 strips
- เลือกส่งแต่ละช่องเข้า Master หรือบัสกลุ่มใดก็ได้; ค่าเริ่มต้นกลองเข้า Bus A
- Post-fader Send ไป Bus F สำหรับใช้ Reverb/Delay ร่วมกัน (ตั้ง FX ให้มี wet 100% เมื่อต้องการ return ล้วน)
- บัสมี FX, gain, pan, mute, solo และ meter ของตัวเอง; บัสออก Master เท่านั้นเพื่อป้องกัน feedback loop
- แสดง FX บน strip, คลิกขวาชื่อช่อง/บัสเพื่อเปลี่ยนชื่อ, กรอง Instruments / Drums / Buses
- ปิดเมโลดี้ได้พร้อมเลือก MIDI channel 1–16 (เริ่มต้น channel 4)
- นำเข้า NCN ทั้งโฟลเดอร์พร้อมโฟลเดอร์ย่อย, ข้ามเพลงซ้ำ, ยกเลิกระหว่างนำเข้าได้
- Windows MIDI output ไป loopMIDI หรือ MIDI device; เลือก external-only หรือเสียงในแอปพร้อม MIDI ได้
- Panic สำหรับหยุดโน้ตค้าง; การ stop/seek/เปลี่ยนคีย์/ตัดเมโลดี้ส่ง MIDI reset ด้วย
- เพิ่ม FX ซ้ำได้โดยไม่กำหนดเพดานจำนวน ลากเปลี่ยนลำดับ Bypass ลบ และบันทึก Chain Preset
- FX ในตัว: EQ, Compressor, Gate, Saturation, Delay, Reverb, Limiter และ Gain
- Master FX, volume, pan, mute, solo, peak meters, ปุ่ม Audition, เปลี่ยนคีย์และ tempo
- VST3 mono/stereo FX และเครื่องดนตรีที่รับ note events, generic parameter controls,
  native plugin editor บน Windows, บันทึก/เรียกคืน component state
- สแกน VST3 ใน subprocess พร้อม timeout เพื่อกันสแกนค้าง; ปลั๊กอินที่ใช้ประมวลผลจริงอยู่ใน host process
- บันทึก project เป็น JSON พร้อมคิว คลังเพลง มิกเซอร์ FX และ path ของ SoundFont

## เริ่มใช้งาน

1. ดาวน์โหลด artifact จาก [Windows builds](https://github.com/cushyam555/cushyam555/actions)
   เลือกรอบที่สำเร็จและ artifact `BT-Karaoke-Studio-0.2.0-Windows-x64`
2. แตก ZIP เปิดตัวติดตั้ง `BTKaraoke-0.2.0-Windows-x64-setup.exe`
3. กด SoundFont เลือก `.sf2` ที่มีสิทธิ์ใช้ แล้ว Import เพลง `.mid` หรือ `.kar`
   สำหรับ NCN กด **NCN folder…** เลือกโฟลเดอร์รากที่มี Song/Lyrics/Cursor
   หรือ Import เลือก `.mid`, `.lyr`, `.cur` ที่ตรงกันได้ ต้องเก็บโครงสร้างและชื่อไฟล์เดิม
   `.lyr`/`.cur` ไม่ใช่ไฟล์เสียง ต้องมี MIDI คู่กัน ไม่ได้อ่านไฟล์ HNK หรือฐานข้อมูล NCN ที่เข้ารหัส
4. เพิ่มเพลงเข้าคิว เลือก Mixer & FX แล้วกด ＋ FX ของช่องที่ต้องการ
5. ปลั๊กอิน VST3: เปิด VST3 plugins แล้วสแกนโฟลเดอร์ที่ติดตั้งปลั๊กอิน
   เช่น `C:\Program Files\Common Files\VST3` จากนั้นเพิ่มลงช่องที่ต้องการ

ไม่มีเพลง SoundFont สำหรับใช้งานจริง หรือปลั๊กอินเชิงพาณิชย์รวมมาให้
ไฟล์โปรเจกต์อ้างอิงไฟล์ภายนอก ต้องเก็บเพลง SoundFont และปลั๊กอินไว้ด้วย
ตัวติดตั้งยังไม่มีลายเซ็น code signing

## ตั้งค่า loopMIDI

1. เปิด loopMIDI แล้วสร้างพอร์ต เช่น `BT Karaoke Out`
2. ใน BT Karaoke กด **MIDI / loopMIDI** → Refresh ports → เลือกพอร์ต → Connect
3. ให้โปรแกรมเสียงภายนอกเปิดพอร์ตเดียวกันเป็น MIDI input และเลือกเสียงที่ต้องการ
4. เลือก External MIDI only เพื่อปิดเสียงภายใน หรือ Internal audio + external MIDI เพื่อใช้ทั้งคู่

loopMIDI ส่งข้อมูลโน้ต/Program Change/CC/Pitch Bend ไม่ใช่เสียง ไม่ส่ง FX, fader, pan,
Bus audio หรือ SoundFont ของ BT Karaoke ไปด้วย การเปลี่ยนคีย์/เมโลดี้และการกรอง note-on
ตาม mute/solo ใช้กับ MIDI output ด้วย ต้องตั้งเสียงและ FX ของปลายทางเอง
ใช้ audio device ของ Windows เป็น clock จึงต้องเริ่ม Audio ได้ก่อนเล่นผ่าน MIDI output
Pause/การเปลี่ยน mute/solo/reset จะตัดโน้ตภายนอกที่ค้างอยู่; sustained notes จะเริ่มอีกครั้งเมื่อมี note-on ใหม่
MIDI SysEx, MIDI Clock, per-part output ports และ MIDI input ยังไม่รองรับ
ไม่ได้เปิดพอร์ตอัตโนมัติเมื่อโหลดโปรเจกต์ ให้เชื่อมต่อพอร์ตใหม่ในแต่ละ session

## ขอบเขตเวอร์ชัน 0.2

รองรับ VST3 x64 แบบ mono/stereo เท่านั้น ไม่รองรับ VST2, AU, AAX, sidechain,
multichannel buses หรือการชดเชย latency ข้ามช่องโดยอัตโนมัติ ยังไม่รับประกันปลั๊กอินทุกผู้ผลิต
VST3 instruments รับ note on/off ในรุ่นนี้; MIDI controller mapping และ dynamic I/O restart
ยังไม่ครอบคลุมทุกกรณี ปลั๊กอินเสียอาจทำให้แอปปิดได้ระหว่างประมวลผลจริง
การเพิ่ม FX ไม่จำกัดจำนวนตายตัว แต่ขึ้นอยู่กับ RAM และกำลัง CPU
Reverb ในตัวเป็นเอฟเฟกต์พื้นฐาน รุ่นแรกยังไม่มี MP4/MP3, ไมโครโฟน, recording, QR requests

## Build และทดสอบ

CMake 3.25+, C++17, Qt 6 Widgets/Test (หรือ Qt 5.15) และ Git
Dependency commits ถูกล็อกใน CMakeLists.txt; ไม่ปิด TLS หรือการตรวจสอบแพ็กเกจ

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BTKaraoke bt_tests --parallel 3
ctest --test-dir build --output-on-failure
```

Windows ใช้ x64 MSVC Native Tools shell, Qt 6.8.3 บน PATH, QT_ROOT_DIR และ Inno Setup 6:

```powershell
./packaging/windows.ps1
```

Tests ตรวจ MIDI/NCN, ไฟล์เสีย, routing กลอง, FX chain 128 ตัว, bypass/order,
SoundFont output และโหลด VST3 integration plugin จริงพร้อม parameter/state round-trip
`--smoke-test` ตรวจการเปิดหน้าจอโดยไม่เปิด audio device
ยังต้องทดสอบฟังเสียง จอที่สอง และปลั๊กอินของผู้ใช้บนพีซีจริง

## ผลตรวจรุ่น 0.1.0

Windows build ที่ผ่าน: [รอบ #9](https://github.com/cushyam555/cushyam555/actions/runs/41030169235)
commit ที่ตรวจ: `2ffd246` ใช้ Qt 6.8.3 และ MSVC x64

- CTest/QtTest ผ่าน 14 รายการ (12 กรณีทำงาน + เริ่ม/จบชุดทดสอบ) ไม่มีรายการข้าม
- โหลด VST3 integration plugin จริง ประมวลผล gain ปรับ parameter, bypass และเรียกคืน state
- SF2 ในพาธภาษาไทย, NCN/TIS-620/CUR, pause/resume และ hi-hat choke ผ่าน
- จัดชุด DLL, สร้างตัวติดตั้ง, เปิดแอปโดยไม่ใช้ Qt ภายนอก, ติดตั้งและถอนติดตั้งผ่าน
- artifact `BT-Karaoke-Studio-0.1.0-Windows-x64` ขนาด 42.7 MB มี setup และ SHA256
- Linux/Qt 5.15.15 build, ชุดทดสอบเดียวกัน และ UI smoke ผ่านด้วย

ยังไม่ใช่การรับรองปลั๊กอินเชิงพาณิชย์ทุกตัว และยังไม่ได้ฟังเสียง/ใช้จอที่สองบนพีซีของผู้ใช้จริง
การทดสอบ UI ใช้ `--smoke-test` โดยไม่เปิด audio device; การสังเคราะห์เสียงทดสอบด้วย PCM output

สคริปต์ `packaging/windows.ps1` ออกแบบสำหรับ GitHub Actions เป็นหลัก หากเรียกเอง
ต้องกำหนด `QT_ROOT_DIR`, `VCToolsRedistDir` และ `RUNNER_TEMP` ให้เป็นโฟลเดอร์ที่มีอยู่
และเขียนได้ พร้อมใช้ x64 Native Tools shell

## แก้การเปิดแล้วไม่มีหน้าต่าง (0.1.1)

- แสดงหน้าต่างก่อนตรวจอุปกรณ์เสียง
- ตรวจไดรเวอร์เสียงใน subprocess พร้อม timeout แล้วเริ่ม audio engine เบื้องหลัง
- หากเสียงเริ่มไม่ได้ หน้าต่างยังใช้ได้ และกด Audio เพื่อลองใหม่ได้
- Start menu มี **BT Karaoke Studio - Safe Mode** สำหรับเปิดโดยไม่เริ่มเสียงอัตโนมัติ
- มี native launcher ซึ่งไม่พึ่ง Qt และแจ้งรหัสข้อผิดพลาดเมื่อโปรแกรมเริ่มไม่ได้
- ไฟล์ `BTKaraoke.exe` เป็น launcher; ตัวแอปคือ `BTKaraokeApp.exe` ต้องติดตั้งครบทั้งแพ็กเกจ
- Log อยู่ที่ `%LOCALAPPDATA%\BTStudio\BTKaraoke\logs` (`launcher.log` และ `startup.log`)

Windows CI ตรวจทั้งการเปิดแบบปกติที่ผ่านเส้นทางเริ่มระบบเสียง และ Safe Mode
รวมถึงตรวจว่า launcher รายงานความล้มเหลวเมื่อ Qt DLL หาย ไม่ใช่เฉพาะการเปิดแบบออฟไลน์เดิม

### ผลตรวจ 0.1.1

[Windows build #11](https://github.com/cushyam555/cushyam555/actions/runs/41061244559)
commit `863ebcd` ผ่านแล้ว พร้อม artifact `BT-Karaoke-Studio-0.1.1-Windows-x64` (42.8 MB)

- core tests 16 รายการผ่าน ไม่มีรายการข้าม
- CTest ตรวจ core, Safe Mode และ normal startup ผ่านทั้ง 3 ชุด
- โหมดปกติเปิดหน้าต่างได้แม้การตรวจเสียงคืนข้อผิดพลาด (เครื่องทดสอบไม่มีอุปกรณ์เสียง)
- launcher ตรวจพบการโหลดล้มเหลวเมื่อจำลอง Qt DLL หาย โดยไม่ไปใช้ Qt จาก PATH เครื่องพัฒนา
- ติดตั้งแพ็กเกจแล้วเปิดทั้งแบบปกติและ Safe Mode และถอนติดตั้งผ่าน

ผลนี้เป็น Windows runner ไม่ใช่การทดสอบบนเครื่อง Windows 10 21H2 ของผู้ใช้โดยตรง
หากยังเปิดไม่ได้ ให้ใช้ Safe Mode จาก Start menu และแจ้งรหัสจากหน้าต่างข้อผิดพลาด
หรือส่งท้ายไฟล์ `startup.log` / `launcher.log` ในโฟลเดอร์ log ที่ระบุข้างต้น

## โปรเจกต์เดิมและการตรวจรุ่น 0.2.0

อ่านโปรเจกต์ schema 1 จาก 0.1.x ได้ โดยคง Master เดิมและคัดลอก FX ของกลุ่มกลองเดิม
ไปยังช่องโน้ตที่แยกใหม่; บันทึกใหม่เป็น schema 2 (แอปรุ่นเก่าเปิด schema 2 ไม่ได้)
การแยก dynamics จากกลุ่มไปเป็นชิ้นอาจทำให้เสียงต่างจากรุ่นเก่า ให้ปรับมิกซ์แล้วบันทึกเป็นไฟล์ใหม่

ชุดทดสอบเพิ่มการแยกกลอง GM ครบ 47 โน้ต, ผลรวมสัญญาณบัส/Send/FX/mute/solo,
การป้องกัน routing วน, ปิดเมโลดี้, external-only silence, state round-trip และ migration
ยังต้องทดสอบ loopMIDI/โปรแกรมเสียง/ปลั๊กอินของผู้ใช้บน Windows จริง
