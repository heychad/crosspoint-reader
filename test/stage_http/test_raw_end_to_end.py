"""Exercise the experimental Python client against the actual draft C++ listener."""
import ast
import hashlib
import io
import os
from pathlib import Path
import select
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from wireless_raw_stage import upload_stage

tree = ast.parse((ROOT / 'test/stage_http/test_raw_session.py').read_text())
stub = next(ast.literal_eval(node.value) for node in tree.body
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'STUB' for t in node.targets))
code = r'''
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include "RawStageListener.h"
struct Context {std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();};
uint32_t clockNow(void* p){auto& c=*static_cast<Context*>(p);return std::chrono::duration_cast<std::chrono::milliseconds>(
 std::chrono::steady_clock::now()-c.start).count();}
bool active(void*){return true;}
bool identity(void*,const char* id){return std::string(id)==std::string(64,'a');}
int main(){HalStorage disk;Context c;wirecheck::RawStageListener server(disk,{&c,clockNow,active,identity});
 if(!server.begin(0))return 2;std::printf("%u\n",server.localPort());std::fflush(stdout);
 for(int i=0;i<10000 && !server.completed();++i){server.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(!server.completed() || server.result()!=wirecheck::RawStageConnection::Result::Staged)return 3;
 std::printf("%zu:",disk.bytes.size());for(auto byte:disk.bytes)std::printf("%02x",byte);std::puts("");
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(stub)
    (directory / 'server.cpp').write_text(code)
    executable = directory / 'server'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'server.cpp'), '-o', str(executable)],
                   check=True, timeout=30)
    server = subprocess.Popen([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if not select.select([server.stdout], [], [], 5)[0]:
            raise TimeoutError('Draft listener did not start.')
        port = int(server.stdout.readline().strip())
        book = b'content sent from the Mac'
        upload_stage('http://127.0.0.1', port, '__xsync_0123456789abcdef0123456789abcdef.part',
                     'a' * 64, io.BytesIO(book), len(book), hashlib.sha256(book).hexdigest())
        stdout, stderr = server.communicate(timeout=15)
        assert server.returncode == 0, stderr
        assert stdout.strip() == f'{len(book)}:{book.hex()}'
    finally:
        if server.poll() is None:
            server.terminate()
            server.wait(timeout=5)


class FakeResponse:
    status = 201

    def __init__(self, body):
        self.body = body

    def read(self, _limit):
        return self.body

    def getheaders(self):
        return [('Content-Length', '15')]


class FakeConnection:
    def __init__(self, body):
        self.body = body
        self.closed = False

    def putrequest(self, *_args, **_kwargs): pass
    def putheader(self, *_args): pass
    def endheaders(self): pass
    def send(self, _data): pass
    def close(self): self.closed = True
    def getresponse(self): return FakeResponse(self.body)


for body in (b'{"staged":1}', b'{"staged":true,"staged":true}', b'{"staged":true,"extra":1}'):
    connection = FakeConnection(body)
    with patch('wireless_raw_stage.http.client.HTTPConnection', return_value=connection):
        try:
            upload_stage('http://127.0.0.1', 1234, '__xsync_0123456789abcdef0123456789abcdef.part',
                         'a' * 64, io.BytesIO(b'x'), 1, hashlib.sha256(b'x').hexdigest())
            raise AssertionError('Invalid staging response was accepted.')
        except ValueError:
            assert connection.closed

connection = FakeConnection(b'{"staged":true}')
with patch('wireless_raw_stage.http.client.HTTPConnection', return_value=connection):
    try:
        upload_stage('http://127.0.0.1', 1234, '__xsync_0123456789abcdef0123456789abcdef.part',
                     'a' * 64, io.BytesIO(b'changed'), 7, hashlib.sha256(b'original').hexdigest())
        raise AssertionError('Changed source was accepted.')
    except ValueError:
        assert connection.closed

# A valid JSON prefix cannot stand in for the rest of a declared HTTP body.
listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(1)
port = listener.getsockname()[1]


def truncated_reply():
    connection, _ = listener.accept()
    with connection:
        request = b''
        while b'\r\n\r\n' not in request:
            chunk = connection.recv(1024)
            if not chunk:
                return
            request += chunk
        connection.sendall(b'HTTP/1.1 201 Created\r\nContent-Length: 100\r\nConnection: close\r\n\r\n'
                           b'{"staged":true}')


thread = threading.Thread(target=truncated_reply)
thread.start()
try:
    try:
        upload_stage('http://127.0.0.1', port, '__xsync_0123456789abcdef0123456789abcdef.part',
                     'a' * 64, io.BytesIO(b''), 0, hashlib.sha256(b'').hexdigest())
        raise AssertionError('Truncated HTTP response was accepted.')
    except ValueError:
        pass
finally:
    listener.close()
    thread.join(timeout=5)
    assert not thread.is_alive()
print('Raw staging client and draft listener exchanged and staged the expected bytes')
