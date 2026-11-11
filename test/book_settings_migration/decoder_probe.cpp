#include <CrossInkBookSettings.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.size() % 2) return 1;
    std::vector<uint8_t> data;
    for (size_t i = 0; i < line.size(); i += 2)
      data.push_back(static_cast<uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)));
    crossink::BookSettingsPreview result;
    if (!crossink::decodeBookSettings(data.data(), data.size(), result)) {
      std::cout << "invalid\n";
      continue;
    }
    std::cout << result.nativeMask;
    for (const auto value : result.values) std::cout << ' ' << static_cast<unsigned>(value);
    std::cout << ' ' << result.unmappedMask << ' ' << static_cast<unsigned>(result.extraFlags) << '\n';
  }
}
