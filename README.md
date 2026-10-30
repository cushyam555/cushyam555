# BT Karaoke Studio 0.1.0

โปรเจกต์ใหม่ที่เขียนขึ้นสำหรับ BT Karaoke โดยไม่ใช้ซอร์สหรือ BASS จาก HandyKaraoke
เวอร์ชันเดิมเก็บใน branch `legacy-handykaraoke` และใช้ใบอนุญาตเดิมของตัวเอง
โค้ดแอปใหม่เป็น MIT; ไลบรารีภายนอกมีใบอนุญาตแยก ดู THIRD_PARTY.md

## รุ่นแรกมีอะไร

- หน้าตาใหม่ธีมมืด คลังเพลง ค้นหา คิวแบบลากเรียง และจอร้องแยก
- อ่าน MIDI format 0/1, KAR และ NCN พร้อม LYR/CUR รวมไดเรกทอรีย่อย
- SoundFont synthesizer แยก 15 MIDI instruments และ 8 กลุ่มกลองก่อนประมวลผล
- Kick, Snare, Closed/Open Hi-hat, Toms, Cymbals, Clap/Rim และ Percussion มี FX Chain แยก
- เพิ่ม FX ซ้ำได้โดยไม่กำหนดเพดานจำนวน ลากเปลี่ยนลำดับ Bypass ลบ และบันทึก Chain Preset
- FX ในตัว: EQ, Compressor, Gate, Saturation, Delay, Reverb, Limiter และ Gain
- Master FX, volume, pan, mute, solo, peak meters, ปุ่ม Audition, เปลี่ยนคีย์และ tempo
- VST3 mono/stereo FX และเครื่องดนตรีที่รับ note events, generic parameter controls,
  native plugin editor บน Windows, บันทึก/เรียกคืน component state
- สแกน VST3 ใน subprocess พร้อม timeout เพื่อกันสแกนค้าง; ปลั๊กอินที่ใช้ประมวลผลจริงอยู่ใน host process
- บันทึก project เป็น JSON พร้อมคิว คลังเพลง มิกเซอร์ FX และ path ของ SoundFont

## เริ่มใช้งาน

1. ดาวน์โหลด artifact จาก [Windows builds](https://github.com/cushyam555/cushyam555/actions/workflows/windows.yml)
   เลือกรอบที่สำเร็จและ artifact `BT-Karaoke-Studio-0.1.0-Windows-x64`
2. แตก ZIP เปิดตัวติดตั้ง `BTKaraoke-0.1.0-Windows-x64-setup.exe`
3. กด SoundFont เลือก `.sf2` ที่มีสิทธิ์ใช้ แล้ว Import เพลง `.mid` หรือ `.kar`
   สำหรับ NCN เลือก MIDI ในโฟลเดอร์ Song โดยมี Lyrics และ Cursor ในโครงสร้างเดียวกัน
4. เพิ่มเพลงเข้าคิว เลือก Mixer & FX แล้วกด ＋ FX ของช่องที่ต้องการ
5. ปลั๊กอิน VST3: เปิด VST3 plugins แล้วสแกนโฟลเดอร์ที่ติดตั้งปลั๊กอิน
   เช่น `C:\Program Files\Common Files\VST3` จากนั้นเพิ่มลงช่องที่ต้องการ

ไม่มีเพลง SoundFont สำหรับใช้งานจริง หรือปลั๊กอินเชิงพาณิชย์รวมมาให้
ไฟล์โปรเจกต์อ้างอิงไฟล์ภายนอก ต้องเก็บเพลง SoundFont และปลั๊กอินไว้ด้วย
ตัวติดตั้งยังไม่มีลายเซ็น code signing

## ขอบเขตเวอร์ชัน 0.1

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
