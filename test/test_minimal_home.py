"""Check compact Home production rendering and stable theme choices."""
from pathlib import Path
import runpy
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
runpy.run_path(str(root/'test/test_compact_home_render.py'), run_name='__main__')

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
