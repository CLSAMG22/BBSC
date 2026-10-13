r"""Local stand-in for the Bloodborne server-info endpoint (/bb-eu/ss.info).

The game fetches this before it enables anything online. thehuntersdream.com served it on
port 20443 and their backend is down, so this serves it instead - and, more usefully, lets us
find out what the game actually wants in it without rebuilding the emulator every try.

Every request is logged in full (method, path, headers), which is the point: the request headers
say what the game expects back, and the game's reaction to each reply narrows down the format.

    python tools/ss_info_stub.py                      # empty 200, see how far the game gets
    python tools/ss_info_stub.py --size 15172         # same length as the real file
    python tools/ss_info_stub.py --body ss.info       # serve a real copy once we have one
    python tools/ss_info_stub.py --status 404         # check the game's failure path

Then point the emulator at it, in %APPDATA%\shadPS4\host_overrides.json:

    "https://ss4.scej-network.jp:20443": "http://127.0.0.1:20443"
"""
import argparse
import datetime
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ARGS = None
BODY = b""


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        pass  # we do our own, with headers

    def _serve(self, with_body):
        print("\n[{}] {} {}  from {}".format(stamp(), self.command, self.path,
                                             self.client_address[0]), flush=True)
        for key, value in self.headers.items():
            print("    {}: {}".format(key, value), flush=True)

        length = int(self.headers.get("Content-Length") or 0)
        if length:
            payload = self.rfile.read(length)
            print("    [request body {} bytes] {!r}".format(length, payload[:200]), flush=True)

        self.send_response(ARGS.status)
        self.send_header("Content-Type", ARGS.content_type)
        self.send_header("Content-Length", str(len(BODY)))
        self.end_headers()
        if with_body:
            self.wfile.write(BODY)
        print("    -> {} {} bytes ({})".format(ARGS.status, len(BODY), ARGS.content_type),
              flush=True)

    def do_GET(self):
        self._serve(True)

    def do_HEAD(self):
        self._serve(False)

    def do_POST(self):
        self._serve(True)


def main():
    global ARGS, BODY
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20443)
    parser.add_argument("--body", help="file to serve as the response body")
    parser.add_argument("--size", type=int, default=0,
                        help="serve this many filler bytes instead (real file is 15172)")
    parser.add_argument("--status", type=int, default=200)
    parser.add_argument("--content-type", default="application/octet-stream")
    ARGS = parser.parse_args()

    if ARGS.body:
        with open(ARGS.body, "rb") as handle:
            BODY = handle.read()
        print("serving {} ({} bytes)".format(ARGS.body, len(BODY)))
    elif ARGS.size:
        BODY = bytes(ARGS.size)
        print("serving {} filler bytes".format(ARGS.size))
    else:
        print("serving an empty body")

    server = ThreadingHTTPServer((ARGS.host, ARGS.port), Handler)
    print("ss.info stub on http://{}:{}  - status {}, {}".format(
        ARGS.host, ARGS.port, ARGS.status, ARGS.content_type))
    print("point host_overrides.json at it, then launch the game. Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
