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

    class DestroyEntityCommand : public EditorCommand
    {
    public:
        DestroyEntityCommand(const std::shared_ptr<Scene>& scene, Entity entity);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return "Delete Entity: " + m_EntityName; }

    private:
        std::shared_ptr<Scene> m_Scene;
        Entity m_Entity;
        std::string m_EntityName;

        struct ComponentSnapshot
        {
            TransformComponent Transform;
            LayerComponent Layer;
        };

        ComponentSnapshot m_Snapshot;
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
        Entity m_Entity;
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
        Entity m_Entity;
        Entity m_NewParent;
        Entity m_OldParent;
        bool m_HadOldParent = false;
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
        Entity m_Source;
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
