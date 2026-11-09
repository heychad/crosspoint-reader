"""Exercise strict, bounded polling without contacting a reader."""
import http.client
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import sys
import threading
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from wireless_hash_poll import verify_stage

NAME = '__xsync_0123456789abcdef0123456789abcdef.part'
IDENTITY = 'a' * 64
DIGEST = 'b' * 64


class Response:
    def __init__(self, status, body, headers=None):
        self.status = status
        self.body = body
        self.headers = headers if headers is not None else [('Content-Length', str(len(body)))]

    def getheaders(self):
        return self.headers

    def read(self, size):
        return self.body[:size]


class Connection:
    def __init__(self, response):
        self.response = response
        self.closed = False
        self.requests = []

    def request(self, method, path, headers):
        self.requests.append((method, path, headers))

    def getresponse(self):
        return self.response

    def close(self):
        self.closed = True


def run(responses, confirm=lambda: None, **kwargs):
    connections = [Connection(response) for response in responses]
    with patch('wireless_hash_poll.http.client.HTTPConnection', side_effect=connections), \
            patch('wireless_hash_poll.time.sleep'):
        result = verify_stage('http://127.0.0.1:8080', NAME, 3, IDENTITY, confirm, **kwargs)
    assert all(connection.closed for connection in connections)
    return result, connections


checks = []
result, connections = run([Response(202, b'{"status":"pending"}'),
                           Response(200, ('{"sha256":"' + DIGEST + '"}').encode())],
                          lambda: checks.append('confirmed'))
assert result == DIGEST and checks == ['confirmed', 'confirmed']
assert all(item.requests[0][0] == 'GET' and item.requests[0][1].startswith('/api/sync/stage-hash?')
           for item in connections)


class LocalHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.calls += 1
        body = (b'{"status":"pending"}' if self.server.calls == 1
                else ('{"sha256":"' + DIGEST + '"}').encode())
        self.send_response(202 if self.server.calls == 1 else 200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


server = HTTPServer(('127.0.0.1', 0), LocalHandler)
server.calls = 0
server.timeout = 5
thread = threading.Thread(target=lambda: (server.handle_request(), server.handle_request()))
thread.start()
try:
    observed = []
    assert verify_stage(f'http://127.0.0.1:{server.server_port}', NAME, 3, IDENTITY,
                        lambda: observed.append(True), timeout=3) == DIGEST
    assert observed == [True, True] and server.calls == 2
finally:
    thread.join(timeout=6)
    server.server_close()
    assert not thread.is_alive()

for response in (Response(200, b'{"sha256":"' + DIGEST.encode() + b'","sha256":"' + DIGEST.encode() + b'"}'),
                 Response(202, b'{"status":"pending","extra":1}'),
                 Response(200, b'{}', [('Content-Length', '4')]),
                 Response(200, b'{}', [('Transfer-Encoding', 'chunked'), ('Content-Length', '2')]),
                 Response(409, b'{}')):
    try:
        run([response])
        raise AssertionError('Invalid response was accepted')
    except (ValueError, http.client.IncompleteRead):
        pass

def reader_changed():
    checks.append('changed')
    if checks.count('changed') == 2:
        raise ValueError('Reader changed')

try:
    run([Response(200, ('{"sha256":"' + DIGEST + '"}').encode())], reader_changed)
    raise AssertionError('Reader change was accepted')
except ValueError as error:
    assert str(error) == 'Reader changed'

try:
    verify_stage('http://127.0.0.1', NAME, 3, IDENTITY, lambda: None, cancelled=lambda: True)
    raise AssertionError('Cancellation was ignored')
except TimeoutError:
    pass

for bad_url in ('https://127.0.0.1', 'http://user@127.0.0.1', 'http://127.0.0.1:0',
                'http://127.0.0.1/other', 'http://127.0.0.1\n.attacker'):
    try:
        verify_stage(bad_url, NAME, 3, IDENTITY, lambda: None)
        raise AssertionError('Invalid reader address was accepted')
    except ValueError:
        pass

print('Cooperative stage-hash polling checks passed')
