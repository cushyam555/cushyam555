"""Generate an original sine-wave SF2 fixture; contains no third-party audio."""
import math
import struct
from pathlib import Path

def chunk(name, data):
    return name + struct.pack('<I', len(data)) + data + b'\0' * (len(data) % 2)
def name(s):
    return s.encode('ascii').ljust(20, b'\0')
def generate():
    count = 22050
    samples = b''.join(struct.pack('<h', int(12000 * math.sin(2 * math.pi * 261.625565 * i / 44100))) for i in range(count))
    info = chunk(b'LIST', b'INFO' + chunk(b'ifil', struct.pack('<HH', 2, 1)) + chunk(b'isng', b'EMU8000\0') + chunk(b'INAM', b'BT Test Tone\0'))
    sdta = chunk(b'LIST', b'sdta' + chunk(b'smpl', samples + bytes(92)))
    phdr = name('BT Test') + struct.pack('<HHHIII', 0, 0, 0, 0, 0, 0) + name('EOP') + struct.pack('<HHHIII', 0, 0, 1, 0, 0, 0)
    inst = name('Sine') + struct.pack('<H', 0) + name('EOI') + struct.pack('<H', 1)
    shdr = name('C4 sine') + struct.pack('<IIIIIBbHH', 0, count, 0, count, 44100, 60, 0, 0, 1)
    shdr += name('EOS') + bytes(26)
    tables = [(b'phdr', phdr), (b'pbag', struct.pack('<HHHH', 0, 0, 1, 0)), (b'pmod', bytes(10)), (b'pgen', struct.pack('<HHHH', 41, 0, 0, 0)), (b'inst', inst), (b'ibag', struct.pack('<HHHH', 0, 0, 1, 0)), (b'imod', bytes(10)), (b'igen', struct.pack('<HHHH', 53, 0, 0, 0)), (b'shdr', shdr)]
    return chunk(b'RIFF', b'sfbk' + info + sdta + chunk(b'LIST', b'pdta' + b''.join(chunk(k, v) for k, v in tables)))
if __name__ == '__main__':
    Path(__file__).with_name('test-tone.sf2').write_bytes(generate())
