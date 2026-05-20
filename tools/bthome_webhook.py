#!/usr/bin/env python3
"""Minimal webhook receiver for inspecting BTHome packets forwarded from a
phone BLE-scanner app.

Listens for HTTP POSTs and dumps method, path, headers and body. If the body
is JSON it is pretty-printed; if a BTHome service-data blob (UUID 0xFCD2) can
be located in the payload, it is decoded.

No dependencies. Run:
    python tools/bthome_webhook.py            # listens on 0.0.0.0:8080
    python tools/bthome_webhook.py --port 9000

Then point the phone app's webhook at http://<this-pc-ip>:8080/
"""

from __future__ import annotations

import argparse
import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# BTHome object IDs -> (name, signed, size_bytes, factor)
BTHOME_OBJECTS = {
    0x00: ("packet_id", False, 1, 1),
    0x01: ("battery_%", False, 1, 1),
    0x0C: ("voltage_V", False, 2, 0.001),
    0x14: ("moisture_%", False, 2, 0.01),
    0x02: ("temperature_C", True, 2, 0.01),
    0x03: ("humidity_%", False, 2, 0.01),
}


def decode_bthome(svc: bytes) -> list[str]:
    """Decode a BTHome v2 service-data value: [uuid lo][uuid hi][devinfo][objs...]."""
    out: list[str] = []
    if len(svc) < 3 or svc[0] != 0xD2 or svc[1] != 0xFC:
        return out
    out.append(f"device_info=0x{svc[2]:02X}")
    i = 3
    while i < len(svc):
        obj = svc[i]
        i += 1
        name, signed, size, factor = BTHOME_OBJECTS.get(obj, (f"obj_0x{obj:02X}", False, 1, 1))
        if i + size > len(svc):
            out.append(f"{name}=<truncated>")
            break
        raw = int.from_bytes(svc[i:i + size], "little", signed=signed)
        i += size
        val = raw * factor if factor != 1 else raw
        out.append(f"{name}={val}")
    return out


def find_bthome(blob: bytes) -> list[str]:
    """Scan a raw byte blob for a 0x16 service-data AD element with UUID 0xFCD2."""
    results: list[str] = []
    for m in re.finditer(b"\x16\xd2\xfc", blob):
        start = m.start() + 1  # skip the 0x16 AD type, keep uuid..
        results.extend(decode_bthome(blob[start:]))
    return results


class Handler(BaseHTTPRequestHandler):
    def _dump(self, method: str) -> None:
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else b""

        print("=" * 60)
        print(f"{method} {self.path}")
        for k, v in self.headers.items():
            print(f"  {k}: {v}")

        text = body.decode("utf-8", errors="replace")
        try:
            parsed = json.loads(text)
            print("body (json):")
            print(json.dumps(parsed, indent=2))
        except json.JSONDecodeError:
            print(f"body ({len(body)} bytes):")
            print(f"  text: {text}")
            print(f"  hex:  {body.hex()}")
            decoded = find_bthome(body)
            if decoded:
                print("  BTHome:", ", ".join(decoded))

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")

    def do_POST(self) -> None:
        self._dump("POST")

    def do_GET(self) -> None:
        self._dump("GET")

    def log_message(self, *_args) -> None:
        pass  # we do our own logging


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    args = ap.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Listening on http://{args.host}:{args.port}/  (Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
