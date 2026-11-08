#include "LinuxScriptModule.h"
#include "Core/Logging/Log.h"
#if defined(__APPLE__)
// macOS: betik modülü (.dylib) Mach-O biçimindedir, ELF değil
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <libkern/OSByteOrder.h>
#else
#include <elf.h>
#endif
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

namespace Conqueror
{
    LinuxScriptModule::~LinuxScriptModule()
    {
        Unload();
    }

    bool LinuxScriptModule::Load(const std::string& path)
    {
        CQ_CORE_INFO("LinuxScriptModule::Load - Attempting to load: {0}", path);
        
        if (m_ModuleHandle)
        {
            CQ_CORE_WARN("Module already loaded: {0}", path);
            return false;
        }

        CQ_CORE_INFO("LinuxScriptModule::Load - Calling dlopen with RTLD_NOW | RTLD_GLOBAL");
        m_ModuleHandle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        
        if (!m_ModuleHandle)
        {
            const char* error = dlerror();
            CQ_CORE_ERROR("LinuxScriptModule::Load - dlopen FAILED for: {0}", path);
            CQ_CORE_ERROR("LinuxScriptModule::Load - dlerror: {0}", error ? error : "Unknown");
            return false;
        }

        m_LoadPath = path;
        CQ_CORE_INFO("LinuxScriptModule::Load - dlopen SUCCESS, handle: {0}", (void*)m_ModuleHandle);
        CQ_CORE_INFO("Loaded script module: {0}", path);
        return true;
    }

    void LinuxScriptModule::Unload()
    {
        if (m_ModuleHandle)
        {
            CQ_CORE_INFO("LinuxScriptModule::Unload - Calling dlclose, handle: {0}", (void*)m_ModuleHandle);
            dlclose(m_ModuleHandle);
            m_ModuleHandle = nullptr;
            CQ_CORE_INFO("Unloaded script module");
        }
    }

    void* LinuxScriptModule::GetFunction(const std::string& name)
    {
        CQ_CORE_INFO("LinuxScriptModule::GetFunction - Looking for function: {0}", name);
        
        if (!m_ModuleHandle)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetFunction - Module not loaded!");
            return nullptr;
        }

        CQ_CORE_INFO("LinuxScriptModule::GetFunction - Module handle valid: {0}", (void*)m_ModuleHandle);
        
        dlerror(); // Clear error
        CQ_CORE_INFO("LinuxScriptModule::GetFunction - Calling dlsym for: {0}", name);
        void* func = dlsym(m_ModuleHandle, name.c_str());
        
        const char* error = dlerror();
        if (error)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetFunction - dlsym FAILED for: {0}", name);
            CQ_CORE_ERROR("LinuxScriptModule::GetFunction - dlerror: {0}", error);
            return nullptr;
        }

        CQ_CORE_INFO("LinuxScriptModule::GetFunction - dlsym SUCCESS, function pointer: {0}", func);
        return func;
    }

    std::vector<std::string> LinuxScriptModule::GetExportedClassNames()
    {
        std::vector<std::string> classNames;

        if (m_LoadPath.empty())
            return classNames;

        int fd = open(m_LoadPath.c_str(), O_RDONLY);
        if (fd < 0)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetExportedClassNames - Cannot open: {0}", m_LoadPath);
            return classNames;
        }

        struct stat st;
        if (fstat(fd, &st) < 0)
        {
            close(fd);
            return classNames;
        }

        void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if (mapped == MAP_FAILED)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetExportedClassNames - mmap failed for: {0}", m_LoadPath);
            return classNames;
        }

