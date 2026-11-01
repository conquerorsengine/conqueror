#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include <string>

namespace Conqueror::Editor
{
    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;
        virtual void Execute() = 0;
        virtual void Unexecute() = 0;
        virtual std::string GetDescription() const = 0;
    };

    class CreateEntityCommand : public EditorCommand
    {
    public:
        CreateEntityCommand(const std::shared_ptr<Scene>& scene, const std::string& name);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Create Entity: " + m_Name; }
        Entity GetCreatedEntity() const { return m_Entity; }

    private:
        std::shared_ptr<Scene> m_Scene;
        std::string m_Name;
        Entity m_Entity;
        bool m_Created = false;
    };

    // Silme: nesne, tüm bileşenleri ve çocuklarıyla AYNI kimlikle (UUID) yedeklenir; geri alınca aynı üst nesnenin altına
    // döner (eskiden yalnız ad, Transform ve Layer geri geliyordu: Mesh Renderer, betikler, çocuklar kayboluyordu)
    class DestroyEntityCommand : public EditorCommand
    {
    public:
        DestroyEntityCommand(const std::shared_ptr<Scene>& scene, Entity entity);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Delete Entity: " + m_EntityName; }

        // Alt ağacı başka bir sahneye aynı UUID'lerle kopyala
        static Entity CloneTree(Scene& destination, Entity source, Entity parent);

    private:
        std::shared_ptr<Scene> m_Scene;
        uint64_t m_RootUUID = 0, m_ParentUUID = 0;
        std::string m_EntityName;
        std::shared_ptr<Scene> m_Backup;
        uint64_t m_BackupRoot = 0;
        bool m_Destroyed = false;
    };

    class RenameEntityCommand : public EditorCommand
    {
    public:
        RenameEntityCommand(Entity entity, const std::string& newName);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Rename: " + m_OldName + " -> " + m_NewName; }

    private:
        Scene* m_Scene = nullptr;
        uint64_t m_UUID = 0;
        std::string m_OldName;
        std::string m_NewName;
    };

    class ReparentEntityCommand : public EditorCommand
    {
    public:
        ReparentEntityCommand(Entity entity, Entity newParent);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Reparent Entity"; }

    private:
        Scene* m_Scene = nullptr;
        uint64_t m_UUID = 0, m_NewParent = 0, m_OldParent = 0;   // 0 = kök
    };

    class DuplicateEntityCommand : public EditorCommand
    {
    public:
        DuplicateEntityCommand(const std::shared_ptr<Scene>& scene, Entity source);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Duplicate Entity"; }
        Entity GetDuplicatedEntity() const { return m_NewEntity; }

    private:
        std::shared_ptr<Scene> m_Scene;
        uint64_t m_SourceUUID = 0, m_NewUUID = 0;
        Entity m_NewEntity;
        bool m_Created = false;
    };

    class ModifyTransformCommand : public EditorCommand
    {
    public:
        ModifyTransformCommand(Entity entity, const TransformComponent& oldTransform, const TransformComponent& newTransform);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Modify Transform"; }

    private:
        Entity m_Entity;
        TransformComponent m_OldTransform;
        TransformComponent m_NewTransform;
    };
}
