"""Loopback-only test feed with a deterministic mid-transfer disconnect case."""
import argparse
import http.server
import pathlib
import socket


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.endswith("/interrupt/payload/data/large.bin"):
            path = pathlib.Path(self.translate_path(self.path))
            self.send_response(200)
            self.send_header("Content-Length", str(path.stat().st_size))
            self.end_headers()
            self.wfile.write(path.read_bytes()[:65536])
            self.wfile.flush()
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            self.close_connection = True
            return
        super().do_GET()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
