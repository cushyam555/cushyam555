# BT Karaoke

แอปคาราโอเกะเดสก์ท็อปสำหรับ Windows 10/11 x64 ดัดแปลงจาก HandyKaraoke 2.4.1
มีธีมมืด แถบปุ่มค้นหา/คิวเพลง/ตั้งค่า แถวคิวเพลงขนาดใหญ่ และคำแนะนำครั้งแรก
รองรับ NCN (MID + LYR + CUR), KAR และ MIDI พร้อม SoundFont; ไม่รองรับ HNK หรือ MP4

## เริ่มใช้งาน

1. ติดตั้งไฟล์ `BTKaraoke-2.4.1-x64-setup.exe` เมื่อ build บน Windows ผ่านแล้ว
2. เปิด BT Karaoke แล้วกด **ตั้งค่า / เพิ่มเพลง**
3. เลือกโฟลเดอร์เพลง NCN ที่มี `Song`, `Lyrics`, `Cursor` หรือโฟลเดอร์ KAR/MIDI
4. กดปรับปรุงฐานข้อมูลเพลง แล้วเลือก SoundFont `.sf2` ในแท็บ SoundFont
5. ระบบเสียงเริ่มต้นใช้ Midi Synthesizer (SoundFont); เลือก MIDI ภายนอกได้ในตั้งค่า
6. พิมพ์ชื่อเพลงเพื่อค้นหา กด Enter เพิ่มคิว ใช้ปุ่มคิวเพลงดูรายการ

ไม่มีเพลงหรือ SoundFont สำหรับใช้งานจริงรวมมาให้ ใช้ไฟล์ที่คุณมีสิทธิ์ใช้งาน
แอปไม่ต้องเชื่อมต่ออินเทอร์เน็ตระหว่างร้องเพลง และไม่อัปเดตจาก HandyKaraoke ต้นฉบับ
การตั้งค่า/ฐานข้อมูลเก็บใน `.BTKaraoke` ภายในโฟลเดอร์ผู้ใช้ แยกจาก HandyKaraoke
เพลงที่เลือกจากโฟลเดอร์ภายนอกยังอยู่ที่เดิม; สำรองไฟล์เพลงและข้อมูลก่อนย้ายเครื่อง

## สร้างตัวติดตั้งบน GitHub Actions

Workflow `.github/workflows/windows.yml` ใช้ Windows 2022 x64, MSVC และ Qt 5.15.2
เมื่อซอร์สอยู่บน GitHub ให้เปิด Actions → Build BT Karaoke for Windows → Run workflow
หากสำเร็จ ดาวน์โหลด artifact `BT-Karaoke-Windows-x64` ซึ่งมีตัวติดตั้งและ SHA256
ขั้นตอนนี้ไม่สร้าง GitHub Release และไม่เผยแพร่ตัวติดตั้งต่อสาธารณะโดยอัตโนมัติ

## สร้างบน Windows เอง

ติดตั้ง Visual Studio Build Tools พร้อม C++ x64, Qt 5.15.2 MSVC2019 x64 และ
Inno Setup 6.3 หรือใหม่กว่า ใช้ x64 Native Tools shell แล้วเพิ่ม Qt `bin` บน PATH
จากโฟลเดอร์ repository รัน:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-windows.ps1
ISCC.exe packaging/bt-x64.iss
```

สคริปต์ build แอป ทดสอบ core integration และเปิด UI smoke test ก่อนจัดชุดไฟล์
ตัวติดตั้งอยู่ที่ `installer-output/BTKaraoke-2.4.1-x64-setup.exe`
ไฟล์โปรแกรมภายในยังชื่อ `HandyKaraoke.exe` เพื่อรักษาความเข้ากันได้กับโปรเจกต์เดิม
ตัวติดตั้งไม่ลงทะเบียนใบรับรอง code signing จึงอาจมีคำเตือน Windows SmartScreen

## การตรวจสอบ

ตรวจสอบบน Linux ด้วย Qt 5.15.15 แล้ว:

- build และ link แอปทั้งโปรเจกต์สำเร็จ
- UI smoke test เปิดหน้าต่างจริงและฐานข้อมูลชั่วคราวสำเร็จ
- QtTest ผ่าน 6 รายการ (รวมเริ่ม/จบชุดทดสอบ): อ่าน MIDI/lyrics, นำเข้า/ค้นหา NCN/KAR
  และ rebuild คลังแบบ KAR อย่างเดียว, สังเคราะห์ข้อมูลเสียงไม่เงียบ, บันทึก/อ่านคิวภาษาไทย
- SoundFont ใน `tests/fixtures` เป็นคลื่น sine ที่สร้างขึ้นสำหรับทดสอบเท่านั้น

ยังต้องยืนยัน Windows workflow และทดสอบฟังเสียงจริง การปรับคีย์ จอที่สอง และการติดตั้ง/
ถอนติดตั้งบน Windows ที่ไม่มี Qt ก่อนแจกจ่ายให้ผู้ใช้ทั่วไป
Windows build รอบที่ 4 ผ่านแล้ว รวม core tests 6 รายการและ UI smoke test
ขั้นตอนตรวจตัวติดตั้งเพิ่มเติมกำลังอยู่ระหว่างยืนยัน

## ใบอนุญาต

ดู `LICENSE` (GPL-3.0) และ `UPSTREAM.md` สำหรับที่มาและการเปลี่ยนแปลง
BASS และไลบรารีอื่นมีเงื่อนไขแยกต่างหาก ต้องตรวจสอบใบอนุญาตก่อนแจกจ่ายเชิงพาณิชย์
