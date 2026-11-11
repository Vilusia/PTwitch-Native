"""Package only a freshly compiled PTwitch executable and complete notices."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import shutil
import struct
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream/komi-tube'
ENGINE = UPSTREAM / 'vendor/tilefinch'
VERSION = (ROOT / 'native/app/version.h').read_text()
BUILD = int(re.search(r'^#define PTWITCH_BUILD (\d+)$', VERSION, re.M)[1])
BUILD_MARKER = re.search(r'^#define PTWITCH_BUILD_MARKER "([^"]+)"$', VERSION, re.M)[1].encode('ascii')


def verify_pbp(path):
    data = path.read_bytes()
    if len(data) < 40:
        raise ValueError('Truncated EBOOT')
    magic, version, *offsets = struct.unpack_from('<10I', data)
    if magic != 0x50425000 or offsets != sorted(offsets) or offsets[0] < 40 or offsets[-1] > len(data):
        raise ValueError('Invalid PBP section bounds')
    if data[offsets[6]:offsets[6] + 4] != b'\x7fELF':
        raise ValueError('Missing native executable payload')
    if b'PTwitchNative\0' not in data[offsets[6]:offsets[7]]:
        raise ValueError('Executable is not PTwitch; refusing to rebrand another app')
    if BUILD_MARKER not in data[offsets[6]:offsets[7]]:
        raise ValueError(f'Stale executable: rebuild PTwitch build {BUILD} before packaging')
    sfo = data[offsets[0]:offsets[1]]
    smagic, _, keys, values, count = struct.unpack_from('<5I', sfo)
    if smagic != 0x46535000 or count > 128:
        raise ValueError('Invalid PARAM.SFO')
    fields = {}
    for index in range(count):
        key, kind, length, capacity, value = struct.unpack_from('<HHIII', sfo, 20 + 16 * index)
        name = sfo[keys + key:].split(b'\0', 1)[0].decode('ascii')
        fields[name] = sfo[values + value:values + value + length]
    if fields.get('TITLE') != b'PTwitch Native\0' or fields.get('MEMSIZE') != struct.pack('<I', 1):
        raise ValueError('Incorrect app title or 64 MB memory setting')


def main():
    source = ENGINE / 'build-preset-psp/ptwitch-app'
    if not (source / 'EBOOT.PBP').is_file():
        raise SystemExit('Build ptwitch-app-elf first. No PSP executable is available.')
    release = ROOT / 'dist'
    release.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ptwitch-', dir=release) as temp:
        stage = Path(temp)
        app = stage / 'PSP/GAME/PTWITCH_NATIVE'
        app.mkdir(parents=True)
        for name in ['EBOOT.PBP', 'roots.pem', 'fonts/TilefinchSans-Regular.ttf']:
            destination = app / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / name, destination)
        for license in (ENGINE / 'fonts').glob('LICENSE-*'):
            shutil.copy2(license, app / 'fonts' / license.name)
        notices = app / 'NOTICES'
        shutil.copytree(ENGINE / 'third_party/notices', notices)
        for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
            shutil.copy2(ENGINE / name, notices / name)
        shutil.copytree(ENGINE / 'third_party/public_suffix', notices / 'public_suffix')
        for original, destination in [
            (ENGINE / 'third_party/POCKETSPHINX_LICENSE.txt', 'pocketsphinx/POCKETSPHINX_LICENSE.txt'),
            (ENGINE / 'psp-assets/voice-model/en-us/README', 'voice-model/ALPHA_CEPHEI_LICENSE.txt'),
        ]:
            (notices / destination).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, notices / destination)
        (notices / 'fonts').mkdir(exist_ok=True)
        for license in (app / 'fonts').glob('LICENSE-*'):
            shutil.copy2(license, notices / 'fonts' / license.name)
        shutil.copy2(ROOT / 'native/LICENSE-KOMI', notices / 'LICENSE-KOMI')
        for source_doc, destination in [
            ('README.md', 'README.md'), ('docs/TESTING.md', 'TESTING.md'),
            ('native/UPSTREAM.md', 'UPSTREAM.md'),
        ]:
            shutil.copy2(ROOT / source_doc, app / destination)
        # Use upstream's SFO repacker; it preserves the actual ELF payload.
        spec = importlib.util.spec_from_file_location('komi_packaging', UPSTREAM / 'scripts/package.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.set_pbp_title(app / 'EBOOT.PBP', 'PTwitch Native', None, memsize=1)
        verify_pbp(app / 'EBOOT.PBP')
        (app / 'BUILD-INFO.json').write_text(json.dumps({
            'name': 'PTwitch Native', 'build': BUILD,
            'playback': 'Direct Twitch HTTPS/HLS; PSP firmware H.264/AAC',
            'requires_pc_to_watch': False, 'requires_helper_server': False,
            'requires_user_api_key': False, 'hardware_verified': False,
            'live_twitch_verified': False,
            'source_commit': '47bf5677a5254dfc37d8716233e7b1861c51698d',
        }, indent=2) + '\n')
        manifest = (ENGINE / 'cmake/PspNoticesManifest.cmake').read_text()
        for entry in re.findall(r'^\s*((?:NOTICES|slot-a/fonts)/[^\s#)]+)', manifest, re.M):
            if not (app / entry.removeprefix('slot-a/')).is_file():
                raise ValueError('Missing required notice: ' + entry)
        (app / 'SHA256SUMS').write_text('\n'.join(
            f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(app).as_posix()}'
            for p in sorted(app.rglob('*')) if p.is_file()
        ) + '\n')
        archive_path = release / 'PTwitch-native-psp.zip'
        with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
            for p in sorted(app.rglob('*')):
                if p.is_file():
                    archive.write(p, p.relative_to(stage))
        with zipfile.ZipFile(archive_path) as archive:
            if archive.testzip() is not None:
                raise ValueError('Install ZIP CRC validation failed')
        print(archive_path)


if __name__ == '__main__':
    main()
