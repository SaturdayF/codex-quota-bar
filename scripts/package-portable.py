"""Small framework-dependent ZIP; requires globally installed .NET 10 Desktop Runtime."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parent.parent
source = root / "artifacts/portable-win-x64"
output = root / "artifacts/releases/v0.1.0/CodexQuotaBar-v0.1.0-Portable-win-x64.zip"
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in sorted(source.rglob("*")):
        if path.is_file() and path.suffix != ".pdb":
            assert "runtime" not in path.relative_to(source).parts
            archive.write(path, path.relative_to(source).as_posix())
print(output, output.stat().st_size)
