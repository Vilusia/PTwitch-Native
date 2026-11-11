"""Make a standalone source handoff, excluding builds and user data."""
from pathlib import Path
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[1]
out = root / 'dist/PTwitch-native-source.zip'
out.parent.mkdir(exist_ok=True)
files = [root / 'README.md', root / '.gitignore']
for folder in ['native', 'scripts', 'docs']:
    for path in (root / folder).rglob('*'):
        if path.is_file() and 'test-results' not in path.parts and '__pycache__' not in path.parts:
            files.append(path)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(files):
        archive.write(path, path.relative_to(root))
    archive.writestr('SOURCE-MANIFEST.json', json.dumps({
        str(p.relative_to(root)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(files)
    }, indent=2) + '\n')
with zipfile.ZipFile(out) as archive:
    if archive.testzip() is not None:
        raise SystemExit('Source ZIP CRC validation failed')
print(out)
