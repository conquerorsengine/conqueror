#include "AndroidSigning.h"

#include "Core/Automation/Automation.h"
#include "Core/Project/PlatformModules.h"
#include "Core/Project/Project.h"
#include "Core/Utils/Platform.h"
#include "Core/Utils/ProcessRunner.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace Conqueror::Editor::AndroidSigning
{
    namespace fs = std::filesystem;

    bool CreateKeystore(const fs::path& file, const std::string& alias, const std::string& password, const std::string& owner, int years,
                        std::string& error)
    {
        std::error_code ec;
        if (password.size() < 6) { error = "The password needs at least 6 characters."; return false; }
        if (alias.empty() || alias.find('"') != std::string::npos) { error = "The key alias is not valid."; return false; }
        if (fs::exists(file, ec)) { error = file.string() + " already exists."; return false; }
        std::string keytool = PlatformModules::FindProgram("keytool");
        if (keytool.empty())
            if (const char* jh = std::getenv("JAVA_HOME"); jh && *jh)
                if (fs::exists(fs::path(jh) / "bin" / "keytool", ec) || fs::exists(fs::path(jh) / "bin" / "keytool.exe", ec))
                    keytool = (fs::path(jh) / "bin" / "keytool").string();
        if (keytool.empty()) { error = "keytool not found: install a JDK (or the Java module from the Hub)."; return false; }
        fs::create_directories(file.parent_path(), ec);
        std::string name = owner.empty() ? std::string("Conqueror Game") : owner;
        for (char& c : name)
            if (c == '"' || c == ',' || c == '=')
                c = ' ';
        Platform::SetEnvironmentVariable("CQ_KEYSTORE_NEW_PASS", password);
        const std::string cmd = "\"" + keytool + "\" -genkeypair -keystore \"" + file.string() + "\" -alias \"" + alias +
                                "\" -keyalg RSA -keysize 2048 -validity " + std::to_string(std::max(1, years) * 365) +
                                " -storepass:env CQ_KEYSTORE_NEW_PASS -keypass:env CQ_KEYSTORE_NEW_PASS -dname \"CN=" + name + ", O=" + name + "\"";
        ProcessRunner runner;
        std::string output;
        if (runner.Start(cmd, file.parent_path()))
        {
            while (runner.IsRunning())
            {
                output += runner.ReadAvailable();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            output += runner.ReadAvailable();
        }
        Platform::SetEnvironmentVariable("CQ_KEYSTORE_NEW_PASS", "");
        if (!fs::exists(file, ec))
        {
            error = "keytool failed: " + output;
            return false;
        }
        return true;
    }

    void RegisterAutomation()
    {
        Automation::RegisterCommand("android_keystore", [](const Automation::CommandArgs& a, std::string& err) {
            auto project = Project::GetActive();
            if (!project) { err = "no project"; return false; }
            auto& android = project->GetConfig().Player.Android;
            if (a.size() > 4 && a[1] == "create")
            {
                if (!CreateKeystore(Project::GetActiveProjectDirectory() / a[2], a[3], a[4], "Regression", 25, err))
                    return false;
                android.Keystore = a[2];
                android.KeyAlias = a[3];
                return true;
            }
            if (a.size() > 3 && a[1] == "use") { android.Keystore = a[2]; android.KeyAlias = a[3]; return true; }
            err = "android_keystore create <file> <alias> <password> | use <file> <alias>";
            return false;
        });
    }
}
