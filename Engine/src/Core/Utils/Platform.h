#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <unordered_map>

namespace Conqueror
{
    namespace Platform
    {
        // Platform bilgileri
        CQ_API std::string GetOSName();
        CQ_API std::string GetOSVersion();
        CQ_API std::string GetArchitecture();
        CQ_API std::string GetComputerName();
        CQ_API std::string GetUserName();
        
        // CPU bilgileri
        CQ_API int GetCPUCoreCount();
        CQ_API int GetCPUThreadCount();
        CQ_API std::string GetCPUName();
        
        // Memory bilgileri
        CQ_API uint64_t GetTotalPhysicalMemory();
        CQ_API uint64_t GetAvailablePhysicalMemory();
        CQ_API uint64_t GetProcessMemoryUsage();
        
        // Environment variables
        CQ_API std::string GetEnvironmentVariable(const std::string& name);
        CQ_API bool SetEnvironmentVariable(const std::string& name, const std::string& value);
        CQ_API std::unordered_map<std::string, std::string> GetAllEnvironmentVariables();
        
        // Process bilgileri
        CQ_API uint32_t GetProcessID();
        CQ_API std::string GetProcessPath();
        CQ_API std::string GetWorkingDirectory();
        CQ_API bool SetWorkingDirectory(const std::string& path);
        
        // System paths
        CQ_API std::string GetUserHomeDirectory();
        CQ_API std::string GetUserDocumentsDirectory();
        CQ_API std::string GetUserAppDataDirectory();
        CQ_API std::string GetTempDirectory();
        
        // Clipboard
        CQ_API std::string GetClipboardText();
        CQ_API bool SetClipboardText(const std::string& text);
        
        // System commands
        CQ_API int ExecuteCommand(const std::string& command);
        CQ_API int ExecuteCommandWithOutput(const std::string& command, std::string& output);
        
        // Dialog boxes (platform-specific)
        CQ_API void ShowMessageBox(const std::string& title, const std::string& message, bool error = false);
        CQ_API bool ShowConfirmDialog(const std::string& title, const std::string& message);
        
        // Battery info (laptop/mobile)
        CQ_API bool IsBatteryPowered();
        CQ_API float GetBatteryLevel(); // 0.0 - 1.0
        CQ_API bool IsCharging();
        
        // Network info
        CQ_API bool IsNetworkAvailable();
        CQ_API std::string GetLocalIPAddress();
        
        // Performance
        CQ_API void Sleep(uint32_t milliseconds);
        CQ_API uint64_t GetTickCount(); // Milliseconds since system start
        
        // Debug
        CQ_API bool IsDebuggerAttached();
        CQ_API void DebugBreak();
    }
}