#if defined(__APPLE__)
        // Mach-O: evrensel (fat) dosyada bu işlemcinin dilimi seçilir, LC_SYMTAB'daki tanımlı dış simgelerden "Create*" olanlar
        // sınıf adını verir (C simgelerinin başında '_' vardır)
        const char* base = static_cast<const char*>(mapped);
        size_t size = (size_t)st.st_size;
        if (size >= sizeof(fat_header) && OSSwapBigToHostInt32(reinterpret_cast<const fat_header*>(base)->magic) == FAT_MAGIC)
        {
            const auto* fh = reinterpret_cast<const fat_header*>(base);
            const uint32_t count = OSSwapBigToHostInt32(fh->nfat_arch);
            const auto* archs = reinterpret_cast<const fat_arch*>(base + sizeof(fat_header));
#if defined(__aarch64__) || defined(__arm64__)
            const cpu_type_t want = CPU_TYPE_ARM64;
#else
            const cpu_type_t want = CPU_TYPE_X86_64;
#endif
            const char* slice = nullptr;
            size_t sliceSize = 0;
            for (uint32_t i = 0; i < count && sizeof(fat_header) + (i + 1) * sizeof(fat_arch) <= size; ++i)
                if ((cpu_type_t)OSSwapBigToHostInt32((uint32_t)archs[i].cputype) == want)
                {
                    const uint32_t off = OSSwapBigToHostInt32(archs[i].offset), len = OSSwapBigToHostInt32(archs[i].size);
                    if ((size_t)off + len <= size)
                    {
                        slice = base + off;
                        sliceSize = len;
                    }
                }
            base = slice;
            size = sliceSize;
        }
        if (!base || size < sizeof(mach_header_64) || reinterpret_cast<const mach_header_64*>(base)->magic != MH_MAGIC_64)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetExportedClassNames - Not a valid 64-bit Mach-O file");
            munmap(mapped, st.st_size);
            return classNames;
        }
        const auto* mh = reinterpret_cast<const mach_header_64*>(base);
        size_t cmdOffset = sizeof(mach_header_64);
        for (uint32_t i = 0; i < mh->ncmds && cmdOffset + sizeof(load_command) <= size; ++i)
        {
            const auto* lc = reinterpret_cast<const load_command*>(base + cmdOffset);
            if (lc->cmd == LC_SYMTAB && cmdOffset + sizeof(symtab_command) <= size)
            {
                const auto* sym = reinterpret_cast<const symtab_command*>(lc);
                if ((size_t)sym->symoff + (size_t)sym->nsyms * sizeof(nlist_64) <= size && (size_t)sym->stroff + sym->strsize <= size)
                {
                    const auto* syms = reinterpret_cast<const nlist_64*>(base + sym->symoff);
                    const char* strs = base + sym->stroff;
                    for (uint32_t k = 0; k < sym->nsyms; ++k)
                    {
                        if (!(syms[k].n_type & N_EXT) || (syms[k].n_type & N_TYPE) != N_SECT || syms[k].n_un.n_strx >= sym->strsize)
                            continue;
                        const char* name = strs + syms[k].n_un.n_strx;
                        if (name[0] == '_')
                            ++name;
                        if (strncmp(name, "Create", 6) == 0 && strlen(name) > 6)
                        {
                            classNames.emplace_back(name + 6);
                            CQ_CORE_INFO("LinuxScriptModule::GetExportedClassNames - Found: {0} -> class: {1}", name, name + 6);
                        }
                    }
                }
            }
            if (lc->cmdsize == 0)
                break;
            cmdOffset += lc->cmdsize;
        }
        munmap(mapped, st.st_size);
        return classNames;
#else
        auto* ehdr = static_cast<Elf64_Ehdr*>(mapped);
        if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetExportedClassNames - Not a valid ELF file");
            munmap(mapped, st.st_size);
            return classNames;
        }

        auto* shdr = reinterpret_cast<Elf64_Shdr*>(static_cast<char*>(mapped) + ehdr->e_shoff);
        const char* shstrtab = static_cast<const char*>(mapped) + shdr[ehdr->e_shstrndx].sh_offset;

        // .dynsym ve .dynstr section'larını bul
        Elf64_Sym* dynsym = nullptr;
        const char* dynstr = nullptr;
        size_t dynsymCount = 0;

        for (int i = 0; i < ehdr->e_shnum; i++)
        {
            if (shdr[i].sh_type == SHT_DYNSYM)
            {
                dynsym = reinterpret_cast<Elf64_Sym*>(static_cast<char*>(mapped) + shdr[i].sh_offset);
                dynsymCount = shdr[i].sh_size / shdr[i].sh_entsize;
            }
            else if (strcmp(&shstrtab[shdr[i].sh_name], ".dynstr") == 0)
            {
                dynstr = static_cast<const char*>(mapped) + shdr[i].sh_offset;
            }
        }

        if (dynsym && dynstr)
        {
            for (size_t i = 0; i < dynsymCount; i++)
            {
                const char* name = &dynstr[dynsym[i].st_name];
                if (name[0] == '\0')
                    continue;

                if (strncmp(name, "Create", 6) == 0 && strlen(name) > 6)
                {
                    classNames.emplace_back(name + 6);
                    CQ_CORE_INFO("LinuxScriptModule::GetExportedClassNames - Found: {0} -> class: {1}", name, name + 6);
                }
            }
        }

        munmap(mapped, st.st_size);
        return classNames;
#endif
    }
}
