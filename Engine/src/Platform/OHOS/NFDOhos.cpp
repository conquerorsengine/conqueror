// HarmonyOS editörü: Native File Dialog (nfd) API'si sistemin belge seçicisine bağlanır (ArkTS DocumentViewPicker, Index.ets).
// Açma / çoklu açma / kaydetme / klasör seçme istekleri motor iş parçacığından ArkTS'e gider; seçici kapanana kadar beklenir.
// Seçilen belgelerin URI'leri ArkTS'te yola çevrilir (fileUri.FileUri.path, ör. /storage/Users/currentUser/Documents/...);
// kum havuzu dışındaki yollara erişim seçicinin verdiği geçici izinle olur.
#if defined(__OHOS__)

#include "OHOSPlatform.h"

#include <nfd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    std::string s_Error;

    // nfd süzgeci: { "Görüntü", "png,jpg" } -> ".png", ".jpg"
    std::vector<std::string> Extensions(const nfdu8filteritem_t* filters, nfdfiltersize_t count)
    {
        std::vector<std::string> out;
        for (nfdfiltersize_t i = 0; filters && i < count; ++i)
        {
            const std::string spec = filters[i].spec ? filters[i].spec : "";
            size_t start = 0;
            while (start <= spec.size())
            {
                size_t end = spec.find(',', start);
                if (end == std::string::npos)
                    end = spec.size();
                std::string ext = spec.substr(start, end - start);
                while (!ext.empty() && ext.front() == ' ') ext.erase(ext.begin());
                while (!ext.empty() && ext.back() == ' ') ext.pop_back();
                if (!ext.empty() && ext != "*")
                    out.push_back(ext.front() == '.' ? ext : "." + ext);
                start = end + 1;
            }
        }
        return out;
    }

    nfdu8char_t* Duplicate(const std::string& s)
    {
        auto* out = static_cast<nfdu8char_t*>(std::malloc(s.size() + 1));
        if (out)
            std::memcpy(out, s.c_str(), s.size() + 1);
        return out;
    }

    nfdresult_t Pick(Conqueror::OHOS::PickerMode mode, const nfdu8filteritem_t* filters, nfdfiltersize_t count, const nfdu8char_t* defaultPath,
                     const nfdu8char_t* defaultName, std::vector<std::string>& paths)
    {
        std::string error;
        if (Conqueror::OHOS::ShowPicker(mode, Extensions(filters, count), defaultPath ? defaultPath : "", defaultName ? defaultName : "", paths, error))
            return NFD_OKAY;
        if (!error.empty())
        {
            s_Error = error;
            return NFD_ERROR;
        }
        return NFD_CANCEL;
    }

    nfdresult_t Single(Conqueror::OHOS::PickerMode mode, nfdu8char_t** outPath, const nfdu8filteritem_t* filters, nfdfiltersize_t count,
                       const nfdu8char_t* defaultPath, const nfdu8char_t* defaultName)
    {
        if (!outPath)
            return NFD_ERROR;
        *outPath = nullptr;
        std::vector<std::string> paths;
        const nfdresult_t result = Pick(mode, filters, count, defaultPath, defaultName, paths);
        if (result != NFD_OKAY)
            return result;
        *outPath = Duplicate(paths.front());
        return *outPath ? NFD_OKAY : NFD_ERROR;
    }

    nfdresult_t Multiple(Conqueror::OHOS::PickerMode mode, const nfdpathset_t** outPaths, const nfdu8filteritem_t* filters, nfdfiltersize_t count,
                         const nfdu8char_t* defaultPath)
    {
        if (!outPaths)
            return NFD_ERROR;
        *outPaths = nullptr;
        std::vector<std::string> paths;
        const nfdresult_t result = Pick(mode, filters, count, defaultPath, nullptr, paths);
        if (result != NFD_OKAY)
            return result;
        *outPaths = new std::vector<std::string>(std::move(paths));
        return NFD_OKAY;
    }

    const std::vector<std::string>* Set(const nfdpathset_t* set) { return static_cast<const std::vector<std::string>*>(set); }
}

