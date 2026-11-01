#pragma once

#include "Scene/Entity.h"

// Yerel betik sınıfını motora tanıtır: Native Script bileşeni .cpp dosyasını kendisi derler ve bu fabrikayı çağırır.
//   class Spinner : public Conqueror::ScriptableEntity { void OnUpdate(float dt) override { ... } };
//   CQ_NATIVE_SCRIPT(Spinner)
#if defined(_WIN32)
    #define CQ_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#else
    #define CQ_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#endif
#define CQ_NATIVE_SCRIPT(ClassName) \
    CQ_SCRIPT_EXPORT ::Conqueror::ScriptableEntity* Create##ClassName() { return new ClassName(); }

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
