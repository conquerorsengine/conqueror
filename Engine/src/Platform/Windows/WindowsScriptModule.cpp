#include "WindowsScriptModule.h"

#ifdef CQ_PLATFORM_WINDOWS

#include "Core/Logging/Log.h"
#include <cstring>

namespace Conqueror
{
    WindowsScriptModule::~WindowsScriptModule()
    {
        Unload();
    }

    bool WindowsScriptModule::Load(const std::string& path)
    {
        if (m_ModuleHandle)
        {
            CQ_CORE_WARN("Module already loaded: {0}", path);
            return false;
        }

        m_ModuleHandle = LoadLibraryA(path.c_str());
        
        if (!m_ModuleHandle)
        {
            DWORD error = GetLastError();
            CQ_CORE_ERROR("Failed to load module: {0}, Error: {1}", path, error);
            return false;
        }

        m_LoadPath = path;
        CQ_CORE_INFO("Loaded script module: {0}", path);
        return true;
    }

    void WindowsScriptModule::Unload()
    {
        if (m_ModuleHandle)
        {
            FreeLibrary(m_ModuleHandle);
            m_ModuleHandle = nullptr;
            CQ_CORE_INFO("Unloaded script module");
        }
    }

    void* WindowsScriptModule::GetFunction(const std::string& name)
    {
        if (!m_ModuleHandle)
        {
            CQ_CORE_ERROR("Module not loaded");
            return nullptr;
        }

        void* func = (void*)GetProcAddress(m_ModuleHandle, name.c_str());
        
        if (!func)
        {
            CQ_CORE_ERROR("Function not found: {0}", name);
        }

        return func;
    }

    std::vector<std::string> WindowsScriptModule::GetExportedClassNames()
    {
        std::vector<std::string> classNames;

        if (!m_ModuleHandle)
            return classNames;

        BYTE* base = (BYTE*)m_ModuleHandle;
        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)base;
        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);

        IMAGE_DATA_DIRECTORY exportDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (exportDir.Size == 0)
            return classNames;

        IMAGE_EXPORT_DIRECTORY* exportDirectory = (IMAGE_EXPORT_DIRECTORY*)(base + exportDir.VirtualAddress);
        DWORD* nameOffsets = (DWORD*)(base + exportDirectory->AddressOfNames);

        for (DWORD i = 0; i < exportDirectory->NumberOfNames; i++)
        {
            const char* name = (const char*)(base + nameOffsets[i]);
            if (strncmp(name, "Create", 6) == 0 && strlen(name) > 6)
            {
                classNames.emplace_back(name + 6);
                CQ_CORE_INFO("WindowsScriptModule::GetExportedClassNames - Found: {0} -> class: {1}", name, name + 6);
            }
        }

        return classNames;
    }
}

#endif
