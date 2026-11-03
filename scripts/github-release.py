"""Publish a frozen tag and assets via existing Git credential helper; never logs credentials.

Usage: python scripts/github-release.py v0.3.2 <commit> <setup> <zip> <sha256-file>
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
    tag, commit, *paths = sys.argv[1:]
    if tag != "v0.3.2":
        raise RuntimeError("Only the current native version can be published")
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
    if tag == "v0.3.2":
        tagged = api(f"/repos/{REPO}/git/ref/tags/{tag}")["object"]
        while tagged["type"] == "tag":
            tagged = api(f"/repos/{REPO}/git/tags/{tagged['sha']}")["object"]
        if tagged["type"] != "commit" or tagged["sha"] != commit:
            raise RuntimeError("Release tag does not point to the expected source commit")
        allowed = {"CodexQuotaBar-v0.3.2-Setup.exe", "CodexQuotaBar-v0.3.2-Portable-win-x64.zip", "SHA256SUMS.txt"}
        if {Path(path).name for path in paths} != allowed:
            raise RuntimeError("Native release requires exact installer, ZIP and checksum asset set")
        body = (ROOT / "docs/releases/v0.3.2.md").read_text(encoding="utf-8").replace("{SOURCE_COMMIT}", commit)
        try:
            release = api(f"/repos/{REPO}/releases/tags/{tag}")
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
            release = api(f"/repos/{REPO}/releases", {"tag_name": tag, "target_commitish": commit,
                "name": "Codex Quota Bar 0.3.2 · Native", "body": body, "draft": True, "prerelease": False})
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
        if {asset["name"] for asset in verified["assets"]} != set(expected):
            raise RuntimeError("Unexpected GitHub release assets; draft retained")
        for name, digest in expected.items():
            if not any(a["name"] == name and a.get("digest") == digest for a in verified["assets"]):
                raise RuntimeError("Native release hash verification failed; draft retained")
        if verified["draft"]:
            verified = api(f"/repos/{REPO}/releases/{release['id']}", {"draft": False}, "PATCH")
        snapshot = {"url": verified["html_url"], "tag": tag, "commit": commit, "draft": verified["draft"],
            "assets": [{k: a[k] for k in ("name", "size", "digest")} for a in verified["assets"]]}
        snapshot_dir = ROOT / "codex_tmp/releases" / tag
        snapshot_dir.mkdir(parents=True, exist_ok=True)
        (snapshot_dir / "github-release.json").write_text(json.dumps(snapshot, indent=2), encoding="utf-8")
        print("Published and SHA256 verified", verified["html_url"], flush=True)
        return

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(type(exc).__name__ + ": " + str(exc), file=sys.stderr)
        sys.exit(1)
