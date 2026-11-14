"""Exercise the native Hub on loopback with synthetic data only (Python stdlib)."""
import argparse
import datetime
import http.client
import json
from pathlib import Path
import socket
import subprocess
import time
import urllib.error
import urllib.request


def run(executable, output):
    output.mkdir(parents=True, exist_ok=False)
    secret = "synthetic-test-secret-only"
    checks = []

    def check(condition, name):
        assert condition, name
        checks.append(name)

    def start():
        (output / "stop").unlink(missing_ok=True)
        (output / "ready.json").unlink(missing_ok=True)
        process = subprocess.Popen(
            [str(executable), "--hub-fixture", "--output", str(output)],
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        deadline = time.monotonic() + 15
        while not (output / "ready.json").exists():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("isolated Hub did not start")
            time.sleep(.05)
        return process, json.loads((output / "ready.json").read_text())["port"]

    process, port = start()
    streams = []

    def request(path, body=None, headers=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}{path}", data=data,
            headers=headers or {"Authorization": f"Bearer {secret}"},
        )
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def raw(headers, body=b""):
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.sendall(headers.encode() + body)
            return int(sock.recv(4096).split(b" ")[1])

    def frame(response):
        lines = []
        while True:
            line = response.readline()
            if line in (b"\n", b"\r\n", b""):
                return b"".join(lines)
            lines.append(line)

    try:
        status, health = request("/api/health", headers={"X-Fixture": "yes"})
        check(status == 200 and health["role"] == "hub", "public health")
        check(request("/api/stats", headers={"X-Fixture": "yes"})[0] == 401,
              "data needs authentication")
        check(request("/api/stats", headers={"X-Token-Monitor-Secret": secret})[0] == 200,
              "legacy secret header")
        body = {"deviceId": "synthetic-mac", "hostname": "Simulated Mac",
                "allTime": {"totalTokens": 1234, "costUsd": .25},
                "historyAvailable": True,
                "history": {"daily": [{"date": datetime.date.today().isoformat(), "tokens": 1234,
                                        "cost": .25}], "monthly": [],
                            "summary": {"activeDaysComplete": False}}}
        check(request("/api/ingest", body)[0] == 200, "synthetic Mac upload")
        check(request("/api/ingest", body)[0] == 200 and
              request("/api/stats")[1]["periods"]["allTime"]["totalTokens"] == 1234,
              "idempotent full snapshot")
        check(request("/api/history")[1]["daily"][0]["tokens"] == 1234,
              "retained History over HTTP")
        for index in range(4):
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            conn.request("GET", "/api/stats/stream", headers={
                "Authorization": f"Bearer {secret}", "X-Token-Monitor-Stream": "4"})
            response = conn.getresponse()
            check(response.status == 200 and b"event: snapshot" in frame(response),
                  f"SSE subscriber {index + 1} gets full v4 fallback")
            streams.append((conn, response))
        check(request("/api/health")[0] == 200, "streams reserve workers for normal HTTP")
        extra = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        extra.request("GET", "/api/stats/stream", headers={"Authorization": f"Bearer {secret}"})
        check(extra.getresponse().status == 503, "SSE capacity is bounded")
        extra.close()
        body["allTime"]["totalTokens"] = 1500
        request("/api/ingest", body)
        update = frame(streams[0][1])
        check(b"event: stats" in update and b"1500" in update,
              "SSE publishes changed aggregate")
        for conn, response in streams:
            response.close()
            conn.close()
        streams.clear()
        common = f"POST /api/ingest HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer {secret}\r\n"
        check(raw(common + "Content-Length: 2097153\r\n\r\n") == 413,
              "2 MiB upload ceiling before body allocation")
        check(raw(common + "Content-Length: 1\r\nContent-Length: 1\r\n\r\nx") == 400,
              "duplicate framing header rejected")
        check(raw(common + "Transfer-Encoding: chunked\r\n\r\n") == 400,
              "unsupported chunked framing rejected")
        check(raw(common + "Content-Length: 1\r\n\r\nx") == 400,
              "malformed JSON rejected")
        check(request("/api/ingest/delta", {})[0] == 404, "delta upload falls back to full snapshot")
        check(request("/api/sync/content")[1]["sessionTitles"]["enabled"] is False,
              "title sharing explicitly disabled")
        (output / "stop").touch()
        process.wait(timeout=12)
        process, port = start()
        check(request("/api/stats")[1]["periods"]["allTime"]["totalTokens"] == 1500,
              "real server restart restores accepted usage")
    finally:
        for conn, response in streams:
            response.close()
            conn.close()
        (output / "stop").touch()
        try:
            process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        (output / "protocol-checks.json").write_text(
            json.dumps({"passed": checks, "count": len(checks)}, indent=2), encoding="utf-8")
    print(f"{len(checks)} HTTP/SSE protocol checks passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    run(args.executable.resolve(), args.output.resolve())
