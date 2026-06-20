#include "PhysicsWorld2D.h"
#include "Core/Logging/Log.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/RHI/Texture.h"

#include <box2d/box2d.h>

namespace Conqueror
{
    PhysicsWorld2D::PhysicsWorld2D()
    {
        m_WorldId = b2_nullWorldId;
    }

    PhysicsWorld2D::~PhysicsWorld2D()
    {
        Shutdown();
    }

    void PhysicsWorld2D::Initialize()
    {
        if (m_Initialized)
            return;

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {m_Gravity.x, m_Gravity.y};
        m_WorldId = b2CreateWorld(&worldDef);

        m_Initialized = true;
        CQ_CORE_INFO("Physics2D initialized");
    }

    void PhysicsWorld2D::Shutdown()
    {
        if (!m_Initialized)
            return;

        if (B2_IS_NON_NULL(m_WorldId))
        {
            b2DestroyWorld(m_WorldId);
            m_WorldId = b2_nullWorldId;
        }

        m_Initialized = false;
        CQ_CORE_INFO("Physics2D shutdown");
    }

    void PhysicsWorld2D::Step(Timestep ts)
    {
        if (!m_Initialized || B2_IS_NULL(m_WorldId))
            return;

        const int32_t subStepCount = 4;
        b2World_Step(m_WorldId, ts.GetSeconds(), subStepCount);

        SyncTransformsFromPhysics();
    }

    void PhysicsWorld2D::SetGravity(const glm::vec2& gravity)
    {
        m_Gravity = gravity;
        if (m_Initialized && B2_IS_NON_NULL(m_WorldId))
            b2World_SetGravity(m_WorldId, {gravity.x, gravity.y});
    }

    glm::vec2 PhysicsWorld2D::GetGravity() const
    {
        return m_Gravity;
    }

    void PhysicsWorld2D::CreateBody(Entity entity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        auto& transform = entity.GetComponent<TransformComponent>();

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.position = {transform.Position.x, transform.Position.y};
        bodyDef.rotation = b2MakeRot(transform.Rotation.z);
        bodyDef.enableSleep = true;
        bodyDef.isAwake = true;

        switch (rb2d.Type)
        {
            case RigidBody2DComponent::BodyType::Static:
                bodyDef.type = b2_staticBody;
                break;
            case RigidBody2DComponent::BodyType::Kinematic:
                bodyDef.type = b2_kinematicBody;
                break;
            case RigidBody2DComponent::BodyType::Dynamic:
            default:
                bodyDef.type = b2_dynamicBody;
                break;
        }

        b2BodyId bodyId = b2CreateBody(m_WorldId, &bodyDef);
        rb2d.RuntimeBody = new b2BodyId(bodyId);

        // Sprite texture boyutundan collider boyutunu hesapla
        glm::vec2 spriteWorldSize = {0.0f, 0.0f};
        if (entity.HasComponent<SpriteRendererComponent>())
        {
            auto& sprite = entity.GetComponent<SpriteRendererComponent>();
            if (sprite.Texture)
            {
                spriteWorldSize.x = (float)sprite.Texture->GetWidth() / 200.0f;
                spriteWorldSize.y = (float)sprite.Texture->GetHeight() / 200.0f;
            }
        }
        else if (entity.HasComponent<ImageComponent>())
        {
            auto& image = entity.GetComponent<ImageComponent>();
            if (image.Texture)
            {
                spriteWorldSize.x = (float)image.Texture->GetWidth() / 200.0f;
                spriteWorldSize.y = (float)image.Texture->GetHeight() / 200.0f;
            }
        }

        // BoxCollider2D
        if (entity.HasComponent<BoxCollider2DComponent>())
        {
            auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

            float halfW, halfH;
            if (spriteWorldSize.x > 0.0f && spriteWorldSize.y > 0.0f)
            {
                halfW = spriteWorldSize.x * transform.Scale.x;
                halfH = spriteWorldSize.y * transform.Scale.y;
            }
            else
            {
                halfW = bc2d.Size.x * transform.Scale.x;
                halfH = bc2d.Size.y * transform.Scale.y;
            }

            b2Polygon box = b2MakeBox(halfW, halfH);
            
            b2ShapeDef shapeDef = b2DefaultShapeDef();
            shapeDef.density = bc2d.Density;
            shapeDef.filter.categoryBits = 0x0001;
            shapeDef.filter.maskBits = 0xFFFF;

            b2CreatePolygonShape(bodyId, &shapeDef, &box);
        }

        // CircleCollider2D
        if (entity.HasComponent<CircleCollider2DComponent>())
        {
            auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();

            float radius;
            if (spriteWorldSize.x > 0.0f && spriteWorldSize.y > 0.0f)
            {
                radius = glm::max(spriteWorldSize.x, spriteWorldSize.y) * glm::max(transform.Scale.x, transform.Scale.y);
            }
            else
            {
                radius = cc2d.Radius * glm::max(transform.Scale.x, transform.Scale.y);
            }

            b2Circle circle;
            circle.center = {cc2d.Offset.x, cc2d.Offset.y};
            circle.radius = radius;

            b2ShapeDef shapeDef = b2DefaultShapeDef();
            shapeDef.density = cc2d.Density;
            shapeDef.filter.categoryBits = 0x0001;
            shapeDef.filter.maskBits = 0xFFFF;

            b2CreateCircleShape(bodyId, &shapeDef, &circle);
        }
    }

