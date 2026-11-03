#include "ImportActivity.h"

#include <chrono>
#include <mutex>

namespace Conqueror::ImportActivity
{
    namespace
    {
        std::vector<Record>& Storage()
        {
            static std::vector<Record> s;
            return s;
        }
        std::mutex& Lock()
        {
            static std::mutex m;
            return m;
        }
        uint64_t s_Version = 0;
        const auto s_Start = std::chrono::steady_clock::now();
    }

    void Add(const std::string& path, const std::string& importer, double milliseconds, bool ok, const std::string& info, bool reimport)
    {
        std::lock_guard<std::mutex> lock(Lock());
        auto& v = Storage();
        if (v.size() >= 5000)
            v.erase(v.begin(), v.begin() + 1000);
        Record r;
        r.Path = path;
        r.Importer = importer;
        r.Info = info;
        r.Milliseconds = milliseconds;
        r.Time = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();
        r.Reimport = reimport;
        r.Ok = ok;
        v.push_back(std::move(r));
        ++s_Version;
    }

    const std::vector<Record>& Records() { return Storage(); }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(Lock());
        Storage().clear();
        ++s_Version;
    }

    uint64_t Version() { return s_Version; }
}
