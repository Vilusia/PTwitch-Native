"""Exercise version/identity checks against a local compiled PBP fixture.
The modified fixture is only for validation, never an installable executable.
"""
from pathlib import Path
import importlib.util
import re
import struct
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ptwitch_package', root / 'native/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
source = root / 'upstream/komi-tube/vendor/tilefinch/build-preset-psp/ptwitch-app/EBOOT.PBP'
data = source.read_bytes()
old = re.search(rb'PTwitch experimental build [^\0]+', data)[0]
with tempfile.TemporaryDirectory(dir=root / 'native/test-results') as temp:
    fixture = Path(temp) / 'validation-only.pbp'
    fixture.write_bytes(data.replace(old, b'X' * len(old)))
    try:
        package.verify_pbp(fixture)
    except ValueError as error:
        assert str(error) == f'Stale executable: rebuild PTwitch build {package.BUILD} before packaging'
    else:
        raise AssertionError('Packager accepted stale build')
    replacement = package.BUILD_MARKER.ljust(len(old), b'\0')
    current = bytearray(data.replace(old, replacement))
    # Keep PBP payload/PSAR section bounds valid if a diagnostic suffix grows.
    psar = struct.unpack_from('<I', data, 36)[0]
    struct.pack_into('<I', current, 36, psar + len(replacement) - len(old))
    fixture.write_bytes(current)
    sfo_spec = importlib.util.spec_from_file_location('komi_package', root / 'upstream/komi-tube/scripts/package.py')
    sfo = importlib.util.module_from_spec(sfo_spec)
    sfo_spec.loader.exec_module(sfo)
    sfo.set_pbp_title(fixture, 'PTwitch Native', None, memsize=1)
    package.verify_pbp(fixture)
    fixture.write_bytes(fixture.read_bytes().replace(b'PTwitchNative\0', b'WrongAppName!\0'))
    try:
        package.verify_pbp(fixture)
    except ValueError as error:
        assert 'not PTwitch' in str(error)
    else:
        raise AssertionError('Packager accepted wrong module identity')
print('PASS: shared version marker, stale build and module identity checks')