    void PhysicsWorld2D::DestroyBody(Entity entity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            if (B2_IS_NON_NULL(*bodyId))
            {
                b2DestroyBody(*bodyId);
            }
            delete bodyId;
            rb2d.RuntimeBody = nullptr;
        }
    }

    void PhysicsWorld2D::ApplyForce(Entity entity, const glm::vec2& force, const glm::vec2& point)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Body_ApplyForce(*bodyId, {force.x, force.y}, {point.x, point.y}, true);
        }
    }

    void PhysicsWorld2D::ApplyForceToCenter(Entity entity, const glm::vec2& force)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Vec2 center = b2Body_GetWorldCenterOfMass(*bodyId);
            b2Body_ApplyForce(*bodyId, {force.x, force.y}, center, true);
        }
    }

    void PhysicsWorld2D::ApplyImpulse(Entity entity, const glm::vec2& impulse, const glm::vec2& point)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Body_ApplyLinearImpulse(*bodyId, {impulse.x, impulse.y}, {point.x, point.y}, true);
        }
    }

    void PhysicsWorld2D::ApplyImpulseToCenter(Entity entity, const glm::vec2& impulse)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Vec2 center = b2Body_GetWorldCenterOfMass(*bodyId);
            b2Body_ApplyLinearImpulse(*bodyId, {impulse.x, impulse.y}, center, true);
        }
    }

    void PhysicsWorld2D::ApplyTorque(Entity entity, float torque)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Body_ApplyTorque(*bodyId, torque, true);
        }
    }

    void PhysicsWorld2D::ApplyAngularImpulse(Entity entity, float impulse)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Body_ApplyAngularImpulse(*bodyId, impulse, true);
        }
    }

    void PhysicsWorld2D::SetLinearVelocity(Entity entity, const glm::vec2& velocity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Body_SetLinearVelocity(*bodyId, {velocity.x, velocity.y});
        }
    }

    glm::vec2 PhysicsWorld2D::GetLinearVelocity(Entity entity) const
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return glm::vec2(0.0f);

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Vec2 vel = b2Body_GetLinearVelocity(*bodyId);
            return glm::vec2(vel.x, vel.y);
        }
        return glm::vec2(0.0f);
    }

    void PhysicsWorld2D::SetAngularVelocity(Entity entity, float velocity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            b2Body_SetAngularVelocity(*bodyId, velocity);
        }
    }

    float PhysicsWorld2D::GetAngularVelocity(Entity entity) const
    {
        if (!m_Initialized || !entity.HasComponent<RigidBody2DComponent>())
            return 0.0f;

        auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
        if (rb2d.RuntimeBody)
        {
            b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
            return b2Body_GetAngularVelocity(*bodyId);
        }
        return 0.0f;
    }

    void PhysicsWorld2D::SyncTransformsFromPhysics()
    {
        // Physics'ten Transform'a senkronizasyon
        if (!m_Initialized || !m_Scene || B2_IS_NULL(m_WorldId))
            return;

        // Tüm RigidBody2DComponent'leri tara
        auto view = m_Scene->m_Registry.view<RigidBody2DComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto& rb2d = view.get<RigidBody2DComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            if (rb2d.RuntimeBody && rb2d.Type == RigidBody2DComponent::BodyType::Dynamic)
            {
                b2BodyId* bodyId = static_cast<b2BodyId*>(rb2d.RuntimeBody);
                
                if (B2_IS_NON_NULL(*bodyId))
                {
                    // Pozisyon al
                    b2Vec2 pos = b2Body_GetPosition(*bodyId);
                    transform.Position.x = pos.x;
                    transform.Position.y = pos.y;
                    
                    // Rotasyon al
                    b2Rot rot = b2Body_GetRotation(*bodyId);
                    transform.Rotation.z = b2Rot_GetAngle(rot);
                }
            }
        }
    }

    void PhysicsWorld2D::SyncTransformsToPhysics()
    {
        // Transform'dan Physics'e senkronizasyon
        // Scene tarafından çağrılacak
    }
}
