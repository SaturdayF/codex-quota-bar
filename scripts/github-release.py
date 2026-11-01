"""Publish a frozen tag and assets via existing Git credential helper; never logs credentials.

Usage: python scripts/github-release.py v0.1.0 <commit> <zip> <sha256-file>
The tag must already exist on origin. Does not overwrite a published release.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
from urllib import request, error, parse

ROOT = Path(__file__).resolve().parent.parent
REPO = "SaturdayF/codex-quota-bar"

def main():
    tag, commit, *paths = sys.argv[1:]
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
            return json.load(response)
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
