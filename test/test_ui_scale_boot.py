"""Verify the selected startup scale binds the intended shared UI font slots."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include "components/UIScale.h"
int main() {
  if (uiScaleSpec().bodyFontId != UI_12_FONT_ID) return 1;
  uiscale::publish(static_cast<uint8_t>(uiscale::Value::Small));
  auto small = uiScaleSpec();
  if (small.smallFontId != UI_10_FONT_ID || small.bodyFontId != UI_10_FONT_ID ||
      small.titleFontId != UI_12_FONT_ID) return 2;
  uiscale::publish(static_cast<uint8_t>(uiscale::Value::Large));
  auto large = uiScaleSpec();
  if (large.smallFontId != UI_10_FONT_ID || large.bodyFontId != UI_12_FONT_ID ||
      large.titleFontId != UI_12_FONT_ID) return 3;
  return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    temporary = Path(directory)
    (temporary / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(temporary / 'test.cpp'),
                    '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
print('UI startup font sizes passed')
