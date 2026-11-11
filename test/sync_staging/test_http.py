"""Exercise the experimental stage handlers with the real stage writer and fake web transport."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('bool CrossPointWebServer::syncStageIdentityValid() const {')
end = source.index('\n#endif\n\n// File listing', start)
code = r'''
#include <cassert>
#include <ArduinoJson.h>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "network/sync/StageWriter.h"
#include "network/sync/StageDigest.h"
#include "network/sync/StageInspect.h"
#include "network/sync/RecoveryPreview.h"
#include "network/sync/PreparePublication.h"
#include "network/sync/CopyPublication.h"
using String=std::string;
HalStorage Storage;
void resetTaskWatchdogIfSubscribed(){} void yield(){}
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){return std::make_unique<T>(std::forward<A>(a)...);}
namespace pluginidentity {bool deviceId(const char*,size_t,char (&out)[65],bool create){assert(!create);memset(out,'a',64);out[64]=0;return true;}}
namespace syncstage {inline bool failDigest=false;
DigestResult hashStage(HalStorage&,const char*,uint64_t,uint8_t (&out)[32],DigestTick tick,void* ctx){
 if(!tick(ctx))return DigestResult::Cancelled;if(failDigest)return DigestResult::IoError;
 for(int i=0;i<32;++i)out[i]=i;return DigestResult::Ok;}
StageInspectResult inspectStage(HalStorage&,const char*,uint64_t,uint8_t (&)[32],DigestTick,void*){return StageInspectResult::Missing;}}
namespace syncpublish {inline PreviewResult next=PreviewResult::NoIntent;
PreviewResult previewRecovery(HalStorage&,RecoveryPreview& out,syncstage::DigestTick tick,void* ctx){
 if(!tick(ctx))return PreviewResult::Cancelled;
 if(next==PreviewResult::Observed){strcpy(out.stage.data(),"__xsync_0123456789abcdef0123456789abcdef.part");
  strcpy(out.destination.data(),"/Authors/Café.epub");out.size=5000;out.stageState=StageState::Matches;
  out.destinationState=DestinationState::Matches;}return next;}
inline PreflightResult nextPrepare=PreflightResult::Prepared;
PreflightResult prepareVerifiedIntent(HalStorage&,const IntentView& intent,syncstage::DigestTick tick,void* ctx){
 if(!tick(ctx))return PreflightResult::Cancelled;
 return valid(intent)?nextPrepare:PreflightResult::Invalid;}
inline InspectResult nextInspect=InspectResult::Valid;inline CopyResult nextCopy=CopyResult::Copied;
InspectResult inspectIntent(HalStorage&,Record& out,size_t& length){
 if(nextInspect==InspectResult::Valid){IntentView i{"__xsync_0123456789abcdef0123456789abcdef.part","/Authors/Book.epub",3,{}};
 assert(encode(i,out,length));}return nextInspect;}
CopyResult copyVerifiedStage(HalStorage&,const IntentView& intent,syncstage::DigestTick tick,void* ctx){
 assert(valid(intent));if(!tick(ctx))return CopyResult::Cancelled;return nextCopy;}}
enum {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload {int status;String name,filename;uint8_t* buf=nullptr;size_t currentSize=0;};
struct Client {bool alive=true;bool connected(){return alive;}void stop(){alive=false;}};
struct Server {std::map<String,String> params,headers;HTTPUpload part;Client conn;int status=0;String body;
 String arg(const char* key){auto i=params.find(key);return i==params.end()?String():i->second;}
 bool hasArg(const char* key){return params.count(key);}bool hasHeader(const char* key){return headers.count(key) && !headers[key].empty();}int args()const{return static_cast<int>(params.size());} String argName(int n){auto it=params.begin();std::advance(it,n);return it->first;} HTTPUpload& upload(){return part;}Client& client(){return conn;}
 void send(int code,const char*,const char* text){status=code;body=text;}
 void send(int code,const char*,const String& text){status=code;body=text;}};
struct CrossPointWebServer {std::unique_ptr<Server> server=std::make_unique<Server>();bool running=true;
 std::function<bool()> uploadCancelCheck;std::unique_ptr<StageWriter> syncStage;
 bool syncStageAttempted=false,syncStageRejected=false;
 bool dropUploadIfCancelled() const {if(uploadCancelCheck && uploadCancelCheck()){server->conn.stop();return true;}return false;}
 bool syncStageIdentityValid()const;void clearSyncStage();void handleSyncStage();void handleSyncStagePost();void handleSyncStageHash();void handleSyncStageInspect();void handleSyncRecoveryPreview();void handleSyncPrepareIntent();void handleSyncCopy();};
''' + source[start:end] + r'''
void init(CrossPointWebServer& s){s.server->params={{"path","/"},{"size","3"},{"deviceId",std::string(64,'a')}};
 s.server->part={UPLOAD_FILE_START,"file","__xsync_0123456789abcdef0123456789abcdef.part"};}
int main(){
 {fake::files.clear();CrossPointWebServer s;init(s);s.handleSyncStage();uint8_t data[]={1,2,3};
  s.server->part.status=UPLOAD_FILE_WRITE;s.server->part.buf=data;s.server->part.currentSize=3;s.handleSyncStage();
  s.server->part.status=UPLOAD_FILE_END;s.handleSyncStage();s.handleSyncStagePost();assert(s.server->status==200);
  assert(fake::files.begin()->second->bytes==std::vector<uint8_t>({1,2,3}));}
 {fake::files.clear();CrossPointWebServer s;init(s);s.server->params["deviceId"]=std::string(64,'b');
  s.handleSyncStage();s.handleSyncStagePost();assert(s.server->status==400 && fake::files.empty());}
 {fake::files.clear();CrossPointWebServer s;init(s);s.server->params["size"]="03";
  s.handleSyncStage();s.handleSyncStagePost();assert(s.server->status==400 && fake::files.empty());}
 {fake::files.clear();CrossPointWebServer s;init(s);s.handleSyncStage();s.handleSyncStage();
  s.handleSyncStagePost();assert(s.server->status==400 && fake::files.size()==1);}
 {fake::files.clear();CrossPointWebServer s;init(s);s.handleSyncStage();s.uploadCancelCheck=[]{return true;};
  uint8_t data[]={1};s.server->part.status=UPLOAD_FILE_WRITE;s.server->part.buf=data;s.server->part.currentSize=1;
  s.handleSyncStage();s.handleSyncStagePost();assert(s.server->status==400 && !s.server->conn.alive);}
 {fake::files.clear();CrossPointWebServer s;init(s);s.handleSyncStage();s.clearSyncStage();
  assert(!s.syncStageAttempted && !s.syncStageRejected && !s.syncStage && fake::files.size()==1);
  s.server->part.filename="__xsync_1123456789abcdef0123456789abcdef.part";s.handleSyncStage();
  assert(s.syncStage && !s.syncStageRejected);s.clearSyncStage();}
 {CrossPointWebServer s;init(s);s.server->params["name"]=s.server->part.filename;s.handleSyncStageHash();
  assert(s.server->status==200 && s.server->body=="{\"sha256\":\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"}");
  syncstage::failDigest=true;s.handleSyncStageHash();assert(s.server->status==409);}
 {CrossPointWebServer s;init(s);s.handleSyncRecoveryPreview();assert(s.server->status==200 && s.server->body=="{\"state\":\"none\"}");
  syncpublish::next=syncpublish::PreviewResult::Observed;s.handleSyncRecoveryPreview();JsonDocument json;
  assert(s.server->status==200 && !deserializeJson(json,s.server->body));
  assert(json["state"].as<std::string>()=="pending" && json["destination"].as<std::string>()=="/Authors/Café.epub");
  assert(json["size"].as<unsigned>()==5000 && json["stageState"].as<std::string>()=="matches");
  assert(json["copyState"].as<std::string>()=="missing");
  assert(json["destinationState"].as<std::string>()=="matches");
  syncpublish::next=syncpublish::PreviewResult::Damaged;s.handleSyncRecoveryPreview();assert(s.server->status==409);
  syncpublish::next=syncpublish::PreviewResult::Changed;s.handleSyncRecoveryPreview();assert(s.server->status==409);
  syncpublish::next=syncpublish::PreviewResult::Unavailable;s.handleSyncRecoveryPreview();assert(s.server->status==503);
  s.server->params["deviceId"]=std::string(64,'b');s.handleSyncRecoveryPreview();assert(s.server->status==400);}
 {CrossPointWebServer s;init(s);s.server->params={{"deviceId",std::string(64,'a')},
  {"stage",s.server->part.filename},{"destination","/Authors/Book.epub"},{"size","3"},{"sha256",std::string(64,'a')}};
  s.handleSyncPrepareIntent();assert(s.server->status==200 && s.server->body=="{\"state\":\"prepared\"}");
  syncpublish::nextPrepare=syncpublish::PreflightResult::DestinationPresent;s.handleSyncPrepareIntent();assert(s.server->status==409);
  syncpublish::nextPrepare=syncpublish::PreflightResult::ReservedState;s.handleSyncPrepareIntent();
  assert(s.server->status==409 && s.server->body=="{\"state\":\"destination_state_conflict\"}");
  s.server->params["sha256"]="bad";s.handleSyncPrepareIntent();assert(s.server->status==400);
  s.server->params["extra"]="x";s.handleSyncPrepareIntent();assert(s.server->status==400);}
 {CrossPointWebServer s;init(s);s.server->params={{"deviceId",std::string(64,'a')}};
  s.handleSyncCopy();assert(s.server->status==200 && s.server->body=="{\"state\":\"copied\"}");
  syncpublish::nextCopy=syncpublish::CopyResult::ExistingCopy;s.handleSyncCopy();assert(s.server->status==409);
  syncpublish::nextCopy=syncpublish::CopyResult::VerifiedExistingCopy;s.handleSyncCopy();assert(s.server->status==200);assert(s.server->body.find("verified_existing_copy")!=std::string::npos);
  syncpublish::nextInspect=syncpublish::InspectResult::Damaged;s.handleSyncCopy();assert(s.server->status==409);
  s.server->params["extra"]="x";s.handleSyncCopy();assert(s.server->status==400);}
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'test.cpp').write_text(code)
    json_include=Path(sys.argv[1]) if len(sys.argv)>1 else root/'.pio/libdeps/x4pro/ArduinoJson/src'
    cmd=[os.environ.get('CXX','c++'),'-std=c++20',*shlex.split(os.environ.get('CXXFLAGS','')),
         '-I',str(json_include),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src'),
         '-I',str(root/'lib/hal'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),str(tmp/'test.cpp'),
         str(root/'src/network/sync/StageWriter.cpp'),'-o',str(tmp/'test')]
    subprocess.run(cmd,check=True);subprocess.run([str(tmp/'test')],check=True)
print('Experimental stage and hash handlers passed simulated requests')
