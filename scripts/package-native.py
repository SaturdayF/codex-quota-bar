"""Package the native Windows app; no .NET/VC redistributable is shipped or required."""
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent.parent
VERSION = "0.2.2"
payload = ROOT / "artifacts/native-win-x64"
release = ROOT / "artifacts/releases" / f"v{VERSION}"
release.mkdir(parents=True, exist_ok=True)
archive = release / f"CodexQuotaBar-v{VERSION}-Portable-win-x64.zip"
allowed = {"CodexQuotaBar.exe", "version.json", "LICENSE", "LICENSE-json.txt", "README.md", "CHANGELOG.md"}
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
    for name in sorted(allowed):
        file = payload / name
        if not file.is_file():
            raise RuntimeError(f"Missing payload: {name}")
        output.write(file, name)
with zipfile.ZipFile(archive) as check:
    assert set(check.namelist()) == allowed
    assert check.testzip() is None
files = [release / f"CodexQuotaBar-v{VERSION}-Setup.exe", archive]
if all(p.is_file() for p in files):
    (release / "SHA256SUMS.txt").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files), encoding="ascii")
print(f"Native ZIP: {archive.name}: {archive.stat().st_size:,} bytes")
