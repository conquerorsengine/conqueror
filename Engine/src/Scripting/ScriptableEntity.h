#pragma once

#include "Scene/Entity.h"

namespace Conqueror
{
    // Kullanıcı script'lerinin türeyeceği base class
    class CQ_API ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        // Lifecycle fonksiyonları
        virtual void OnCreate() {}
        virtual void OnDestroy() {}
        virtual void OnUpdate(float deltaTime) {}

        // Component erişim helper'ları
        template<typename T>
        T& GetComponent()
        {
            return m_Entity.GetComponent<T>();
        }

        template<typename T>
        bool HasComponent()
        {
            return m_Entity.HasComponent<T>();
        }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            return m_Entity.AddComponent<T>(std::forward<Args>(args)...);
        }

        template<typename T>
        void RemoveComponent()
        {
            m_Entity.RemoveComponent<T>();
        }

        Entity GetEntity() { return m_Entity; }

    protected:
        Entity m_Entity;
        friend class Scene;
    };
}
