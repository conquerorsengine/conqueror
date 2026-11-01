#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"
#include "Renderer/PostProcess.h"

#include <imgui.h>

#include <memory>
#include <string>

struct ImNodesContext;

namespace Conqueror { class Material; class Framebuffer; }

namespace Conqueror::Editor
{
    class ShaderEditorPanel
    {
    public:
        ShaderEditorPanel();
        ~ShaderEditorPanel();

        void SetContext(class Scene* scene);
        void SetSelectedEntity(Entity entity);
        // Bir .cqmat varlığını düzenle (Content Browser çift tık / Inspector "Shader Graph"); boş yol = seçili nesne
        void SetMaterialAsset(const std::string& path);
        void OnImGuiRender();

    private:
        void DrawSidePanel(Conqueror::Material* material);
        void DrawPreview(Conqueror::Material* material);
        void DrawProperties(Conqueror::Material* material);
        void DrawCode(Conqueror::Material* material);
        void DrawNodeSearch(Conqueror::Material* material);
        void Recompile(Conqueror::Material* material);
        uint64_t HashGraph(const Conqueror::Material* material) const;

        ::ImNodesContext* m_NodesContext = nullptr;
        // Önizleme
        std::shared_ptr<Conqueror::Framebuffer> m_PreviewFramebuffer;
        PostProcessStack m_PreviewPost;
        int m_PreviewMesh = 0;          // 0 küre, 1 küp, 2 düzlem, 3 silindir
        float m_PreviewYaw = 0.6f, m_PreviewPitch = 0.35f, m_PreviewDistance = 4.2f;
        const Conqueror::Material* m_LastMaterial = nullptr;
        bool m_PreviewRotate = true;
        float m_PreviewTime = 0.0f;
        // Derleme
        bool m_AutoCompile = true;
        uint64_t m_LastGraphHash = 0;
        float m_DirtyTimer = -1.0f;
        float m_LastCompileMs = 0.0f;
        std::string m_CompileStatus;
        // Kod görünümü
        int m_CodeStage = 1;
        int m_CodeView = 0;             // 0 graf kaynağı, 1 GLSL (SPIR-V'den), 2 SPIR-V, 3 HLSL, 4 MSL
        std::string m_CodeCache;
        uint64_t m_CodeCacheKey = 0;
        int m_SideTab = 0;
        float m_SideWidth = 360.0f;
        char m_SearchBuffer[64] = {};
        ImVec2 m_SearchSpawnPos{ 0, 0 };
        ImVec2 m_GraphOrigin{ 0, 0 };
        class Scene* m_Context = nullptr;
        Entity m_SelectedEntity;
        int m_SelectedMaterialIndex = 0;
        std::string m_MaterialAssetPath;
        std::shared_ptr<Conqueror::Material> m_AssetMaterial;
    };
}
