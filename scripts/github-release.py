"""Publish a frozen tag and assets via existing Git credential helper; never logs credentials.

Usage: python scripts/github-release.py v0.1.0 <commit> <zip> <sha256-file>
The tag must already exist on origin. Does not overwrite a published release.
"""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
from urllib import request, error, parse

ROOT = Path(__file__).resolve().parent.parent
REPO = "SaturdayF/codex-quota-bar"

def main():
    refresh = len(sys.argv) > 1 and sys.argv[1] == "--refresh-existing"
    tag, commit, *paths = sys.argv[2:] if refresh else sys.argv[1:]
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never")
    credential = subprocess.run(["git", "-c", f"safe.directory={ROOT.as_posix()}", "credential", "fill"],
        input="protocol=https\nhost=github.com\n\n", text=True, capture_output=True, env=env, check=True)
    fields = dict(line.split("=", 1) for line in credential.stdout.splitlines() if "=" in line)
    token = fields.get("password")
    if not token:
        raise RuntimeError("GitHub credential helper did not provide a token")
    def api(url, data=None, method=None, content_type="application/json"):
        if url.startswith("/"):
            url = "https://api.github.com" + url
        body = json.dumps(data).encode() if isinstance(data, dict) else data
        headers = {"Authorization": "Bearer " + token, "User-Agent": "CodexQuotaBar-Release",
                   "Accept": "application/vnd.github+json", "Content-Type": content_type}
        with request.urlopen(request.Request(url, data=body, headers=headers, method=method), timeout=180) as response:
            payload = response.read()
            return json.loads(payload) if payload else None
    if not refresh and tag == "v0.2.0":
        allowed = {"CodexQuotaBar-v0.2.0-Setup.exe", "CodexQuotaBar-v0.2.0-Portable-win-x64.zip", "SHA256SUMS.txt"}
        if {Path(path).name for path in paths} != allowed:
            raise RuntimeError("Native release requires exact installer, ZIP and checksum asset set")
        body = (ROOT / "docs/releases/v0.2.0.md").read_text(encoding="utf-8").replace("{SOURCE_COMMIT}", commit)
        try:
            release = api(f"/repos/{REPO}/releases/tags/{tag}")
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
            release = api(f"/repos/{REPO}/releases", {"tag_name": tag, "target_commitish": commit,
                "name": "Codex Quota Bar 0.2.0 · Native", "body": body, "draft": True, "prerelease": False})
        if release["target_commitish"] != commit:
            raise RuntimeError("Native release commit does not match")
        expected = {}
        for item in paths:
            file = Path(item)
            digest = "sha256:" + hashlib.sha256(file.read_bytes()).hexdigest()
            expected[file.name] = digest
            existing = next((a for a in release["assets"] if a["name"] == file.name), None)
            if existing:
                if existing.get("digest") != digest:
                    raise RuntimeError("Native asset already exists with different content; no overwrite")
                continue
            if not release["draft"]:
                raise RuntimeError("Published native release is immutable")
            upload = release["upload_url"].split("{")[0] + "?name=" + parse.quote(file.name)
            api(upload, file.read_bytes(), "POST", "application/octet-stream")
            print("Uploaded", file.name, flush=True)
        verified = api(f"/repos/{REPO}/releases/{release['id']}")
        for name, digest in expected.items():
            if not any(a["name"] == name and a.get("digest") == digest for a in verified["assets"]):
                raise RuntimeError("Native release hash verification failed; draft retained")
        if verified["draft"]:
            verified = api(f"/repos/{REPO}/releases/{release['id']}", {"draft": False}, "PATCH")
        snapshot = {"url": verified["html_url"], "tag": tag, "commit": commit, "draft": verified["draft"],
            "assets": [{k: a[k] for k in ("name", "size", "digest")} for a in verified["assets"]]}
        (ROOT / "codex_tmp/native/github-release.json").write_text(json.dumps(snapshot, indent=2), encoding="utf-8")
        print("Published and SHA256 verified", verified["html_url"], flush=True)
        return
    if refresh:
        if tag != "v0.1.0-installer.1":
            raise RuntimeError("Existing-release refresh is authorized only for the 0.1.0 installer supplement")
        release = api(f"/repos/{REPO}/releases/tags/v0.1.0")
        allowed = {"CodexQuotaBar-v0.1.0-Setup.exe", "CodexQuotaBar-v0.1.0-Portable-win-x64.zip", "SHA256SUMS-installer.txt"}
        if {Path(path).name for path in paths} != allowed:
            raise RuntimeError("Unexpected replacement asset set")
        expected = {}
        for item in paths:
            file = Path(item)
            digest = "sha256:" + hashlib.sha256(file.read_bytes()).hexdigest()
            expected[file.name] = digest
            existing = next((a for a in release["assets"] if a["name"] == file.name), None)
            if existing:
                if existing.get("digest") != digest:
                    raise RuntimeError("Existing new asset differs; review before replacement")
                continue
            upload = release["upload_url"].split("{")[0] + "?name=" + parse.quote(file.name)
            api(upload, file.read_bytes(), "POST", "application/octet-stream")
            print("Uploaded", file.name, flush=True)
        confirmed = api(f"/repos/{REPO}/releases/{release['id']}")
        for name, digest in expected.items():
            if not any(a["name"] == name and a.get("digest") == digest for a in confirmed["assets"]):
                raise RuntimeError("New asset digest verification failed; old downloads retained")
        # User explicitly requested smaller packages to be republished. Only remove the
        # exact original large ZIP and checksum after all replacement assets are verified.
        originals = {"CodexQuotaBar-v0.1.0-win-x64.zip": "sha256:c567760f2721b4c01b3239909e3677b9075195d95cf1952cb6e62fbfd690a8aa",
                     "SHA256SUMS.txt": "sha256:792cda034f0dbeab45270c84ce7dd1f870d960c9518d3b6608095b9436d75f50"}
        for asset in confirmed["assets"]:
            if asset["name"] in originals:
                if asset.get("digest") != originals[asset["name"]]:
                    raise RuntimeError("Original asset changed unexpectedly; no deletion")
                api(f"/repos/{REPO}/releases/assets/{asset['id']}", method="DELETE")
                print("Replaced old asset", asset["name"], flush=True)
        body = (ROOT / "docs/releases/v0.1.0-installer.md").read_text(encoding="utf-8").replace("{SOURCE_COMMIT}", commit)
        updated = api(f"/repos/{REPO}/releases/{release['id']}", {"body": body}, "PATCH")
        print("Updated", updated["html_url"], flush=True)
        return
    try:
        release = api(f"/repos/{REPO}/releases/tags/{tag}")
        if not release["draft"]:
            raise RuntimeError("Release already published; never replace frozen assets")
        if release["target_commitish"] != commit:
            raise RuntimeError("Existing draft points at a different commit")
    except error.HTTPError as exc:
        if exc.code != 404:
            raise
        body = (f"首个封存版本 **{tag}**。\n\n"
            "- ChatGPT / Codex 自动跟随，多屏任务栏额度展示。\n"
            "- 七种外观、独立颜色、大小、余量与填充动画。\n"
            "- 悬浮详情、五档刷新间隔、设置与确认后卸载。\n"
            "- 白底相扣双环应用图标，图文说明与动画演示。\n\n"
            f"源码提交：`{commit}`。后续修改进入下个版本，不替换本版本。\n\n"
            "下载 ZIP 并完整解压，运行 install.ps1；SHA256SUMS.txt 用于校验 ZIP。\n"
            "包内附带所需运行时，无需全局安装 .NET。程序尚未代码签名。\n\n"
            "已通过本机原生双屏交互和离线检查；卸载在隔离目录验证，真实服务端重置未被人为触发。")
        release = api(f"/repos/{REPO}/releases", {"tag_name": tag, "target_commitish": commit,
            "name": "Codex Quota Bar " + tag, "body": body, "draft": True, "prerelease": False})
    for item in paths:
        file = Path(item)
        if any(asset["name"] == file.name for asset in release["assets"]):
            raise RuntimeError("Asset exists in draft; review before retrying")
        upload = release["upload_url"].split("{")[0] + "?name=" + parse.quote(file.name)
        api(upload, file.read_bytes(), "POST", "application/zip" if file.suffix == ".zip" else "text/plain")
        print("Uploaded", file.name, flush=True)
    published = api(f"/repos/{REPO}/releases/{release['id']}", {"draft": False}, "PATCH")
    print("Published", published["html_url"], flush=True)

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(type(exc).__name__ + ": " + str(exc), file=sys.stderr)
        sys.exit(1)
