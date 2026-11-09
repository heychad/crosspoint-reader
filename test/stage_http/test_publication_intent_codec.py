"""Exercise the XSP1 intent codec against an independent byte fixture."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
stage = b'__xsync_0123456789abcdef0123456789abcdef.part'
destination = b'/Authors/Writer/An EPUB.epub'
digest = bytes((1, 2, 3, 4)) + bytes(28)
record = b'XSP1' + struct.pack('<QH', 123456, len(destination)) + stage + digest + destination
record += struct.pack('<I', zlib.crc32(record))
code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "PublicationIntentCodec.h"
using namespace wirecheck::publication;
int main(){
 const std::array<uint8_t, GOLDEN_SIZE> golden={GOLDEN_BYTES};
 const View view{"__xsync_0123456789abcdef0123456789abcdef.part", "/Authors/Writer/An EPUB.epub", 123456, {1,2,3,4}};
 std::array<uint8_t,MAX_RECORD> encoded{};const auto n=encodeV1(view,encoded.data(),encoded.size());
 assert(n==golden.size() && std::memcmp(encoded.data(),golden.data(),n)==0);
 Intent decoded{};assert(decodeV1(golden.data(),golden.size(),decoded));
 assert(std::string(decoded.stage.data())==view.stage && std::string(decoded.destination.data())==view.destination);
 assert(decoded.size==view.size && decoded.sha256==view.sha256);
 for(size_t i=0;i<golden.size();++i){auto broken=golden;broken[i]^=1;Intent keep{};keep.size=987;
  assert(!decodeV1(broken.data(),broken.size(),keep) && keep.size==987);}
 for(size_t i=0;i<golden.size();++i){Intent keep{};keep.size=987;assert(!decodeV1(golden.data(),i,keep) && keep.size==987);}
 assert(!decodeV1(nullptr,golden.size(),decoded));assert(!encodeV1(view,nullptr,encoded.size()));
 assert(!encodeV1(view,encoded.data(),golden.size()-1));
 for(const char* path:{"/../Bad.epub","/.crosspoint/Bad.epub","/Books/Bad.pdf","/Books//Bad.epub",
                       "/Books/Bad.epub/","/XTCache/Bad.epub","/x/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/Bad.epub"}){
  auto v=view;v.destination=path;assert(!validV1(v));}
 auto v=view;v.destination="/Authors/Title.EPUB";assert(validV1(v));
 v=view;v.stage="__xsync_0123456789ABCDEF0123456789abcdef.part";assert(!validV1(v));
 v=view;v.size=MAX_SIZE+1;assert(!validV1(v));
 std::string longPath="/read/"+std::string(1008,'x')+"\xc3\xa9.epub";v=view;v.destination=longPath;
 assert(validV1(v));const auto longSize=encodeV1(v,encoded.data(),encoded.size());assert(longSize && decodeV1(encoded.data(),longSize,decoded));
 assert(std::string(decoded.destination.data())==longPath);
}
'''.replace('GOLDEN_SIZE', str(len(record))).replace('GOLDEN_BYTES', ','.join(map(str, record)))
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'codec.cpp'
    binary = Path(tmp) / 'codec'
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', str(ROOT / 'src/network/sync'),
                    '-I', str(ROOT / 'lib/Serialization'), '-I', str(ROOT / 'lib/FsHelpers'),
                    str(cpp), '-o', str(binary)], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
print('XSP1 fixture, corruption, truncation and path checks passed')
