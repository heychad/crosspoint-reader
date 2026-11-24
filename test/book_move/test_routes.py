"""Run production move/rename handlers against a recording transport and move owner."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index('\n}\n', start + len(signature)) + 2
    return source[start:end]


shim = r'''
#pragma once
#include <string>
#include <map>
class String : public std::string {
 public:
 using std::string::string;
 String(const std::string& s):std::string(s){}
 bool isEmpty()const{return empty();}
 bool equals(const char* value)const{return *this==value;}
 void trim(){while(!empty()&&back()==' ')pop_back();while(!empty()&&front()==' ')erase(begin());}
 int indexOf(char c)const{auto n=find(c);return n==npos?-1:int(n);}
 int lastIndexOf(char c)const{auto n=rfind(c);return n==npos?-1:int(n);}
 String substring(size_t start,size_t end=npos)const{return substr(start,end==npos?npos:end-start);}
 bool endsWith(const char* s)const{return ends_with(s);}
};
enum HTTPMethod{HTTP_GET,HTTP_HEAD,HTTP_POST,HTTP_PUT,HTTP_DELETE,HTTP_OPTIONS,HTTP_PROPFIND,HTTP_MOVE,HTTP_COPY};
struct HTTPUpload{};struct HTTPRaw{};
struct WebServer {
 std::map<std::string,String> params;int code=0;String body;
 String arg(const char* key){return params[key];}bool hasArg(const char* key){return params.contains(key);}
 void send(int n,const char* ="",const String& value=""){code=n;body=value;}
};
struct RequestHandler {
 virtual ~RequestHandler()=default;
 virtual bool canHandle(WebServer&,HTTPMethod,const String&){return false;}
 virtual bool canUpload(WebServer&,const String&){return false;}
 virtual bool canRaw(WebServer&,const String&){return false;}
 virtual bool handle(WebServer&,HTTPMethod,const String&){return false;}
};
'''
program = r'''
#include <cassert>
#include <memory>
#include <vector>
#include "network/BookMoveWeb.h"
#include "util/BookCacheUtils.h"
#include <ArduinoJson.h>
void convertFromJson(JsonVariantConst json,String& out){out=String(json.as<const char*>());}
DeserializationError deserializeJson(JsonDocument& doc,const String& input){return deserializeJson(doc,input.c_str());}
namespace FsHelpers { constexpr const char* HIDDEN_ITEMS[]={".crosspoint"}; }
#define FREEINK_DEVICE_X4PRO 1
#include "network/ProtectedPaths.h"
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
namespace bookmove {
bool held=false;Result next=Result::Moved;unsigned calls=0;std::string oldPath,newPath;
bool pending(){return held;}
bool managedBook(const std::string& p){return p.ends_with(".epub");}
Result move(const std::string& a,const std::string& b){++calls;oldPath=a;newPath=b;return next;}
}
unsigned cacheClears=0,plainMoves=0,removals=0;
bool wsUploadInProgress=false;
struct HalFile {
 bool opened=false,dir=false; HalFile openNextFile(){return {};}
 explicit operator bool()const{return opened;}bool isDirectory()const{return dir;}bool close(){opened=false;return true;}
};
struct FakeStorage {
 enum class PathStatus{Missing,Present,Failed};
 std::map<std::string,bool> paths;
 bool exists(const char* p){return paths.contains(p);}
 bool rmdir(const char* p){return paths.erase(p)!=0;}
 HalFile open(const char* p){return {exists(p),paths.contains(p)&&paths[p]};}
 HalFile openChecked(const char* p){return open(p);}
 PathStatus lookupPath(const char* p){return exists(p)?PathStatus::Present:PathStatus::Missing;}
} Storage;
using HalStorage=FakeStorage;
String normalizeWebPath(const String& p){return p;}
bool isProtectedItemName(const String& p){return p.starts_with('.');}
void clearBookCache(const std::string&){++cacheClears;}
bool removeBookFile(const std::string&){++removals;return true;}
const char* bookDeleteIssueMessage(BookDeleteIssue issue){
 if(issue==BookDeleteIssue::InsufficientSpace)return tr(STR_DELETE_NEEDS_SPACE);
 if(issue==BookDeleteIssue::UnsupportedPath)return "rename";return nullptr;
}
bool removeBookFileWithReceipt(const std::string& path, BookDeleteIssue* issue) {
 *issue=path=="/full.epub"?BookDeleteIssue::InsufficientSpace:path=="/long.epub"?BookDeleteIssue::UnsupportedPath:BookDeleteIssue::None;
 if(*issue!=BookDeleteIssue::None || path=="/failed.epub")return false;
 return Storage.paths.erase(path)!=0;
}
struct ClippingStore{static bool moveBook(const std::string&,const std::string&){++plainMoves;return true;}};
struct CrossPointWebServer {
 std::unique_ptr<WebServer> server=std::make_unique<WebServer>();struct{bool file=false;}upload;
 void handleRename()const;void handleMove()const;void handleDelete()const;
};
struct WebDAVHandler {
 String source="/old.epub",dest="/Books/old.epub";bool overwrite=true;
 String getRequestPath(WebServer&){return source;}String getDestinationPath(WebServer&){return dest;}
 bool getOverwrite(WebServer&){return overwrite;}bool isProtectedPath(const String&){return false;}
 void handleMove(WebServer&);
};
''' + method('src/network/CrossPointWebServer.cpp', 'void CrossPointWebServer::handleDelete()') + method('src/network/CrossPointWebServer.cpp', 'void CrossPointWebServer::handleRename()') + method(
    'src/network/CrossPointWebServer.cpp', 'void CrossPointWebServer::handleMove()') + method(
    'src/network/WebDAVHandler.cpp', 'void WebDAVHandler::handleMove(') + r'''
void reset(){Storage.paths={{"/old.epub",false},{"/Books",true}};bookmove::held=false;bookmove::calls=0;
 bookmove::next=bookmove::Result::Moved;cacheClears=plainMoves=removals=0;wsUploadInProgress=false;}
int main(){
 for(auto outcome:{bookmove::Result::Moved,bookmove::Result::Conflict,bookmove::Result::RecoveryRequired}){
  reset();bookmove::next=outcome;CrossPointWebServer web;
  web.server->params={{"path","/old.epub"},{"name","new.epub"}};web.handleRename();
  assert(bookmove::calls==1&&bookmove::oldPath=="/old.epub"&&bookmove::newPath=="/new.epub");
  assert(cacheClears==0&&plainMoves==0&&removals==0);
  reset();bookmove::next=outcome;web.server->params={{"path","/old.epub"},{"dest","/Books"}};web.handleMove();
  assert(bookmove::calls==1&&bookmove::newPath=="/Books/old.epub"&&cacheClears==0&&plainMoves==0);
 }
 reset();CrossPointWebServer web;web.server->params={{"path","/old.epub"},{"dest","/Books"}};
 wsUploadInProgress=true;web.handleMove();assert(web.server->code==423&&bookmove::calls==0);
 bookmove::sendResult(*web.server,bookmove::Result::InsufficientSpace);assert(web.server->code==507);
 Storage.paths={{"/long.epub",false}};web.server->params={{"path","/long.epub"}};
 web.handleDelete();assert(web.server->code==422&&web.server->body.find("rename")!=String::npos&&Storage.exists("/long.epub"));

 Storage.paths={{"/full.epub",false},{"/ok.epub",false},{"/failed.epub",false}};
 web.server->params={{"paths",R"(["/ok.epub","/full.epub","/failed.epub"])"}};
 web.handleDelete();
 assert(web.server->code==507 && web.server->body.find("STR_DELETE_NEEDS_SPACE")!=String::npos);
 assert(web.server->body.find("/failed.epub (deletion failed)")!=String::npos && !Storage.exists("/ok.epub"));
 assert(Storage.exists("/full.epub") && Storage.exists("/failed.epub"));
 web.server->params={{"path","/failed.epub"}};web.handleDelete();assert(web.server->code==500);
 const String queue="/zDev/Library deletions";
 reset();Storage.paths[queue]=true;web.server->params={{"path","/old.epub"},{"dest",queue}};
 web.handleMove();assert(web.server->code==403&&bookmove::calls==0&&plainMoves==0);
 reset();const String held=queue+"/record.epub.held";Storage.paths[held]=false;
 web.server->params={{"path",held},{"name","changed.held"}};
 web.handleRename();assert(web.server->code==403&&bookmove::calls==0&&plainMoves==0);
 reset();WebDAVHandler guarded;WebServer protectedTransport;guarded.dest=queue+"/old.epub";
 guarded.handleMove(protectedTransport);assert(protectedTransport.code==403&&bookmove::calls==0&&removals==0);
 for(bool overwrite:{false,true}){
  reset();WebDAVHandler dav;WebServer transport;dav.overwrite=overwrite;
  Storage.paths[dav.dest]=false;dav.handleMove(transport);
  assert(transport.code==(overwrite?409:412)&&removals==0&&bookmove::calls==0&&cacheClears==0);
 }
 reset();WebDAVHandler dav;WebServer transport;dav.handleMove(transport);
 assert(transport.code==201&&bookmove::calls==1&&removals==0&&cacheClears==0);
 reset();Storage.paths["/old.epub"]=true;dav.handleMove(transport);
 assert(transport.code==409&&bookmove::calls==0&&removals==0);
 bookmove::RecoveryGuard guard;bookmove::held=true;
 for(auto method:{HTTP_POST,HTTP_PUT,HTTP_DELETE,HTTP_MOVE,HTTP_COPY}){
  assert(guard.canHandle(transport,method,"/upload"));
  assert(guard.canUpload(transport,"/upload")&&guard.canRaw(transport,"/dav/file"));
  assert(guard.handle(transport,method,"/upload")&&transport.code==423);
 }
 for(auto method:{HTTP_GET,HTTP_HEAD,HTTP_OPTIONS,HTTP_PROPFIND})assert(!guard.canHandle(transport,method,"/files"));
 assert(!guard.canHandle(transport,HTTP_POST,"/api/books/move/recover"));
 bookmove::held=false;assert(!guard.canHandle(transport,HTTP_POST,"/upload"));
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / 'WebServer.h').write_text(shim)
    (directory / 'I18n.h').write_text('#pragma once\n#define tr(key) #key\n')
    (directory / 'test.cpp').write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(directory), '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'), '-I', str(ROOT / 'src'), str(directory / 'test.cpp'),
                    '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test')], check=True)
print('Production Wi-Fi/DAV routes preserve book-state ownership, conflicts, and streaming recovery guards')
