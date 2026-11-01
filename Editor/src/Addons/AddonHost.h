#pragma once

#include "Addons/AddonManager.h"
#include "Scene/Entity.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    namespace CQS { class CQSVM; }
}

namespace Conqueror::Editor
{
    // Etkin addon'ların editör betiklerini her biri kendi CQS VM'inde çalıştırır (oyun VM'i Play/Stop'ta
    // sıfırlandığından ayrıdır). Betikler OnLoad'da menü öğesi, ADDONS sekmesi düğmesi ve panel kaydeder;
    // menü/düğme çağrıları sahne anlık görüntüsüyle geri alınabilir. Gui* fonksiyonları panel çiziminde ImGui'ye gider.
    class AddonHost
    {
    public:
        struct MenuItem { std::string Path, Method; };
        struct ToolButton { std::string Label, Icon, Method, Tooltip; };
        struct Panel { std::string Title, Method; bool Open = false; bool Focus = false; };

        struct Instance
        {
            AddonManifest Manifest;
            std::unique_ptr<CQS::CQSVM> VM;
            CQS::Value Object;
            uint32_t PinHandle = 0;
            bool Loaded = false;
            std::string Error;
            std::vector<MenuItem> Menu;
            std::vector<ToolButton> Buttons;
            std::vector<Panel> Panels;
            bool HasUpdate = false;
            std::vector<std::string> Messages;   // EditorNotify geçmişi (son 32)
        };

        // EditorLayer tarafından bağlanan editör işlevleri
        struct Hooks
        {
            std::function<std::shared_ptr<Scene>()> GetScene;
            std::function<std::vector<Entity>()> GetSelection;
            std::function<void(const std::vector<Entity>&)> SetSelection;
            std::function<glm::vec3()> GetCameraTarget;
            std::function<glm::vec3()> GetCameraPosition;
            std::function<void(Entity)> Focus;
            std::function<bool()> IsPlaying;
            // Addon panellerinin ilk açılışta yerleşeceği dock düğümü (0 = serbest pencere)
            std::function<unsigned int()> ToolsDockId;
            // Sahneyi değiştirebilecek eylemi geri alınabilir sar
            std::function<void(const std::string& label, const std::function<void()>& action)> Undoable;
        };

        static AddonHost& Get();
        void SetHooks(Hooks hooks) { m_Hooks = std::move(hooks); }

        // AddonManager değişince / proje değişince etkin addon'ları (yeniden) yükle
        void Sync();
        void ReloadAll();
        void UnloadAll();

        void Update(float dt);
        void DrawMenuItems();     // ana menü "Addons" içeriği
        void DrawPanels();        // açık addon panelleri

        // Menü / düğme / betik çağrısı: geri alınabilir eylem olarak
        void Run(Instance& inst, const std::string& method, const std::string& label);
        const std::vector<std::unique_ptr<Instance>>& GetInstances() const { return m_Instances; }
        Instance* FindInstance(const std::string& id);
        // Otomasyon: addon metodunu (geri alınabilir) çağır / paneli aç
        bool Invoke(const std::string& id, const std::string& method);
        bool OpenPanel(const std::string& id, const std::string& title);

    private:
        bool Load(Instance& inst);
        void RegisterNatives(Instance& inst);
        bool Call(Instance& inst, const std::string& method, const CQS::Value* args = nullptr, int argc = 0);
        void Notify(Instance& inst, const std::string& message);

        Hooks m_Hooks;
        std::vector<std::unique_ptr<Instance>> m_Instances;
        uint32_t m_LoadedVersion = 0;
        std::string m_LoadedPackages;
        bool m_InPanel = false;   // Gui* yalnızca panel çiziminde ImGui'ye gider
        int m_DisabledDepth = 0;  // kapatılmamış GuiBeginDisabled'lar panel sonunda kapatılır
        int m_IndentDepth = 0;
    };
}
