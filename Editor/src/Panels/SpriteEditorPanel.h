#pragma once

#include "Renderer/SpriteSheet.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Texture2D;
}

namespace Conqueror::Editor
{
    // Unity Sprite Editor karşılığı: otomatik / ızgara dilimleme, elle dikdörtgen
    // oluşturma ve düzenleme (taşı, boyutlandır), pivot, 9-slice kenarlıkları,
    // kırpma, geri al/yinele; sonuç "<doku>.cqsprite" olarak kaydedilir.
    class SpriteEditorPanel
    {
    public:
        SpriteEditorPanel() = default;

        void OnImGuiRender(bool* open);
        bool Open(const std::filesystem::path& texturePath);
        bool Apply();
        void Revert();

        const std::filesystem::path& GetTexturePath() const { return m_TexturePath; }
        SpriteSheet& GetSheet() { return m_Sheet; }
        bool IsDirty() const { return m_Dirty; }

        // Otomasyon/test için dilimleme
        void SliceGridBySize(glm::ivec2 cell, glm::ivec2 offset = {}, glm::ivec2 padding = {});
        void SliceGridByCount(glm::ivec2 count);
        void SliceAutomatic();

    private:
        enum class DragMode
        {
            None, Create, Move, Pan, Pivot,
            ResizeL, ResizeR, ResizeB, ResizeT, ResizeLB, ResizeRB, ResizeLT, ResizeRT,
            BorderL, BorderR, BorderB, BorderT
        };

        void DrawToolbar();
        void DrawCanvas();
        void DrawSidebar();
        void DrawSlicePopup();

        glm::vec2 TexToScreen(const glm::vec2& px) const;
        glm::vec2 ScreenToTex(const glm::vec2& screen) const;
        void FrameAll();

        void PushUndo();
        void Undo();
        void Redo();
        void MarkDirty() { m_Dirty = true; }
        void AddSprites(const std::vector<glm::vec4>& rects, bool replace);
        glm::vec2 PivotPreset(int preset) const;

        std::filesystem::path m_TexturePath;
        std::shared_ptr<Texture2D> m_Texture;
        std::vector<uint8_t> m_Pixels; // RGBA8, satır 0 = üst
        int m_Width = 0, m_Height = 0;

        SpriteSheet m_Sheet;
        bool m_ReadOnly = false;   // Packages varlığı: yalnız görüntülenir (Unity: "the asset is not editable")
        bool m_Dirty = false;
        int m_Selected = -1;

        std::vector<std::vector<SpriteDef>> m_UndoStack;
        std::vector<std::vector<SpriteDef>> m_RedoStack;

        // Görünüm
        ImVec2 m_CanvasMin{ 0, 0 }, m_CanvasSize{ 0, 0 };
        glm::vec2 m_Pan{ 0.0f };
        float m_Zoom = 1.0f;
        bool m_RequestFrame = true;
        bool m_ShowGrid = true;

        // Etkileşim
        DragMode m_Drag = DragMode::None;
        glm::vec2 m_DragStartTex{ 0.0f };
        glm::vec4 m_DragStartRect{ 0.0f };
        glm::vec4 m_DragStartBorder{ 0.0f };
        bool m_DragPushedUndo = false;

        // Dilimleme ayarları
        int m_SliceType = 0;       // 0 Otomatik, 1 Hücre boyutu, 2 Hücre sayısı
        glm::ivec2 m_CellSize{ 32, 32 };
        glm::ivec2 m_CellCount{ 4, 4 };
        glm::ivec2 m_Offset{ 0, 0 };
        glm::ivec2 m_Padding{ 0, 0 };
        int m_SlicePivot = 0;      // ön ayar indeksi
        bool m_SliceKeepExisting = false;
        bool m_SkipEmpty = true;
        int m_MinSize = 4;

        std::vector<SpriteDef> m_SidebarSnapshot;
        bool m_SidebarEditing = false;

        bool m_ConfirmDiscard = false;
        std::filesystem::path m_PendingOpen;
    };
}
