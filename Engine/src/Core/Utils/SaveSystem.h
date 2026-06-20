#pragma once
#include "Core/Base/Base.h"
#include <string>

namespace Conqueror::Utils {
    class CQ_API SaveSystem {
    public:
        // Returns true if save was successful
        static bool SaveGame(const std::string& filename, const std::string& jsonData);
        
        // Returns the saved JSON data, or an empty string if failed
        static std::string LoadGame(const std::string& filename);
    };
}
