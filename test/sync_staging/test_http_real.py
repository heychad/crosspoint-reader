"""Compose actual staging HTTP handlers, HAL methods, digest and patched SdFat."""
import importlib.util
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('stage_real', root/'test/sync_staging/test_real.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
source = (root/'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('bool CrossPointWebServer::syncStageIdentityValid() const {')
end = source.index('\n#endif\n\n// File listing', start)
code = fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code = code.replace('bool openFileForWrite(const char*,const char* path,FsFile& file)',
                    'bool rename(const char* a,const char* b){return volume->rename(a,b);}\n bool mkdir(const char* p,bool recursive){return volume->mkdir(p,recursive);}\n bool exists(const char* p){return volume->exists(p);}\n bool openFileForWrite(const char*,const char* path,FsFile& file)')
code = code.replace('bool withStorageLock(bool (*)(void*),void*);',
                    'bool withStorageLock(bool (*)(void*),void*);bool rename(const char*,const char*);bool mkdir(const char*,bool);')
code += '\n#define HAL_STORAGE_MUTATION_CALL(method, ...) HalStorage::StorageLock lock; mutationFence.mutationAttempt(); const bool ok=SDCard.method(__VA_ARGS__); if(!ok)mutationFence.poison(); return ok;\n'
code += fixture.hal.extract('bool HalStorage::rename(')
code += fixture.hal.extract('bool HalStorage::mkdir(')
code += '\n#include "PublishIntentStore.h"\n' + (root/'src/network/sync/PublishIntentStore.cpp').read_text()
code += '\n#include "StageInspect.h"\n' + (root/'src/network/sync/StageInspect.cpp').read_text()
code += '\n#include "RecoveryPreview.h"\n' + (root/'src/network/sync/RecoveryPreview.cpp').read_text()
code += '\n#include "DestinationGuard.h"\n' + (root/'src/network/sync/DestinationGuard.cpp').read_text()
code += '\n#include "CanonicalDestination.h"\n' + (root/'src/network/sync/CanonicalDestination.cpp').read_text()
code += '\n#include "StoredReferenceGuard.h"\n' + (root/'src/network/sync/StoredReferenceGuard.cpp').read_text()
code += '\n#include "PreparePublication.h"\n' + (root/'src/network/sync/PreparePublication.cpp').read_text().replace('prepareLocked','preflightPrepareLocked')
code += '\n#include "CopyPublication.h"\n' + (root/'src/network/sync/CopyPublication.cpp').read_text()
code += '\n#include "FinalizePublication.h"\n' + (root/'src/network/sync/FinalizePublication.cpp').read_text().replace('Work', 'FinalizeWork')
code += '\n#include "ArchivePublication.h"\n' + (root/'src/network/sync/ArchivePublication.cpp').read_text()
parents = (root/'src/network/sync/EnsureParents.cpp').read_text().replace('ScanContext','ParentsScanContext').replace('keepGoing','parentsKeepGoing')
parents = re.sub(r'\bemit\b', 'parentsEmit', parents)
code += '\n#include "EnsureParents.h"\n' + parents
code += r'''
namespace syncpublish {bool testGate(const char*,void*){return true;} bool testAcquire(void*){return true;} void testRelease(void*){}
FinalizeResult finalizeFromTransferActivity(HalStorage& s,syncstage::DigestTick t,void* c){return finalizeVerifiedCopy(s,t,c,testGate,nullptr,testAcquire,testRelease,nullptr);}}
'''
code += r'''
#include <ArduinoJson.h>
#include <functional>
#include <map>
using String=std::string;
HalStorage& Storage=HalStorage::getInstance();
void resetTaskWatchdogIfSubscribed(){} void yield(){}
namespace pluginidentity {bool deviceId(const char*,size_t,char (&out)[65],bool create){assert(!create);memset(out,'a',64);out[64]=0;return true;}}
enum {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload {int status;String name,filename;uint8_t* buf=nullptr;size_t currentSize=0;};
struct Client {bool alive=true;bool connected(){return alive;}void stop(){alive=false;}};
struct Server {std::map<String,String> params,headers;HTTPUpload part;Client conn;int status=0;String body;
 String arg(const char* key){auto i=params.find(key);return i==params.end()?String():i->second;}
 bool hasArg(const char* key){return params.count(key);}bool hasHeader(const char* key){return headers.count(key) && !headers[key].empty();}int args()const{return static_cast<int>(params.size());} String argName(int n){auto it=params.begin();std::advance(it,n);return it->first;} HTTPUpload& upload(){return part;}Client& client(){return conn;}
 void send(int result,const char*,const char* text){status=result;body=text;}
 void send(int result,const char*,const String& text){status=result;body=text;}};
struct CrossPointWebServer {std::unique_ptr<Server> server=std::make_unique<Server>();bool running=true;
 std::function<bool()> uploadCancelCheck;std::unique_ptr<StageWriter> syncStage;
 bool syncStageAttempted=false,syncStageRejected=false;
 bool experimentalPublicationEnabled=true;
 bool dropUploadIfCancelled() const {if(uploadCancelCheck && uploadCancelCheck()){server->conn.stop();return true;}return false;}
 bool syncStageIdentityValid()const;void clearSyncStage();void handleSyncStage();void handleSyncStagePost();void handleSyncStageHash();void handleSyncStageInspect();void handleSyncRecoveryPreview();void handleSyncPrepareIntent();void handleSyncCopy();void handleSyncFinalize();void handleSyncArchive();void handleSyncEnsureParents();};
''' + source[start:end] + r'''
int main(int argc,char** argv){
 assert(argc==3);bool exfat=std::string(argv[1])=="exfat";std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter fmt;assert(fmt.format(&disk,scratch));}
 else {FatFormatter fmt;assert(fmt.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/books",true));assert(volume.mkdir("/.crosspoint",true));
 {auto f=volume.open("/books/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(f && f.write(p,sizeof(p))==sizeof(p) && f.sync() && f.close());}
 CrossPointWebServer web;constexpr char name[]="__xsync_0123456789abcdef0123456789abcdef.part";
 constexpr char path[]="/__xsync_0123456789abcdef0123456789abcdef.part";
 if(scenario=="full") {
  CrossPointWebServer browser;browser.server->params={{"path","/"},{"size","5000"},{"deviceId",std::string(64,'a')}};
  browser.server->headers["Origin"]="https://example.invalid";browser.server->part={UPLOAD_FILE_START,"file",name};
  const auto writes=disk.writes;browser.handleSyncStage();browser.handleSyncStagePost();
  assert(browser.server->status==400 && disk.writes==writes && !volume.exists(path));
  browser.server->params={{"deviceId",std::string(64,'a')},{"destination","/books/Blocked/Book.epub"}};
  browser.handleSyncEnsureParents();assert(browser.server->status==400 && disk.writes==writes);
  assert(!volume.exists("/books/Blocked"));
 }
 std::vector<uint8_t> bytes(5000,42);
 if(scenario=="full") {web.server->params={{"name",name},{"size","5000"},{"deviceId",std::string(64,'a')}};
  web.handleSyncStageInspect();assert(web.server->status==200 && web.server->body=="{\"state\":\"missing\"}");}
 web.server->params={{"path","/"},{"size","5000"},{"deviceId",std::string(64,'a')}};
 web.server->part={UPLOAD_FILE_START,"file",name};
 if(scenario.starts_with("write-"))disk.failAt=disk.writes+std::stoi(scenario.substr(6));
 web.handleSyncStage();web.server->part.status=UPLOAD_FILE_WRITE;web.server->part.buf=bytes.data();
 web.server->part.currentSize=bytes.size();web.handleSyncStage();web.server->part.status=UPLOAD_FILE_END;
 web.handleSyncStage();web.handleSyncStagePost();
 if(disk.injected)assert(web.server->status==400);else assert(web.server->status==200);
 const bool uploaded=web.server->status==200;disk.failAt=-1;
 if(uploaded){
  SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
  if(scenario.starts_with("inspectread-")) {web.server->params={{"name",name},{"size","5000"},{"deviceId",std::string(64,'a')}};
   disk.failReadAt=disk.reads+std::stoi(scenario.substr(12));const auto writes=disk.writes;web.handleSyncStageInspect();
   if(disk.injectedRead)assert(web.server->status!=200);
   else {JsonDocument inspected;assert(web.server->status==200 && !deserializeJson(inspected,web.server->body));
    assert(inspected["state"].as<std::string>()=="complete");}assert(disk.writes==writes);}
  if(scenario=="full") {web.server->params={{"name",name},{"size","5001"},{"deviceId",std::string(64,'a')}};
   web.handleSyncStageInspect();assert(web.server->status==200 && web.server->body=="{\"state\":\"wrong_size\"}");
   web.server->params["size"]="5000";web.handleSyncStageInspect();JsonDocument inspected;
   assert(web.server->status==200 && !deserializeJson(inspected,web.server->body));
   assert(inspected["state"].as<std::string>()=="complete" && inspected["sha256"].as<std::string>().size()==64);}
  if(scenario.starts_with("hash-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
  web.server->params["name"]=name;web.handleSyncStageHash();
  if(disk.injectedRead)assert(web.server->status==409);
  else {uint8_t digest[32];CC_SHA256(bytes.data(),bytes.size(),digest);char expected[78]="{\"sha256\":\"";
   constexpr char hex[]="0123456789abcdef";for(int i=0;i<32;++i){expected[11+i*2]=hex[digest[i]>>4];expected[12+i*2]=hex[digest[i]&15];}
   expected[75]='"';expected[76]='}';expected[77]=0;
   assert(web.server->status==200 && web.server->body==expected);}
  if(scenario=="normal" || scenario=="full" || scenario=="parents" || scenario=="partial" || scenario=="unicode-clear" || scenario=="unicode-conflict" || scenario=="sidecar" || scenario=="temporary" || scenario=="noncanonical" || scenario.starts_with("copywrite-") || scenario.starts_with("copyread-")){
   if(scenario=="parents") {web.server->params={{"deviceId",std::string(64,'a')},{"destination","/books/New/Series/Book.epub"}};
    web.handleSyncEnsureParents();assert(web.server->status==200 && web.server->body=="{\"state\":\"ready\",\"created\":2}");
    web.handleSyncEnsureParents();assert(web.server->status==200 && web.server->body=="{\"state\":\"ready\",\"created\":0}");}
   if(scenario=="sidecar"){auto f=Storage.openChecked("/books/Book.epub.meta.json",O_CREAT|O_EXCL|O_WRONLY);
    uint8_t x=9;assert(f && f.write(&x,1)==1 && f.sync() && f.close());}
   if(scenario=="temporary"){auto f=Storage.openChecked("/books/other.epub.part",O_CREAT|O_EXCL|O_WRONLY);
    uint8_t x=9;assert(f && f.write(&x,1)==1 && f.sync() && f.close());}
   if(scenario=="unicode-clear" || scenario=="unicode-conflict"){
    const std::string json=scenario=="unicode-clear"?"{\"openEpubPath\":\"/other/Été.epub\"}":"{\"openEpubPath\":\"/other/été.epub\"}";
    auto f=Storage.openChecked("/.crosspoint/state.json",O_CREAT|O_EXCL|O_WRONLY);
    assert(f && f.write(reinterpret_cast<const uint8_t*>(json.data()),json.size())==json.size() && f.sync() && f.close());}
   uint8_t digest[32];CC_SHA256(bytes.data(),bytes.size(),digest);constexpr char hex[]="0123456789abcdef";
   std::string hash;for(auto b:digest){hash+=hex[b>>4];hash+=hex[b&15];}
   web.server->params={{"deviceId",std::string(64,'a')},{"stage",name},
    {"destination","/books/Book.epub"},{"size","5000"},{"sha256",hash}};
   if(scenario=="parents")web.server->params["destination"]="/books/New/Series/Book.epub";
   if(scenario=="unicode-conflict")web.server->params["destination"]="/books/ÉTÉ.epub";
   if(scenario=="noncanonical")web.server->params["destination"]="/BOOKS/Book.epub";
   web.handleSyncPrepareIntent();
   if(scenario=="sidecar" || scenario=="temporary" || scenario=="unicode-conflict")assert(web.server->status==409 && web.server->body=="{\"state\":\"destination_state_conflict\"}");
   else if(scenario=="noncanonical")assert(web.server->status==409 && web.server->body=="{\"state\":\"noncanonical_path\"}");
   else {assert(web.server->status==200 && web.server->body=="{\"state\":\"prepared\"}");
    web.server->params={{"deviceId",std::string(64,'a')}};
    if(scenario=="partial") {const std::string privatePath=std::string("/.crosspoint/")+name;
     auto f=Storage.openChecked(privatePath.c_str(),O_CREAT|O_EXCL|O_WRONLY);uint8_t p=42;
     assert(f && f.write(&p,1)==1 && f.sync() && f.close());}
    if(scenario.starts_with("copywrite-"))disk.failAt=disk.writes+std::stoi(scenario.substr(10));
    if(scenario.starts_with("copyread-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(9));
    web.handleSyncCopy();
    if(disk.injected || disk.injectedRead)assert(web.server->status!=200);
    else {assert(web.server->status==200 && web.server->body=="{\"state\":\"copied\"}");
     if(scenario=="normal" || scenario=="partial") {const auto writes=disk.writes;web.handleSyncCopy();
      assert(web.server->status==200 && web.server->body=="{\"state\":\"verified_existing_copy\"}");
      assert(disk.writes==writes);}
     web.handleSyncRecoveryPreview();JsonDocument json;
     assert(web.server->status==200 && !deserializeJson(json,web.server->body));
     assert(json["stageState"].as<std::string>()=="matches" && json["copyState"].as<std::string>()=="matches" && json["destinationState"].as<std::string>()=="missing");
     if(scenario=="full" || scenario=="parents"){
      web.experimentalPublicationEnabled=false;web.handleSyncFinalize();assert(web.server->status==403);
      web.server->params["stage"]=name;web.handleSyncArchive();assert(web.server->status==403);web.server->params.erase("stage");
      web.experimentalPublicationEnabled=true;web.handleSyncFinalize();assert(web.server->status==200 && web.server->body=="{\"state\":\"published\"}");
      web.handleSyncFinalize();assert(web.server->status==200 && web.server->body=="{\"state\":\"matching_destination\"}");
      web.server->params["stage"]="invalid";web.handleSyncArchive();assert(web.server->status==400);
      web.server->params["stage"]=name;web.handleSyncArchive();assert(web.server->status==200 && web.server->body=="{\"state\":\"archived\"}");
      web.handleSyncArchive();assert(web.server->status==200 && web.server->body=="{\"state\":\"already_archived\"}");
     }}}
  }
 }
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injected || disk.injectedRead?"injected":"not reached");
 disk.failAt=disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 if(uploaded && !disk.injectedRead){auto stage=volume.open(path,O_RDONLY);std::vector<uint8_t> actual(bytes.size());
  assert(stage && stage.read(actual.data(),actual.size())==static_cast<int>(actual.size()) && stage.close() && actual==bytes);}
 auto extra=volume.open("/books/later.bin",O_CREAT|O_EXCL|O_WRONLY);assert(extra);
 std::vector<uint8_t> allocation(32768,0x5a);assert(extra.write(allocation.data(),allocation.size())==allocation.size() && extra.sync() && extra.close());
 auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t p[4]{};
 assert(progress && progress.read(p,sizeof(p))==sizeof(p) && progress.close());
 assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
 if(scenario=="parents")assert(volume.exists("/books/New/Series"));
 if(scenario=="full" || scenario=="parents") {auto book=volume.open(scenario=="parents"?"/books/New/Series/Book.epub":"/books/Book.epub",O_RDONLY);std::vector<uint8_t> actual(bytes.size());
  assert(book && book.read(actual.data(),actual.size())==static_cast<int>(actual.size()) && book.close() && actual==bytes);
  assert(!volume.exists(syncpublish::INTENT_PATH));
  assert(volume.exists("/.crosspoint/xsync-0123456789abcdef0123456789abcdef.done"));}
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'test.cpp').write_text(code)
    (tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n')
    (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    cmd=[os.environ.get('CXX','c++'),*fixture.hal.flags,'-DCROSSPOINT_EXPERIMENTAL_PUBLISH=1',*shlex.split(os.environ.get('CXXFLAGS','')),
         '-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),
         '-I',str(fixture.hal.dependency/'src'),'-I',str(root/'src/network/sync'),
         '-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src/activities/reader'),
         '-I',str(root/'src'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),
         '-I',str(root/'.pio/libdeps/x4pro/ArduinoJson/src'),str(tmp/'test.cpp'),
         *map(str,fixture.hal.files),'-o',str(tmp/'test')]
    subprocess.run(cmd,check=True)
    for fs in ('fat','exfat'):
        for scenario in ('normal','full','parents','partial','unicode-clear','unicode-conflict','sidecar','temporary','noncanonical',*(f'write-{n}' for n in range(12)),*(f'hash-{n}' for n in range(12)),*(f'inspectread-{n}' for n in range(20)),
                         *(f'copywrite-{n}' for n in range(20)),*(f'copyread-{n}' for n in range(30))):
            subprocess.run([str(tmp/'test'),fs,scenario],check=True)
print('Stage HTTP handlers passed FAT/exFAT filesystem integration with simulated transport')
