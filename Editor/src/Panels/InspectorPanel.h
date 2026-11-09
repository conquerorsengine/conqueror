#pragma once

#include "Scene/Entity.h"

#include <functional>
#include <memory>
#include <string>

namespace Conqueror { struct ParticleSystemComponent; }

namespace Conqueror::Editor
{
    // Particle System ayarları arayüzü (bileşen ve .cqparticle varlığı)
    void DrawParticleSystemUI(ParticleSystemComponent& ps);

    class InspectorPanel
    {
    public:
        InspectorPanel() = default;
        ~InspectorPanel() = default;

        void OnImGuiRender();

        // Sprite Renderer -> "Open Sprite Editor" (EditorLayer bağlar)
        static std::function<void(const std::string&)> s_OpenSpriteEditor;
        // Input Actions penceresini bu varlıkla aç (Inspector > Edit Asset)
        static std::function<void(const std::string&)> s_OpenInputActions;
        // Kayıtlı bir bileşen türünün alanlarını sahneden bağımsız bir nesne üzerinde çiz (ör. Volume Profile varlığı); değişince true
        static bool DrawObjectFields(const std::string& componentType, void* object, const std::function<bool(const std::string&)>& include);
        static std::function<void(const std::string&)> s_OpenCodeEditor;
        // Geri alma komutlarının tuttuğu sahne (EditorLayer bağlar)
        static std::function<std::shared_ptr<class Scene>()> s_SharedScene;
        void SetSelectedEntity(Entity entity);
        Entity GetSelectedEntity() const { return m_SelectionContext; }
        void SetContext(class Scene* scene) 
        { 
            m_Context = scene;
            // Başka sahneye ait seçimi bırak (aynı sahne yeniden bağlanınca seçim korunur)
            if (m_SelectionContext.GetScene() != scene)
                m_SelectionContext = {};
        }

    private:
        std::string m_NativeCompileLog;      // Native Script "Compile Now" sonucu
        bool m_NativeCompileOk = false;
        void DrawComponents(Entity entity);
        void DrawConquerorScripts(Entity entity);
        
        template<typename T>
        void DrawAddComponentEntry(const std::string& entryName);

        // Geri alma: düzenleme başlarken seçili varlığın kopyası, bitince değiştiyse tek adım
        bool IsInspectorHovered() const;
        void BeginUndoCapture();
        void EndUndoCapture();

    private:
        Entity m_SelectionContext;
        class Scene* m_Context = nullptr;

        unsigned int m_WindowID = 0;
        bool m_UndoPending = false;
        uint64_t m_UndoEntityUUID = 0;
        std::shared_ptr<class Scene> m_UndoBefore;
        std::string m_UndoBeforeText;
    };
}
