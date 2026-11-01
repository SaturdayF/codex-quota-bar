"""Freeze the published directory as a ZIP plus SHA-256 file; never overwrite an archive."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parent.parent
version = ET.parse(root / "version.props").findtext("PropertyGroup/Version")
source = root / "artifacts/win-x64"
assert json.loads((source / "version.json").read_text(encoding="utf-8-sig"))["version"] == version
destination = root / "artifacts/releases" / ("v" + version)
destination.mkdir(parents=True, exist_ok=True)
archive = destination / f"CodexQuotaBar-v{version}-win-x64.zip"
if archive.exists():
    raise FileExistsError("Frozen archive already exists; use the next version")
with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
    for path in sorted(source.rglob("*")):
        if path.is_file() and path.suffix.lower() != ".pdb":
            output.write(path, path.relative_to(source).as_posix())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(destination / "SHA256SUMS.txt").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
print(archive)
print("SHA256", digest)
