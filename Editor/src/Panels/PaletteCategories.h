#pragma once

#include <functional>
#include <string>
#include <vector>

// Evrensel Palette (199): eski Tile Palette paneli artık "Palette"; üstteki düğmeler UI | Blueprint | Shader | Tiles. Her kategori kendi
// düzenleyicisine öğe ekler (UI Editor'e UI öğesi ya da kullanıcının UI prefab'i, görsel betiğe düğüm, Shader Graph'a düğüm, Tiles = eski
// karo paleti); düzenleyicisi açık değilken düğme soluktur. Palette çalışılan düzenleyiciye kendiliğinden geçer. Arama, favoriler,
// son kullanılanlar, liste / büyük kutu görünümü ve Library | User Created süzgeci (Unity Library "Project", Unreal "User Created").
namespace Conqueror::Editor::Palette
{
    enum Category : int { UI = 0, Blueprint, Shader, Tiles, Count };

    struct Hooks
    {
        std::function<bool()> UIEditorOpen, BlueprintOpen, ShaderOpen;
        std::function<void(const std::string&)> AddUI;              // "Button" ya da "prefab:Assets/UI/HealthBar.cqprefab"
        std::function<void(const std::string&)> AddBlueprintNode;   // NodeDef::Id
        std::function<void(int)> AddShaderNode;                     // ShaderGraphNodeType
    };
    Hooks& GetHooks();

    int GetCategory();
    void SetCategory(int category);
    // Odaktaki düzenleyiciye göre kategoriyi değiştirir (her kare)
    void FollowFocus();
    // Panelin üstündeki düğmeler; Tiles seçiliyse true (karo paleti kendi içeriğini çizer), değilse kategoriyi çizer
    bool DrawCategoryBar();
    void DrawCategory();

    // UI öğeleri (Library) ve kullanıcının UI prefab'leri (User Created)
    std::vector<std::string> LibraryUIItems();
    std::vector<std::string> UserUIItems(bool rescan = false);
    void NoteUsed(const std::string& item);
}
