#pragma once

#include "Renderer/SpriteSheet.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/mat3x3.hpp>

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
    // Sol üst menüdeki modlar: Sprite Editor, Custom Outline (çizim ağı), Custom Physics Shape (Polygon Collider 2D),
    // Secondary Textures (_NormalMap, _MaskTex ...) ve Skinning Editor (kemik, ağ, ağırlık; SpriteEditorModes.cpp).
    class SpriteEditorPanel
    {
    public:
        enum class EditorMode { Sprite = 0, Outline, Physics, Secondary, Skin };
        enum class SkinTool { PreviewPose = 0, EditBone, CreateBone, SplitBone, AutoGeometry, EditGeometry, CreateVertex, AutoWeights, WeightSlider, WeightBrush };

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

        void SetMode(EditorMode mode);
        EditorMode GetMode() const { return m_Mode; }
        void SelectSprite(int index);
        int GetSelectedSprite() const { return m_Selected; }
        glm::vec2 CanvasPoint(const glm::vec2& texturePixel) const { return TexToScreen(texturePixel); }

        // Custom Outline / Custom Physics Shape: seçili dilim (seçim yoksa hepsi) için saydam olmayan bölgenin dış çizgisi
        void GenerateShapes(float tolerance);

        // Skinning Editor (seçili dilim)
        void SetSkinTool(SkinTool tool);
        int SkinAddBone(const glm::vec2& start, const glm::vec2& tip, int parent);
        void SkinAutoGeometry(int density, int alphaTolerance);
        void SkinAutoWeights();
        void SkinSetPose(int bone, float degrees);
        void SkinResetPose();
        // Secondary Textures
        void AddSecondaryTexture(const std::string& name, const std::string& path);

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
        void DrawSpriteList();
        void DrawCanvasInfo(bool hovered, const glm::vec2& mouseTex);

        // Modlar (SpriteEditorModes.cpp)
        void DrawModeCanvas(bool hovered);
        void DrawPolygonToolbar();
        void DrawPolygonMode(ImDrawList* dl, bool hovered);
        void DrawPolygonSidebar();
        void DrawSecondarySidebar();
        void DrawSkinMode(ImDrawList* dl, bool hovered);
        void DrawSkinSidebar();
        void HandleModeShortcuts();
        void EnsureSingleSprite();
        void ValidateModeSelection();
        int SpriteAt(const glm::vec2& texturePixel) const;
        std::string UniqueBoneName(const SpriteSkinData& skin) const;
        void EnsurePoseSize(const SpriteSkinData& skin);
        std::vector<glm::mat3> SkinPoseMatrices(const SpriteSkinData& skin) const;

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
        EditorMode m_Mode = EditorMode::Sprite;
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

        // Custom Outline / Physics Shape
        int m_PolyPath = -1, m_PolyVertex = -1;     // seçili köşe
        bool m_PolyDragging = false;
        bool m_PolyCreating = false;
        glm::vec2 m_PolyCreateStart{ 0.0f };
        bool m_Snap = true;
        float m_OutlineTolerance = 0.5f;
        std::vector<std::vector<glm::vec2>> m_PolyClipboard;
        bool m_HasPolyClipboard = false;

        // Secondary Textures: önizleme
        int m_SecondarySelected = -1;
        std::shared_ptr<Texture2D> m_SecondaryPreview;
        std::string m_SecondaryPreviewPath;

        // Skinning Editor
        enum class SkinDrag { None, Joint, Tip, Body, PoseRotate, PoseMove, Vertices, BoxSelect, Brush };
        SkinTool m_SkinTool = SkinTool::EditBone;
        SkinDrag m_SkinDrag = SkinDrag::None;
        int m_SelectedBone = -1;
        std::vector<int> m_SelectedVerts;
        glm::vec2 m_SkinDragLast{ 0.0f };
        glm::vec2 m_BoxStart{ 0.0f };
        bool m_BonePlacing = false;
        glm::vec2 m_BoneStart{ 0.0f };
        int m_BoneParent = -1;
        ImVec2 m_BonePressScreen{ 0, 0 };
        std::vector<float> m_PoseRotation;          // radyan; yalnız önizleme, kaydedilmez
        std::vector<glm::vec2> m_PoseOffset;
        int m_Density = 12;
        int m_AlphaTolerance = 1;
        bool m_GeometryWeights = true;
        float m_BrushSize = 24.0f;                  // doku pikseli (yarıçap)
        float m_BrushHardness = 0.5f;
        float m_BrushStep = 0.15f;
        int m_BrushMode = 0;                        // 0 Add, 1 Subtract, 2 Smooth
        int m_SliderMode = 0;                       // 0 Add & Subtract, 1 Smooth
        float m_SliderValue = 0.0f;
        bool m_SliderPushedUndo = false;

        std::vector<SpriteDef> m_SidebarSnapshot;
        bool m_SidebarEditing = false;

        bool m_ConfirmDiscard = false;
        std::filesystem::path m_PendingOpen;
    };
}
