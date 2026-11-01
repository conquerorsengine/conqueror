#pragma once

#include "EditorCommand.h"
#include "Scene/Scene.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // Sahnenin tamamını öncesi/sonrası metin anlık görüntüsüyle geri alan komut. Toplu değişiklikler için
    // (addon eylemleri, betikle düzenleme): eylem zaten uygulandığından ilk Execute bir şey yapmaz.
    class SceneSnapshotCommand : public EditorCommand
    {
    public:
        SceneSnapshotCommand(std::shared_ptr<Scene> scene, std::string before, std::string after, std::string label,
                             std::function<void()> onRestored)
            : m_Scene(std::move(scene)), m_Before(std::move(before)), m_After(std::move(after)), m_Label(std::move(label)),
              m_OnRestored(std::move(onRestored))
        {
        }

        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return m_Label; }

        // Sahneyi YAML metnine yaz / metinden geri yükle
        static std::string Capture(const std::shared_ptr<Scene>& scene);
        static void Restore(const std::shared_ptr<Scene>& scene, const std::string& snapshot);

    private:
        std::shared_ptr<Scene> m_Scene;
        std::string m_Before, m_After, m_Label;
        std::function<void()> m_OnRestored;
        bool m_SkipFirstExecute = true;
    };

    // Düzenleyicide komutsuz oluşturulan nesneler (menüler, araç çubuğu, sürükle-bırak, eklentiler): geri al = bu
    // nesneleri (alt ağaçlarıyla) sil, ileri al = oluşturma sonrası anlık görüntüyü geri yükle
    class CreateEntitiesCommand : public EditorCommand
    {
    public:
        CreateEntitiesCommand(std::shared_ptr<Scene> scene, std::vector<uint64_t> roots, std::vector<uint64_t> created, std::string after,
                              std::string label, std::function<void()> onRestored)
            : m_Scene(std::move(scene)), m_Roots(std::move(roots)), m_Created(std::move(created)), m_After(std::move(after)), m_Label(std::move(label)),
              m_OnRestored(std::move(onRestored))
        {
        }

        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return m_Label; }

    private:
        std::shared_ptr<Scene> m_Scene;
        std::vector<uint64_t> m_Roots, m_Created;
        std::string m_After, m_Label;
        std::function<void()> m_OnRestored;
        bool m_SkipFirstExecute = true;
    };
}
