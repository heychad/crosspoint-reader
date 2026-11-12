"""Exercise the Minimal Home rendering path with a small fake framebuffer."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/components/themes/minimal/MinimalHomeTheme.cpp").read_text()
function = source[source.index("Rect MinimalHomeTheme::coverRect"):]
program = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
struct Rect { int x,y,width,height; };
struct RecentBook { std::string title,author,coverBmpPath; };
namespace MinimalHomeMetrics { struct M { int homeCoverHeight=300; }; constexpr M values; }
namespace EpdFontFamily { enum Style { REGULAR, BOLD }; }
constexpr int UI_10_FONT_ID=10;
struct Scale { int bodyFontId=12; }; Scale uiScaleSpec(){ return {}; }
struct HalFile { bool close(){return true;} };
struct StorageFake { bool present=false; int opens=0; bool openFileForRead(const char*,const std::string&,HalFile&){ ++opens; return present; } } Storage;
enum class BmpReaderError { Ok, Bad };
struct Bitmap { explicit Bitmap(HalFile&){} BmpReaderError parseHeaders(){return BmpReaderError::Ok;} };
struct UITheme { static std::string getCoverThumbPath(const std::string& s,int){return s;} };
struct GfxRenderer {
 int selection=0,texts=0; Rect selectionRect{};
 void drawRect(int x,int y,int w,int h,bool){++selection;selectionRect={x,y,w,h};}
 std::string truncatedText(int,const char* text,int width,EpdFontFamily::Style=EpdFontFamily::REGULAR){assert(width>0);return text;}
 void drawCenteredText(int,int,const char*,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR){++texts;}
 int getLineHeight(int){return 12;}
};
struct MinimalHomeTheme {
 static Rect coverRect(Rect);
 mutable int placeholders=0,images=0,empty=0;
 void drawEmptyRecents(GfxRenderer&,Rect)const{++empty;}
 void drawCoverPlaceholder(GfxRenderer&,Rect)const{++placeholders;}
 bool drawCoverThumbFill(GfxRenderer&,Bitmap&,Rect)const{++images;return true;}
 void drawRecentBookCover(GfxRenderer&,Rect,const std::vector<RecentBook>&,int,bool&,bool&,bool&,std::function<bool()>)const;
};
''' + function + r'''
int main(){
 MinimalHomeTheme theme; GfxRenderer renderer; bool rendered=false,stored=false,restored=false; int saves=0;
 std::vector<RecentBook> none, books{{"Title","Author","cover"}};
 auto save=[&]{++saves;return true;}; Rect tile{10,56,460,390};
 theme.drawRecentBookCover(renderer,tile,none,0,rendered,stored,restored,save);
 assert(theme.empty==1 && saves==0);
 Storage.present=true;
 theme.drawRecentBookCover(renderer,tile,books,0,rendered,stored,restored,save);
 assert(theme.images==1 && saves==1 && rendered && stored && renderer.selection==1 && renderer.texts==2);
 assert(renderer.selectionRect.x>=tile.x && renderer.selectionRect.x+renderer.selectionRect.width<=tile.x+tile.width);
 assert(renderer.selectionRect.y>=tile.y && renderer.selectionRect.y+renderer.selectionRect.height<=tile.y+tile.height);
 restored=true; theme.drawRecentBookCover(renderer,tile,books,1,rendered,stored,restored,save);
 assert(theme.images==1 && saves==1 && renderer.selection==1 && renderer.texts==4);
 restored=false; Storage.present=false;
 theme.drawRecentBookCover(renderer,tile,books,1,rendered,stored,restored,[&]{++saves;return false;});
 assert(theme.placeholders==1 && saves==2 && !rendered && !stored);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++20", str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("Minimal Home renders, restores, and falls back correctly")

settings = (root / "src/SettingsList.h").read_text()
start = settings.index("inline std::vector<StrId> homeThemeValues()")
end = settings.index("\n}\n", start) + 2
settings_program = r'''
#include <cassert>
#include <iterator>
#include <vector>
enum class StrId { STR_THEME_CLASSIC, STR_THEME_LYRA, STR_THEME_LYRA_EXTENDED,
 STR_THEME_ROUNDEDRAFF, STR_THEME_COVER_GRID, STR_THEME_MINIMAL_HOME, STR_THEME_DASHBOARD_HOME };
struct UITheme { static bool psram; static bool supportsCoverGrid(){return psram;} }; bool UITheme::psram=false;
''' + settings[start:end] + r'''
int main(){
 auto low=homeThemeValues(); assert(low.size()==4 && low[3]==StrId::STR_THEME_ROUNDEDRAFF);
 UITheme::psram=true; auto high=homeThemeValues();
 assert(high.size()==7 && high[4]==StrId::STR_THEME_COVER_GRID && high[5]==StrId::STR_THEME_MINIMAL_HOME && high[6]==StrId::STR_THEME_DASHBOARD_HOME);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "settings.cpp").write_text(settings_program)
    subprocess.run(["c++", "-std=c++20", str(directory / "settings.cpp"), "-o", str(directory / "settings")], check=True)
    subprocess.run([str(directory / "settings")], check=True)
print("Theme choices preserve saved indexes")
