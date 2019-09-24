#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <fstream>

#define USE_DETECT_PATH_2

struct PtgOverlayConfig {
  std::string root_path;

  void Load() {
    char path[256];
    DWORD hr = GetCurrentDirectoryA(sizeof(path), path);
    if (SUCCEEDED(hr)) {
      printf("[PtgOverlayConfig::Load] Current directory: %s\n", path);
    }

    std::ifstream c("./ptg_overlay.conf");
    if (c.good()) {
      while (c.good()) {
        std::string line;
        std::getline(c, line);

        const int x = int(line.find('='));
        std::string key = line.substr(0, x), value = line.substr(x + 1);

        std::transform(key.begin(), key.end(), key.begin(),
          [](unsigned char c) { return std::tolower(c); });

        if (key == "root_path") {
          root_path = value;
        }
      }
    }
  }
};