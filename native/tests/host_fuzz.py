"""Exercise the actual compiled C parser's byte bounds, deterministically."""
import ctypes as c
from pathlib import Path
import random

root = Path(__file__).resolve().parents[2]
lib = c.CDLL(str(root / 'native/test-results/core-test.dll'))
lib.tw_token_url.argtypes = [c.c_char_p, c.c_char_p, c.c_size_t, c.c_void_p]
lib.tw_token_url.restype = c.c_int


class Variant(c.Structure):
    _fields_ = [('url', c.c_char * 2048), ('width', c.c_uint),
                ('height', c.c_uint), ('bandwidth', c.c_uint), ('fps', c.c_uint)]


class GuardedVariant(c.Structure):
    _fields_ = [('before', c.c_uint64), ('value', Variant), ('after', c.c_uint64)]


lib.tw_variant.argtypes = [c.c_char_p, c.c_size_t, c.POINTER(Variant)]
lib.tw_variant.restype = c.c_int
rng = random.Random(20261111)
sentinel = 0x123456789ABCDEF0
for i in range(20000):
    n = rng.randrange(2048)
    data = rng.randbytes(n)
    if i % 4 == 0:
        data = b'#EXTM3U\n' + data
    if i % 4 == 1:
        data = b'{"data":' + data + b'}'
    output = c.create_string_buffer(b'X' * (4096 + 32))
    address = c.addressof(output)
    lib.tw_token_url(b'name', data, len(data), address + 16)
    assert output.raw[:16] == b'X' * 16 and output.raw[4112:4128] == b'X' * 16
    guarded = GuardedVariant(sentinel, Variant(), sentinel)
    lib.tw_variant(data, len(data), c.byref(guarded.value))
    assert guarded.before == sentinel and guarded.after == sentinel
print('PASS: 20,000 deterministic hostile inputs; token/variant output guards intact')
