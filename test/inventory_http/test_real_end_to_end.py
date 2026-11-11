"""Join actual inventory handlers, session and HAL methods to SdFat images."""
from pathlib import Path
import importlib.util
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hal_real', ROOT / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
web = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
start = web.index('struct CrossPointWebServer::InventoryWebState {')
handlers = web[start:web.index('\n#endif', start)]
code = hal.code.split('int main(', 1)[0] + r'''
#include <cerrno>
#include <functional>
#include <unordered_map>
#define MSG_DONTWAIT 0x40
unsigned long clockValue=10;
unsigned long millis(){return clockValue;}
void yield(){} void delay(unsigned long ms){clockValue+=ms;} void resetTaskWatchdogIfSubscribed(){}
void esp_fill_random(void* p,size_t n){for(size_t i=0;i<n;++i)static_cast<uint8_t*>(p)[i]=static_cast<uint8_t>(i);}
struct NetworkClient {
 bool alive=true;size_t limit=SIZE_MAX;std::string data;
 bool connected()const{return alive;}int fd()const{return 7;}void stop(){alive=false;}
};
NetworkClient* activeClient=nullptr;std::function<void(const char*,size_t)> sendHook;
int send(int fd,const void* raw,size_t n,int flags){
 assert(fd==7 && flags==MSG_DONTWAIT && activeClient);const auto* data=static_cast<const char*>(raw);
 if(sendHook)sendHook(data,n);
 if(activeClient->data.size()>=activeClient->limit){errno=EAGAIN;return -1;}
 n=std::min(n,activeClient->limit-activeClient->data.size());activeClient->data.append(data,n);return static_cast<int>(n);
}
struct Server {
 NetworkClient connection;std::unordered_map<std::string,std::string> args;int status=0;std::string body;
 NetworkClient& client(){activeClient=&connection;return connection;}
 bool hasArg(const char* key){return args.count(key);}
 std::string arg(const char* key){return args.at(key);}
 void send(int code,const char*,const char* text){status=code;body=text;}
};
auto& Storage=HalStorage::getInstance();
class CrossPointWebServer {
 public:
 struct InventoryWebState;
 std::unique_ptr<Server> server=std::make_unique<Server>();
 std::unique_ptr<InventoryWebState> inventory;
 bool running=true;std::function<bool()> uploadCancelCheck;
 CrossPointWebServer();~CrossPointWebServer();
 void handleInventoryStart();void handleInventoryDirectory();void handleInventoryFinish();
 void expireInventory();bool inventoryActive()const;bool inventoryEmit(const wirecheck::DirectoryEntry&);
};
''' + handlers + r'''
CrossPointWebServer::CrossPointWebServer()=default;
CrossPointWebServer::~CrossPointWebServer()=default;
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string mode=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}
 else {FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/books",true));
 {auto file=volume.open("/books/A book.epub",O_CREAT|O_EXCL|O_WRONLY);const uint8_t data[]={1,2,3};
  assert(file && file.write(data,sizeof(data))==sizeof(data) && file.sync() && file.close());}
 {auto file=volume.open("/books/.hidden",O_CREAT|O_EXCL|O_WRONLY);assert(file && file.close());}
 CrossPointWebServer web;web.handleInventoryStart();assert(web.server->status==200 && web.inventory);
 const std::string token=web.inventory->token;
 web.server->args={{"token",token},{"path","/books"}};
 if(mode=="cut")web.server->connection.limit=160;
 bool changed=false;
 if(mode=="change")sendHook=[&](const char* data,size_t n){
  if(!changed && std::string(data,n).find("A book.epub")!=std::string::npos){
   changed=true;auto writer=Storage.openChecked("/books/new.epub",O_CREAT|O_EXCL|O_WRONLY);
   assert(writer && writer.close());
  }
 };
 web.handleInventoryDirectory();const auto& response=web.server->connection.data;
 assert(!web.server->connection.alive);
 if(mode=="normal"){
  assert(web.inventory && response.find("A book.epub")!=std::string::npos);
  assert(response.find(".hidden")==std::string::npos && response.find("complete\\\":true")!=std::string::npos);
  assert(response.size()>=5 && response.substr(response.size()-5)=="0\r\n\r\n");
  web.server->connection.alive=true;web.server->args={{"token",token}};web.handleInventoryFinish();
  assert(web.server->status==200 && web.server->body=="{\"snapshotValid\":true}");
 } else {
  if(mode=="change")assert(changed);
  assert(!web.inventory && (response.size()<5 || response.substr(response.size()-5)!="0\r\n\r\n"));
 }
 return 0;
}
'''
# Make the JSON marker a C++ literal matching the wire response.
code = code.replace('complete\\\\\\":true', 'complete\\\":true')
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HalStorage.h').write_text('#pragma once\n')
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\nunsigned long millis();\n')
    command = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp), '-I', str(ROOT / 'lib/hal'),
               '-I', str(hal.dependency / 'src'), '-I', str(ROOT / 'src/network/sync'), '-I', str(ROOT / 'src/activities/reader'), '-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/FsHelpers'),
               str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(command, check=True)
    for filesystem in ('fat', 'exfat'):
        for scenario in ('normal', 'change', 'cut'):
            subprocess.run([str(tmp / 'test'), filesystem, scenario], check=True)
print('Actual inventory handlers and session passed with FAT/exFAT images and simulated network')
