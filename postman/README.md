# Native Hub contract checks

`collections/native-hub` is a local v3 collection containing eight synthetic
contract assertions. It needs no production credentials or Postman workspace.
Its pre-request guard accepts only an IPv4 loopback origin.

1. Start the development executable with `--hub-fixture --output <new-test-dir>`.
   This binds **127.0.0.1 only**, chooses a free port, disables real log collection
   and uses the literal test credential `synthetic-test-secret-only`.
2. Read the chosen port from `<new-test-dir>/ready.json`.
3. Run `postman collection lint postman/collections/native-hub`.
4. Run `postman collection run postman/collections/native-hub --env-var baseUrl=http://127.0.0.1:<port> --no-report-events`.
5. Create `<new-test-dir>/stop` and wait for the fixture to exit. It also exits
   automatically after 60 seconds.

Use a fresh isolated test directory: the assertions expect no other devices.
Never point this collection at a user's normal Hub. It writes synthetic records.

The independent Python HTTP/SSE checks run with:

```powershell
python tests/HubProtocol.Tests.py <development-executable> <new-test-dir>
```

The native model/client checks run with `--hub-test --output <new-test-dir>`.
Both suites use synthetic records and do not read real login credentials.

The compatibility reference is the pinned
[Token Monitor API](https://github.com/Javis603/token-monitor/blob/8c50fcc9571710dbebf9ca9b56c1c371865ec03a/docs/API.md).
These contract fixtures do not establish interoperability with a specific Mac
installation, firewall or VPN.