extern "C"
{
    nfdresult_t NFD_Init(void) { return NFD_OKAY; }
    void NFD_Quit(void) {}
    const char* NFD_GetError(void) { return s_Error.empty() ? nullptr : s_Error.c_str(); }
    void NFD_ClearError(void) { s_Error.clear(); }

    void NFD_FreePathU8(nfdu8char_t* filePath) { std::free(filePath); }
    void NFD_FreePathN(nfdnchar_t* filePath) { std::free(filePath); }

    nfdresult_t NFD_OpenDialogU8(nfdu8char_t** outPath, const nfdu8filteritem_t* filterList, nfdfiltersize_t filterCount, const nfdu8char_t* defaultPath)
    {
        return Single(Conqueror::OHOS::PickerMode::OpenFile, outPath, filterList, filterCount, defaultPath, nullptr);
    }
    nfdresult_t NFD_OpenDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t* filterList, nfdfiltersize_t filterCount, const nfdnchar_t* defaultPath)
    {
        return NFD_OpenDialogU8(outPath, filterList, filterCount, defaultPath);
    }
    nfdresult_t NFD_OpenDialogU8_With_Impl(nfdversion_t, nfdu8char_t** outPath, const nfdopendialogu8args_t* args)
    {
        return args ? NFD_OpenDialogU8(outPath, args->filterList, args->filterCount, args->defaultPath) : NFD_ERROR;
    }
    nfdresult_t NFD_OpenDialogN_With_Impl(nfdversion_t version, nfdnchar_t** outPath, const nfdopendialognargs_t* args)
    {
        return NFD_OpenDialogU8_With_Impl(version, outPath, args);
    }

    nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t** outPaths, const nfdu8filteritem_t* filterList, nfdfiltersize_t filterCount,
                                         const nfdu8char_t* defaultPath)
    {
        return Multiple(Conqueror::OHOS::PickerMode::OpenFiles, outPaths, filterList, filterCount, defaultPath);
    }
    nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t** outPaths, const nfdnfilteritem_t* filterList, nfdfiltersize_t filterCount,
                                        const nfdnchar_t* defaultPath)
    {
        return NFD_OpenDialogMultipleU8(outPaths, filterList, filterCount, defaultPath);
    }
    nfdresult_t NFD_OpenDialogMultipleU8_With_Impl(nfdversion_t, const nfdpathset_t** outPaths, const nfdopendialogu8args_t* args)
    {
        return args ? NFD_OpenDialogMultipleU8(outPaths, args->filterList, args->filterCount, args->defaultPath) : NFD_ERROR;
    }
    nfdresult_t NFD_OpenDialogMultipleN_With_Impl(nfdversion_t version, const nfdpathset_t** outPaths, const nfdopendialognargs_t* args)
    {
        return NFD_OpenDialogMultipleU8_With_Impl(version, outPaths, args);
    }

    nfdresult_t NFD_SaveDialogU8(nfdu8char_t** outPath, const nfdu8filteritem_t* filterList, nfdfiltersize_t filterCount,
                                 const nfdu8char_t* defaultPath, const nfdu8char_t* defaultName)
    {
        return Single(Conqueror::OHOS::PickerMode::SaveFile, outPath, filterList, filterCount, defaultPath, defaultName);
    }
    nfdresult_t NFD_SaveDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t* filterList, nfdfiltersize_t filterCount,
                                const nfdnchar_t* defaultPath, const nfdnchar_t* defaultName)
    {
        return NFD_SaveDialogU8(outPath, filterList, filterCount, defaultPath, defaultName);
    }
    nfdresult_t NFD_SaveDialogU8_With_Impl(nfdversion_t, nfdu8char_t** outPath, const nfdsavedialogu8args_t* args)
    {
        return args ? NFD_SaveDialogU8(outPath, args->filterList, args->filterCount, args->defaultPath, args->defaultName) : NFD_ERROR;
    }
    nfdresult_t NFD_SaveDialogN_With_Impl(nfdversion_t version, nfdnchar_t** outPath, const nfdsavedialognargs_t* args)
    {
        return NFD_SaveDialogU8_With_Impl(version, outPath, args);
    }

    nfdresult_t NFD_PickFolderU8(nfdu8char_t** outPath, const nfdu8char_t* defaultPath)
    {
        return Single(Conqueror::OHOS::PickerMode::PickFolder, outPath, nullptr, 0, defaultPath, nullptr);
    }
    nfdresult_t NFD_PickFolderN(nfdnchar_t** outPath, const nfdnchar_t* defaultPath) { return NFD_PickFolderU8(outPath, defaultPath); }
    nfdresult_t NFD_PickFolderU8_With_Impl(nfdversion_t, nfdu8char_t** outPath, const nfdpickfolderu8args_t* args)
    {
        return NFD_PickFolderU8(outPath, args ? args->defaultPath : nullptr);
    }
    nfdresult_t NFD_PickFolderN_With_Impl(nfdversion_t version, nfdnchar_t** outPath, const nfdpickfoldernargs_t* args)
    {
        return NFD_PickFolderU8_With_Impl(version, outPath, args);
    }
    // Sistem seçicisi tek klasör verir
    nfdresult_t NFD_PickFolderMultipleU8(const nfdpathset_t** outPaths, const nfdu8char_t* defaultPath)
    {
        return Multiple(Conqueror::OHOS::PickerMode::PickFolder, outPaths, nullptr, 0, defaultPath);
    }
    nfdresult_t NFD_PickFolderMultipleN(const nfdpathset_t** outPaths, const nfdnchar_t* defaultPath)
    {
        return NFD_PickFolderMultipleU8(outPaths, defaultPath);
    }
    nfdresult_t NFD_PickFolderMultipleU8_With_Impl(nfdversion_t, const nfdpathset_t** outPaths, const nfdpickfolderu8args_t* args)
    {
        return NFD_PickFolderMultipleU8(outPaths, args ? args->defaultPath : nullptr);
    }
    nfdresult_t NFD_PickFolderMultipleN_With_Impl(nfdversion_t version, const nfdpathset_t** outPaths, const nfdpickfoldernargs_t* args)
    {
        return NFD_PickFolderMultipleU8_With_Impl(version, outPaths, args);
    }

    nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t* pathSet, nfdpathsetsize_t* count)
    {
        if (!pathSet || !count)
            return NFD_ERROR;
        *count = (nfdpathsetsize_t)Set(pathSet)->size();
        return NFD_OKAY;
    }
    nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdu8char_t** outPath)
    {
        if (!pathSet || !outPath || index >= Set(pathSet)->size())
            return NFD_ERROR;
        *outPath = Duplicate((*Set(pathSet))[index]);
        return *outPath ? NFD_OKAY : NFD_ERROR;
    }
    nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdnchar_t** outPath)
    {
        return NFD_PathSet_GetPathU8(pathSet, index, outPath);
    }
    void NFD_PathSet_FreePathU8(const nfdu8char_t* filePath) { std::free(const_cast<nfdu8char_t*>(filePath)); }
    void NFD_PathSet_FreePathN(const nfdnchar_t* filePath) { std::free(const_cast<nfdnchar_t*>(filePath)); }
    void NFD_PathSet_Free(const nfdpathset_t* pathSet) { delete Set(pathSet); }

    nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t* pathSet, nfdpathsetenum_t* outEnumerator)
    {
        if (!pathSet || !outEnumerator)
            return NFD_ERROR;
        outEnumerator->ptr = new std::pair<const std::vector<std::string>*, size_t>(Set(pathSet), 0);
        return NFD_OKAY;
    }
    void NFD_PathSet_FreeEnum(nfdpathsetenum_t* enumerator)
    {
        if (enumerator)
            delete static_cast<std::pair<const std::vector<std::string>*, size_t>*>(enumerator->ptr);
    }
    nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t* enumerator, nfdu8char_t** outPath)
    {
        if (!enumerator || !enumerator->ptr || !outPath)
            return NFD_ERROR;
        auto* state = static_cast<std::pair<const std::vector<std::string>*, size_t>*>(enumerator->ptr);
        *outPath = state->second < state->first->size() ? Duplicate((*state->first)[state->second++]) : nullptr;
        return NFD_OKAY;
    }
    nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t* enumerator, nfdnchar_t** outPath) { return NFD_PathSet_EnumNextU8(enumerator, outPath); }
}

#endif
