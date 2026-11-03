"""Package the native Windows app; no .NET/VC redistributable is shipped or required."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent.parent
VERSION = "0.3.1"
payload = ROOT / "artifacts/native-win-x64"
if json.loads((payload / "version.json").read_text(encoding="utf-8-sig")).get("version") != VERSION:
    raise RuntimeError("Native runtime version does not match package version")
release = ROOT / "artifacts/releases" / f"v{VERSION}"
release.mkdir(parents=True, exist_ok=True)
archive = release / f"CodexQuotaBar-v{VERSION}-Portable-win-x64.zip"
allowed = {"CodexQuotaBar.exe", "version.json", "LICENSE", "LICENSE-json.txt", "README.md", "CHANGELOG.md"}
(payload / "portable.json").write_text(json.dumps({"product":"CodexQuotaBar","version":VERSION}), encoding="utf-8")
import shutil
shutil.copyfile(ROOT / "scripts/portable-uninstall.ps1", payload / "portable-uninstall.ps1")
allowed.update({"portable.json", "portable-uninstall.ps1"})
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
