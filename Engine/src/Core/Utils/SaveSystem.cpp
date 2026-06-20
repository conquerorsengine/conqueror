#include "SaveSystem.h"
#include <fstream>
#include <sstream>

namespace Conqueror::Utils {
    bool SaveSystem::SaveGame(const std::string& filename, const std::string& jsonData) {
        if (filename.empty()) return false;
        // Basic file writing
        std::ofstream out(filename);
        if (!out.is_open()) return false;
        out << jsonData;
        out.close();
        return true;
    }

    std::string SaveSystem::LoadGame(const std::string& filename) {
        if (filename.empty()) return "";
        std::ifstream in(filename);
        if (!in.is_open()) return "";
        std::stringstream buffer;
        buffer << in.rdbuf();
        in.close();
        return buffer.str();
    }
}
