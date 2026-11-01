#pragma once

#include "EditorCommand.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"

#include <memory>
#include <string>

namespace Conqueror::Editor
{
    // Tek varlığın bileşen durumunu öncesi/sonrası kopyalarıyla geri alan komut (Inspector düzenlemeleri).
    // Kopyalar küçük özel sahnelerde tutulur; bileşen ekleme/kaldırma da geri alınır.
    class EntitySnapshotCommand : public EditorCommand
    {
    public:
        EntitySnapshotCommand(std::shared_ptr<Scene> scene, uint64_t entityUUID, std::shared_ptr<Scene> before, std::shared_ptr<Scene> after,
                              std::string label)
            : m_Scene(std::move(scene)), m_UUID(entityUUID), m_Before(std::move(before)), m_After(std::move(after)), m_Label(std::move(label))
        {
        }

        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return m_Label; }

        // Varlığın bileşenlerini bağımsız bir sahneye kopyala
        static std::shared_ptr<Scene> Capture(Entity entity);
        // Kopyayı canlı varlığa uygula (eksik bileşenleri kaldırır)
        static void Restore(const std::shared_ptr<Scene>& snapshot, Entity target);

    private:
        std::shared_ptr<Scene> m_Scene;
        uint64_t m_UUID = 0;
        std::shared_ptr<Scene> m_Before, m_After;
        std::string m_Label;
        bool m_SkipFirstExecute = true;
    };
}
