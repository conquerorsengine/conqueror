#include "Core/Audio/AudioRandomContainer.h"
#include "AISystem/BehaviorTree/BehaviorTreeAsset.h"
#include "Scene/AssetRuntime.h"
#include "Scene/Foliage.h"
#include "Scene/AnimationAssets.h"
#include "Renderer/RayTracing/RayTracedGI.h"
#include "Scene/FogVolume.h"
#include "AISystem/Navigation/Navigation2D.h"
#include "Renderer/Utilities/Renderer3D/ClusterRenderer.h"
#include "Renderer/LightProfile.h"
#include "Renderer/ExtraTextures.h"
#include "Core/Audio/SoundCue.h"
#include "Renderer/ShaderVariantCollection.h"
#include "Renderer/CustomRenderTexture.h"
#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Scene.h"
#include <random>
#include "Core/Localization/Localization.h"
#include "Core/Animation/PropertyAnimation.h"
#include "Scripting/NativeScriptCompiler.h"
#include "Core/Console/ConsoleVariables.h"
#include "Scene/GameplayTags.h"
#include "Scene/AbilitySystem.h"
#include "Scene/GameFramework.h"
#include "Scene/LevelStreaming.h"
#include "Scene/AutoLOD.h"
#include "Scene/AudioOcclusion.h"
#include "Scene/VertexPaint.h"
#include "Scene/Fracture.h"
#include "Scene/Ragdoll.h"
#include "Scene/VirtualGeometry.h"
#include "Scene/CharacterCustomizer.h"
#include "Scene/Signals.h"
#include "Scene/Tweens.h"
#include "Scene/CSG.h"
#include "Scene/MultiMesh.h"
#include "Scene/GodotNodes.h"
#include "Scene/CameraRenderTarget.h"
#include "Scene/Skeleton2D.h"
#include "Renderer/Utilities/Renderer3D/ClusterMesh.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Networking/Multiplayer/NetworkSession.h"
#include "Networking/Multiplayer/NetworkComponents.h"
#include "Timeline/TimelineSystem.h"
#include "Core/Math/Math.h"
#include "Renderer/Renderer.h"
#include "Components.h"
#include "Entity.h"
#include "Scripting/ScriptableEntity.h"
#include "Scripting/ScriptEngine.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ScriptRuntime.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Shadow/ShadowPass.h"
#include "Core/Project/Project.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Input/Input.h"
#include "Core/Input/GameInput.h"
#include "Scene/Splines.h"
#include "Scene/CameraRig.h"
#include "Scene/PostProcessVolume.h"
#include "Scene/FullScreenPass.h"
#include "Scene/Decals.h"
#include "Scene/Lighting2D.h"
#include "Scene/PixelPerfect.h"
#include "Scene/Billboards.h"
#include "Scene/CameraSkybox.h"
#include "Scene/AreaLight.h"
#include "Scene/LightProbeGroup.h"
#include "Scene/SpriteAnimation2D.h"
#include "Scene/SpriteShape.h"
#include "Scene/OcclusionCulling.h"
#include "Scene/Articulation.h"
#include "Scene/XRInteraction.h"
#include "XR/OpenXR.h"
#include "Particles/VisualEffect.h"
#include "Scene/WorldActors.h"
#include "Scene/ActorComponents.h"
#include "Scene/Water.h"
#include "Scene/PlanarReflection.h"
#include "Core/Audio/MetaSound.h"
#include "Core/Audio/InteractiveMusic.h"
#include "Scene/WorldPartition.h"
#include "Scene/BlendShapes.h"
#include "Scene/MetaHuman.h"
#include "Scene/TreeCreator.h"
#include "Renderer/TextureStreaming.h"
#include "Scene/PlayerInput.h"
#include "Scene/RigConstraints.h"
#include "AISystem/Navigation/NavAgents.h"
#include "Scene/VideoPlayer.h"
#include "Core/PhysicsSystem/Physics3D/ClothMesh.h"
#include "Core/Project/ProjectSettings.h"
#include "Scripting/ConquerorScript/CQSNativeBridge.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioMixer.h"
#include "Terrain/Terrain.h"
#include "UI/UISystem.h"
#include "Core/Audio/AudioSource.h"
#include "Core/Audio/AudioFilter.h"
#include "Core/Audio/AudioOneShots.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/GameplaySystems.h"
#include "Grid/GridSystem.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Debug/DebugMetrics.h"
#include "Core/Debug/DebugGridDraw.h"
#include "Core/Debug/DebugPalette.h"
#include "Core/Animation/Animator.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Animation/AnimationStateMachine.h"
#include "Core/Time/TimeManager.h"
#include "Core/Utils/PlayerPrefs.h"
#include "Renderer/EditorGrid.h"
#include "Renderer/IBL/EnvironmentMap.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace Conqueror
{
    // Ray Traced GI: ikincil kamera çizimi (Render Texture kamerası) oyun görünümünün prob hacmini izlemeden kullanır
    static bool s_GISecondaryView = false;


    namespace
    {
        // TAA hareket takibinde varlığın kararlı kimliği: UUID (Play'e geçişte sahne kopyalanınca da aynı kalır), yoksa tutamaç
        uint64_t MotionIdentity(const entt::registry& registry, entt::entity entity)
        {
            const auto* id = registry.try_get<IDComponent>(entity);
            const uint64_t value = (id && id->ID != 0) ? id->ID : (uint64_t)(uint32_t)entity + 1ull;
            return value != 0 ? value : 1ull;
        }

        // Varlığın ses çalışma zamanı nesneleri: önce kaynak (grafikten ayrılır), sonra filtre düğümleri.
        // Oyun sırasında silinen varlıkların sesleri (ör. döngüsel motor sesi) böylece susar ve sızmaz.
        void ReleaseAudioRuntime(entt::registry& registry, entt::entity entity)
        {
            // Ses motoru kapandıysa düğümler motorla birlikte gitti: yalnızca işaretçileri bırak
            const bool engineAlive = AudioEngine::IsInitialized();
            if (!engineAlive)
            {
                if (auto* audio = registry.try_get<AudioSourceComponent>(entity))
                    audio->RuntimeSound = nullptr;
            }
            if (auto* audio = registry.try_get<AudioSourceComponent>(entity); audio && audio->RuntimeSound)
            {
                auto* source = static_cast<AudioSource*>(audio->RuntimeSound);
                source->Stop();
                delete source;
                audio->RuntimeSound = nullptr;
                audio->IsPlaying = false;
                audio->NeedsReconnection = false;
            }
            auto release = [engineAlive](auto* component) {
                if (component && component->RuntimeFilter)
                {
                    if (engineAlive)
                        delete static_cast<AudioNode*>(component->RuntimeFilter);
                    component->RuntimeFilter = nullptr;
                }
            };
            release(registry.try_get<AudioLowPassFilterComponent>(entity));
            release(registry.try_get<AudioHighPassFilterComponent>(entity));
            release(registry.try_get<AudioEchoFilterComponent>(entity));
            release(registry.try_get<AudioReverbFilterComponent>(entity));
            release(registry.try_get<AudioChorusFilterComponent>(entity));
            release(registry.try_get<AudioDistortionFilterComponent>(entity));
            release(registry.try_get<AudioGainComponent>(entity));
            release(registry.try_get<AudioPanComponent>(entity));
        }
    }

    // Predefined tag listesi
    std::vector<std::string> Scene::s_AvailableTags = {
        "Untagged",
        "Player",
        "Enemy",
        "Respawn",
        "Finish",
        "EditorOnly",
        "MainCamera",
        "GameController"
    };

    Scene::Scene()
    {
        // Physics world'leri oluştur
        m_PhysicsWorld2D = std::make_unique<PhysicsWorld2D>();
        m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>();
        m_PhysicsWorld2D->m_Scene = this;
        m_PhysicsWorld3D->m_Scene = this;
        m_PhysicsWorld2D->Initialize();
        m_PhysicsWorld3D->Initialize();

        // Default layer'ları oluştur
        m_Layers = {
            { 0, "Default" },
            { 1, "TransparentFX" },
            { 2, "Ignore Raycast" },
            { 3, "User Layer" },
            { 4, "Water" },
            { 5, "UI" }
        };
        
        // Default flare elements (3 adet)
        m_FlareElements.resize(3);
        m_FlareElements[0] = { glm::vec3(1.0f), 0.08f, 0.3f, 1.0f };
        m_FlareElements[1] = { glm::vec3(1.0f), 0.06f, 0.6f, 0.8f };
        m_FlareElements[2] = { glm::vec3(1.0f), 0.05f, 0.9f, 0.6f };
    }

    Scene::~Scene()
    {
        BlendShapes::Forget(*this);
        Navigation2D::ForgetScene(this);
        Net::NetworkSession::Get().OnSceneStopped(this);
        TimelineSystem::ForgetScene(this);
        VideoPlayers::Release(this);
        if (m_IsRuntimeScene)
            DestroyScriptInstances(false);
        // Ses kaynakları ve filtre düğümleri (filtre bileşeni kaynaksız varlıkta da olabilir)
        for (auto entity : m_Registry.view<entt::entity>())
            ReleaseAudioRuntime(m_Registry, entity);
        
        m_PhysicsWorld2D->Shutdown();
        m_PhysicsWorld3D->Shutdown();
        GameplaySystems::ForgetScene(this);
        MultiMesh::ForgetScene(this);
    }

    void Scene::OnRuntimeStart(bool newPlaySession)
    {
        // Native Script .cpp kaynakları: değiştiyse derlenir, modül yüklenir, sınıf bağlanır (oyunda hazır kitaplık)
        {
            CQ_PROFILE_SAMPLE("RuntimeStart.NativeScripts", Scripts);
            NativeScriptCompiler::PrepareScene(*this);
        }
        // Her oyun oturumu temiz betik çalışma zamanlarıyla başlar (statik durum taşınmaz);
        // oyun içi sahne geçişlerinde (LoadScene) çalışma zamanları korunur
        if (newPlaySession)
        {
            CQ_PROFILE_SAMPLE("RuntimeStart.Session", Scripts);
            Scripting::ScriptRuntimes::PlayStart();
            AudioMixer::Get().BeginPlaySession();
            // Preloaded Shaders (Project Settings > Graphics): ilk kullanımda derleme takılması olmasın
            AudioRandomContainers::ResetAll();
            SoundCues::Reset();
            SoundMixes::Clear();
            Dialogue::Clear();
            AssetRuntime::Reset();
            MaterialParameterCollections::ResetOverrides();
            ShaderVariantCollections::ResetPreloaded();
            ShaderVariantCollections::WarmUpPreloaded();
            // XR Plug-in Management > Initialize XR on Startup: OpenXR gözlük oturumu (çalışma zamanı yoksa XR Device Simulator kalır)
            if (OpenXR::WantedAtStartup() && !OpenXR::GetStatus().SessionCreated)
                OpenXR::Start();
        }
        ScriptContext::SetActiveScene(this);
        ScriptContext::SetCurrentEntity(ScriptContext::NullEntity);
        m_IsRuntimeScene = true;

        // Unity Tile.gameObject: karo prefabları fizik ve betikler kurulmadan örneklenir (düzenleyici önizlemeleri kalkar)
        GridSystem::SyncTileObjects(*this, true);

        // 3D fizik: tüm gövde/eklem/karakterler kurulur; sabit adımda betiklerin FixedUpdate'i çağrılır
        if (m_PhysicsWorld3D)
        {
            CQ_PROFILE_SAMPLE("RuntimeStart.Physics3D", Physics);
            UpdateWorldTransforms();
            m_PhysicsWorld3D->OnFixedUpdate = [this](float dt) {
                // Sabit adım kuvvetleri (her fizik adımına uygulanır): yüzdürme, Physics Volume, radyal kuvvet; sonra betiklerin FixedUpdate'i
                WaterBodies::FixedUpdate(*this, dt);
                WorldActors::FixedUpdate(*this, dt);
                GodotNodes::FixedUpdate(*this, dt);   // Process Callback = Physics zamanlayıcıları
                InvokeScriptsFixedUpdate(dt);
            };
            m_PhysicsWorld3D->OnJointBreak = [this](uint32_t entity, float force) {
                CQS::Value arg = CQS::Value::MakeFloat((double)force);
                InvokeScriptMethod(entity, "OnJointBreak", &arg, 1);
            };
            Articulations::PrepareRuntime(*this);   // Articulation Body bağlantıları: gövde + eklem
            m_PhysicsWorld3D->OnRuntimeStart();
            WorldActors::BeginPlay(*this);
            WorldPartition::BeginPlay(*this);
        }
        if (m_PhysicsWorld2D)
        {
            m_PhysicsWorld2D->OnJointBreak = [this](uint32_t entity, float force) {
                CQS::Value arg = CQS::Value::MakeFloat((double)force);
                InvokeScriptMethod(entity, "OnJointBreak2D", &arg, 1);
            };
            m_PhysicsWorld2D->OnRuntimeStart();
        }

        // Gezinme ağı: NavMesh Surface "Bake On Play" (aynı sahne geometrisi için önbellekten)
        {
            CQ_PROFILE_SAMPLE("RuntimeStart.Navigation", AI);
            UpdateWorldTransforms();
            NavAgents::OnRuntimeStart(*this);
            Navigation2D::OnRuntimeStart(*this);   // 2D gezinme bölgeleri (Godot NavigationRegion2D)
        }

        // Game Mode: varsayılan oyuncuyu (Default Pawn) bir Player Start'ta oluştur
        GameFramework::BeginPlay(*this);

        // Çok oyunculu: NetworkManager "Auto Start" / komut satırı (ilk karenin sonunda başlar)
        Net::NetworkSession::Get().OnRuntimeStart(this);
    }

    void Scene::OnRuntimeStop(bool endPlaySession)
    {
        MetaSounds::StopAll(*this);
        InteractiveMusic::StopAll(*this);
        GodotNodes::ForgetScene(this);
        WorldPartition::Forget(*this);
        Navigation2D::ForgetScene(this);
        Fracture::ForgetScene(this);
        Signals::ForgetScene(this);
        Tweens::ForgetScene(this);
        Net::NetworkSession::Get().OnSceneStopped(this);
        TimelineSystem::ForgetScene(this);
        VideoPlayers::Release(this);
        if (m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->OnFixedUpdate = nullptr;
            m_PhysicsWorld3D->OnJointBreak = nullptr;
            m_PhysicsWorld3D->OnRuntimeStop();
        }
        if (m_PhysicsWorld2D)
        {
            m_PhysicsWorld2D->OnJointBreak = nullptr;
            m_PhysicsWorld2D->OnRuntimeStop();
        }
        DestroyScriptInstances(true);
        GameplaySystems::ResetRuntime(*this);
        PlayerPrefs::Save();   // OnDestroy dahil bu oturumdaki tercihler
        if (ScriptContext::GetActiveScene() == this)
            ScriptContext::SetActiveScene(nullptr);
        if (endPlaySession)
        {
            OpenXR::Stop();
            Scripting::ScriptRuntimes::PlayStop();
            AudioOneShots::StopAll();
            AudioMixer::Get().EndPlaySession();
        }
    }

    void Scene::DestroyScriptInstances(bool callOnDestroy)
    {
        // Örnekleri önce topla ve bileşenlerden ayır: OnDestroy varlık silebilir/oluşturabilir (görünüm yinelemesi bozulmasın)
        std::vector<std::tuple<Scripting::ScriptRuntime*, void*, uint32_t>> targets;
        auto view = m_Registry.view<ConquerorScriptComponent>();
        for (auto entity : view)
        {
            for (auto& script : view.get<ConquerorScriptComponent>(entity).Scripts)
            {
                if (script.Instance && script.Runtime)
                    targets.emplace_back(static_cast<Scripting::ScriptRuntime*>(script.Runtime), script.Instance, (uint32_t)entity);
                script.Instance = nullptr;
                script.Runtime = nullptr;
            }
        }
        if (callOnDestroy)
        {
            for (auto& [runtime, instance, entity] : targets)
            {
                if (Scripting::ScriptRuntimes::IsRetired(instance))
                    continue;
                ScriptContext::EntityScope scope(entity);
                runtime->Invoke(instance, "OnDestroy", nullptr, 0);
            }
        }
        for (auto& [runtime, instance, entity] : targets)
            Scripting::ScriptRuntimes::RetireInstance(runtime, instance);
        Scripting::ScriptRuntimes::FlushRetired();
    }

    void Scene::InvokeScriptMethod(uint32_t entity, const char* method, const CQS::Value* args, int argc)
    {
        entt::entity handle = (entt::entity)entity;
        if (!m_Registry.valid(handle) || !m_Registry.all_of<ConquerorScriptComponent>(handle))
            return;
        // Kopya: çağrılan betik bileşen listesini değiştirebilir
        std::vector<std::pair<Scripting::ScriptRuntime*, void*>> targets;
        for (auto& script : m_Registry.get<ConquerorScriptComponent>(handle).Scripts)
            if (script.Instance && script.Runtime)
                targets.emplace_back(static_cast<Scripting::ScriptRuntime*>(script.Runtime), script.Instance);
        for (auto& [runtime, instance] : targets)
            if (!Scripting::ScriptRuntimes::IsRetired(instance) && runtime->HasMethod(instance, method))
                runtime->Invoke(instance, method, args, argc);
    }

    void Scene::CollectScriptInstances(std::vector<std::pair<Scripting::ScriptRuntime*, void*>>& instances)
    {
        instances.clear();
        const bool ordered = !ProjectSettings::ScriptOrder().empty();
        std::vector<std::pair<int, std::pair<Scripting::ScriptRuntime*, void*>>> keyed;
        auto view = m_Registry.view<ConquerorScriptComponent>();
        for (auto entity : view)
        {
            if (!IsEntityActive(entity) || IsProcessDisabled(entity))
                continue;
            for (auto& script : view.get<ConquerorScriptComponent>(entity).Scripts)
                if (script.Instance && script.Runtime)
                {
                    auto item = std::make_pair(static_cast<Scripting::ScriptRuntime*>(script.Runtime), script.Instance);
                    if (ordered)
                        keyed.emplace_back(ProjectSettings::GetScriptOrder(script.ClassName), item);
                    else
                        instances.push_back(item);
                }
        }
        if (ordered)
        {
            std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& k : keyed)
                instances.push_back(k.second);
        }
    }

    void Scene::InvokeScriptsFixedUpdate(float dt)
    {
        CQS::Value arg = CQS::Value::MakeFloat((double)dt);
        std::vector<std::pair<Scripting::ScriptRuntime*, void*>> instances;
        CollectScriptInstances(instances);
        for (auto& [runtime, instance] : instances)
            if (!Scripting::ScriptRuntimes::IsRetired(instance) && runtime->HasMethod(instance, "FixedUpdate"))
                runtime->Invoke(instance, "FixedUpdate", &arg, 1);
    }

    void Scene::DispatchPhysicsEvents()
    {
        if (!m_PhysicsWorld3D)
            return;
        auto& pending = m_PhysicsWorld3D->GetPendingEvents();
        if (pending.empty())
            return;
        std::vector<Physics3D::ContactEvent> events;
        events.swap(pending);
        static const char* kMethods[2][3] = {
            { "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit" },
            { "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit" },
        };
        auto send = [&](const Physics3D::ContactEvent& ev, uint32_t self, uint32_t other, const glm::vec3& normal) {
            entt::entity handle = (entt::entity)self;
            if (self == Physics3D::NoEntity || !m_Registry.valid(handle) || !m_Registry.all_of<ConquerorScriptComponent>(handle))
                return;
            ScriptContext::ContactInfo info;
            info.Valid = true;
            info.Other = other;
            info.Point = ev.Point;
            info.Normal = normal;
            info.RelativeVelocity = ev.RelativeVelocity;
            info.Impulse = ev.Impulse;
            ScriptContext::SetContact(info);
            std::string otherName;
            if (other != Physics3D::NoEntity && m_Registry.valid((entt::entity)other) && m_Registry.all_of<TagComponent>((entt::entity)other))
                otherName = m_Registry.get<TagComponent>((entt::entity)other).Tag;
            CQS::Value args[2] = { CQS::Value::MakeInt((int64_t)other), CQS::Value::MakeString(otherName) };
            InvokeScriptMethod(self, kMethods[ev.Trigger ? 1 : 0][(int)ev.Type], args, 2);
        };
        for (const auto& ev : events)
        {
            // Normal A'dan B'ye: her taraf "diğerinden kendisine" yönü görür
            send(ev, ev.EntityA, ev.EntityB, -ev.Normal);
            send(ev, ev.EntityB, ev.EntityA, ev.Normal);
            // Godot sinyalleri: tetikleyici giriş / çıkış (body_entered / body_exited) ve çarpışma (collided)
            {
                const char* signal = nullptr;
                if (ev.Trigger)
                    signal = ev.Type == Physics3D::ContactEvent::Enter ? "body_entered" : ev.Type == Physics3D::ContactEvent::Exit ? "body_exited" : nullptr;
                else if (ev.Type == Physics3D::ContactEvent::Enter)
                    signal = "collided";
                if (signal)
                    for (int side = 0; side < 2; ++side)
                    {
                        const uint32_t self = side == 0 ? ev.EntityA : ev.EntityB, other = side == 0 ? ev.EntityB : ev.EntityA;
                        if (self == Physics3D::NoEntity || !m_Registry.valid((entt::entity)self))
                            continue;
                        CQS::Value sargs[2] = { CQS::Value::MakeInt(other == Physics3D::NoEntity ? -1 : (int64_t)other), CQS::Value::MakeFloat(ev.Impulse) };
                        Signals::Emit(*this, self, signal, sargs, ev.Trigger ? 1 : 2);
                    }
            }
            // Çarpışmada kamera sarsıntısı (Impulse Source → Generate On Collision)
            if (ev.Type == Physics3D::ContactEvent::Enter && !ev.Trigger)
            {
                CameraRig::OnCollision(*this, ev.EntityA, ev.Impulse);
                CameraRig::OnCollision(*this, ev.EntityB, ev.Impulse);
                // Fracture: güçlü çarpışma kırılmayı sıraya alır (Gameplay güncellemesinde uygulanır)
                Fracture::OnContact(*this, ev);
            }
        }
        ScriptContext::ClearContact();
    }

    void Scene::UpdateMouseEvents()
    {
        constexpr uint32_t kNone = 0xFFFFFFFFu;
        auto alive = [&](uint32_t e) { return e != kNone && m_Registry.valid((entt::entity)e); };
        if (!alive(m_MouseHoverEntity)) m_MouseHoverEntity = kNone;
        if (!alive(m_MouseDownEntity)) m_MouseDownEntity = kNone;

        // İmlecin altındaki çarpıştırıcı: 3D ışın, yoksa 2D nokta sorgusu (z = 0 düzlemi)
        uint32_t hovered = kNone;
        glm::vec2 mouse = GameInput::GetMousePosition();
        glm::vec3 origin, dir;
        if (CQS::ScreenPointToWorldRay(this, mouse.x, mouse.y, origin, dir))
        {
            if (m_PhysicsWorld3D && m_PhysicsWorld3D->IsRunning())
            {
                Physics3D::RaycastHit hit;
                if (m_PhysicsWorld3D->Raycast(origin, dir, 100000.0f, hit))
                    hovered = hit.Entity;
            }
            if (hovered == kNone && m_PhysicsWorld2D && std::fabs(dir.z) > 1e-6f)
            {
                float t = -origin.z / dir.z;
                if (t >= 0.0f)
                {
                    glm::vec3 p = origin + dir * t;
                    std::vector<uint32_t> found;
                    m_PhysicsWorld2D->OverlapCircle(glm::vec2(p), 0.001f, found);
                    if (!found.empty())
                        hovered = found.front();
                }
            }
        }
        if (!alive(hovered))
            hovered = kNone;

        // Yalnızca betiği olan varlıklara ilet (yöntem yoksa çağrı zaten yok sayılır)
        auto send = [&](uint32_t e, const char* method) {
            if (alive(e) && m_Registry.all_of<ConquerorScriptComponent>((entt::entity)e))
                InvokeScriptMethod(e, method, nullptr, 0);
        };
        if (hovered != m_MouseHoverEntity)
        {
            send(m_MouseHoverEntity, "OnMouseExit");
            send(hovered, "OnMouseEnter");
            m_MouseHoverEntity = hovered;
        }
        send(m_MouseHoverEntity, "OnMouseOver");
        if (GameInput::IsMouseDown(Mouse::ButtonLeft) && m_MouseHoverEntity != kNone)
        {
            m_MouseDownEntity = m_MouseHoverEntity;
            send(m_MouseDownEntity, "OnMouseDown");
        }
        else if (m_MouseDownEntity != kNone && GameInput::IsMouseHeld(Mouse::ButtonLeft))
            send(m_MouseDownEntity, "OnMouseDrag");
        if (m_MouseDownEntity != kNone && !GameInput::IsMouseHeld(Mouse::ButtonLeft))
        {
            uint32_t down = m_MouseDownEntity;
            m_MouseDownEntity = kNone;
            send(down, "OnMouseUp");
            if (down == m_MouseHoverEntity)
                send(down, "OnMouseUpAsButton");
        }
    }

    void Scene::DispatchPhysicsEvents2D()
    {
        if (!m_PhysicsWorld2D)
            return;
        auto& pending = m_PhysicsWorld2D->GetPendingEvents();
        if (pending.empty())
            return;
        std::vector<ContactEvent2D> events;
        events.swap(pending);
        static const char* kMethods[2][3] = {
            { "OnCollisionEnter2D", "OnCollisionStay2D", "OnCollisionExit2D" },
            { "OnTriggerEnter2D", "OnTriggerStay2D", "OnTriggerExit2D" },
        };
        auto send = [&](const ContactEvent2D& ev, uint32_t self, uint32_t other, const glm::vec2& normal) {
            entt::entity handle = (entt::entity)self;
            if (!m_Registry.valid(handle) || !m_Registry.all_of<ConquerorScriptComponent>(handle))
                return;
            ScriptContext::ContactInfo info;
            info.Valid = true;
            info.Other = other;
            info.Point = glm::vec3(ev.Point, 0.0f);
            info.Normal = glm::vec3(normal, 0.0f);
            info.Impulse = ev.Impulse;
            ScriptContext::SetContact(info);
            std::string otherName;
            if (m_Registry.valid((entt::entity)other) && m_Registry.all_of<TagComponent>((entt::entity)other))
                otherName = m_Registry.get<TagComponent>((entt::entity)other).Tag;
            CQS::Value args[2] = { CQS::Value::MakeInt((int64_t)other), CQS::Value::MakeString(otherName) };
            InvokeScriptMethod(self, kMethods[ev.Trigger ? 1 : 0][(int)ev.Type], args, 2);
        };
        for (const auto& ev : events)
        {
            send(ev, ev.EntityA, ev.EntityB, -ev.Normal);
            send(ev, ev.EntityB, ev.EntityA, ev.Normal);
        }
        ScriptContext::ClearContact();
    }

    void Scene::DispatchUIEventsToScripts(const std::vector<std::pair<Scripting::ScriptRuntime*, void*>>& instances)
    {
        // UI olayları (Button.OnClick, Toggle/Slider.OnValueChanged ...) aynı adlı betik metotlarına iletilir
        const auto& events = UISystem::GetEvents();
        if (events.empty())
            return;
        // Godot sinyalleri: UI kaynağı pressed / value_changed / submitted yayar
        for (const UIEvent& ev : events)
        {
            if (!ev.Source || ev.Source.GetScene() != this)
                continue;
            const char* signal = ev.Type == "Click" ? "pressed" : ev.Type == "ValueChanged" ? "value_changed" : ev.Type == "Submit" ? "submitted" : nullptr;
            if (!signal)
                continue;
            CQS::Value sargs[1] = { CQS::Value::MakeFloat((double)ev.Value) };
            Signals::Emit(*this, (uint32_t)(entt::entity)ev.Source, signal, sargs, ev.Type == "Click" ? 0 : 1);
        }
        if (instances.empty())
            return;
        for (const UIEvent& ev : events)
        {
            if (ev.Name.empty())
                continue;
            CQS::Value args[2] = { CQS::Value::MakeFloat((double)ev.Value), CQS::Value::MakeString(ev.Type) };
            for (auto& [runtime, inst] : instances)
                if (!Scripting::ScriptRuntimes::IsRetired(inst) && runtime->HasMethod(inst, ev.Name))
                    runtime->Invoke(inst, ev.Name, args, 2);
        }
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        return CreateEntityWithUUID(0, name); // UUID will be generated
    }

    Entity Scene::CreateEntityWithUUID(uint64_t uuid, const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };
        
        // UUID 0 ise generate et (timestamp + random)
        if (uuid == 0)
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            uuid = (uint64_t)timestamp ^ ((uint64_t)rand() << 32) ^ (uint64_t)rand();
        }
        
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<LayerComponent>(); // Default layer component
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        entt::entity entityHandle = entity;
        // Zaten silinmiş ya da şu an siliniyor (ör. OnDestroy içinden kendini/üstünü tekrar silme)
        if (!m_Registry.valid(entityHandle) || !m_DestroyingEntities.insert((uint32_t)entityHandle).second)
            return;

        // Betikler: önce OnDestroy (Unity gibi üstten alta), örnekler kare sonunda yok edilir
        RetireEntityScripts(entityHandle);
        m_Persistent.erase(entityHandle);

        // Child'ları da sil
        auto children = GetEntityChildren(entity);
        for (auto child : children)
        {
            DestroyEntity(child);
        }

        // Parent ilişkisini kaldır
        RemoveEntityParent(entity);
        
        // Child listesini temizle
        m_EntityChildren.erase(entityHandle);

        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->OnEntityDestroyed(entity);
        if (m_PhysicsWorld2D)
            m_PhysicsWorld2D->OnEntityDestroyed(entity);
        if (m_Registry.valid(entityHandle))
            ReleaseAudioRuntime(m_Registry, entityHandle);

        // Registry'den sil (OnDestroy bileşenleri değiştirmiş olabilir: geçerliliği yeniden denetle)
        if (m_Registry.valid(entityHandle))
            m_Registry.destroy(entityHandle);
        m_DestroyingEntities.erase((uint32_t)entityHandle);
    }

    void Scene::RetireEntityScripts(entt::entity entity)
    {
        auto* sc = m_Registry.try_get<ConquerorScriptComponent>(entity);
        if (!sc)
            return;
        // Önce örnekleri bileşenden ayır: OnDestroy bileşen depolarını değiştirebilir (sc geçersizleşebilir)
        std::vector<std::pair<Scripting::ScriptRuntime*, void*>> targets;
        for (auto& script : sc->Scripts)
        {
            if (script.Instance && script.Runtime)
                targets.emplace_back(static_cast<Scripting::ScriptRuntime*>(script.Runtime), script.Instance);
            script.Instance = nullptr;
            script.Runtime = nullptr;
        }
        if (targets.empty())
            return;
        // OnDestroy bu sahnenin bağlamında çalışır (ör. sahne geçişinde eski sahne silinirken); sonra geri al
        Scene* previousScene = ScriptContext::GetActiveScene();
        ScriptContext::SetActiveScene(this);
        {
            ScriptContext::EntityScope scope((uint32_t)entity);
            for (auto& [runtime, instance] : targets)
            {
                if (Scripting::ScriptRuntimes::IsRetired(instance))
                    continue;
                if (runtime->HasMethod(instance, "OnDestroy"))
                    runtime->Invoke(instance, "OnDestroy", nullptr, 0);
                Scripting::ScriptRuntimes::RetireInstance(runtime, instance);
            }
        }
        ScriptContext::SetActiveScene(previousScene);
    }

    void Scene::CopyEntityComponents(Entity src, Entity dst, bool shareRuntimeResources)
    {
        if (!src || !dst)
            return;
        auto& reg = dst.GetScene()->m_Registry;
        const entt::entity h = dst;
#define CQ_COPY(T) if (src.HasComponent<T>()) reg.emplace_or_replace<T>(h, src.GetComponent<T>());

        reg.get<TransformComponent>(h) = src.GetComponent<TransformComponent>();
        CQ_COPY(LayerComponent)
        {
            auto& srcTag = src.GetComponent<TagComponent>();
            auto& dstTag = reg.get<TagComponent>(h);
            dstTag.GameTag = srcTag.GameTag;
            dstTag.IsStatic = srcTag.IsStatic;
        }
        CQ_COPY(ActiveComponent)

        // Görsel
        CQ_COPY(SpriteRendererComponent)
        CQ_COPY(MeshRendererComponent)
        CQ_COPY(CameraComponent)
        CQ_COPY(TextRendererComponent)
        CQ_COPY(ButtonComponent)
        CQ_COPY(CanvasComponent)
        CQ_COPY(CanvasScalerComponent)
        CQ_COPY(GraphicRaycasterComponent)
        CQ_COPY(AnimatorComponent)
        CQ_COPY(AnimationComponent)
        CQ_COPY(DirectionalLightComponent)
        CQ_COPY(PointLightComponent)
        CQ_COPY(SpotLightComponent)
        CQ_COPY(ReflectionProbeComponent)
        CQ_COPY(AdaptiveProbeVolumeComponent)
        CQ_COPY(ParticleSystemComponent)  // kendi kopya kurucusu: simülasyon paylaşılmaz
        CQ_COPY(RectTransformComponent) CQ_COPY(UIImageComponent) CQ_COPY(UITextComponent) CQ_COPY(UIButtonComponent)
        CQ_COPY(UIToggleComponent) CQ_COPY(UISliderComponent) CQ_COPY(UIInputFieldComponent) CQ_COPY(UIDropdownComponent)
        CQ_COPY(UIScrollRectComponent) CQ_COPY(UILayoutGroupComponent)
        if (src.HasComponent<TerrainComponent>())
        {
            auto& t = reg.emplace_or_replace<TerrainComponent>(h, src.GetComponent<TerrainComponent>());
            t.RuntimeBody = nullptr;
            if (!shareRuntimeResources)
            {
                // Bağımsız kopya: GPU ağı/splat paylaşılırsa birini düzenlemek diğerini de bozar
                t.RuntimeMesh.reset();
                t.RuntimeSplat.reset();
                t.RuntimeMaterial.reset();
                t.MeshDirty = true;
                t.SplatDirty = true;
            }
        }

        // Fizik (çalışma zamanı tutamaçları kopyalanmaz)
        if (src.HasComponent<RigidBody2DComponent>())
            reg.emplace_or_replace<RigidBody2DComponent>(h, src.GetComponent<RigidBody2DComponent>()).RuntimeBody = nullptr;
        if (src.HasComponent<BoxCollider2DComponent>())
            reg.emplace_or_replace<BoxCollider2DComponent>(h, src.GetComponent<BoxCollider2DComponent>()).RuntimeFixture = nullptr;
        if (src.HasComponent<CircleCollider2DComponent>())
            reg.emplace_or_replace<CircleCollider2DComponent>(h, src.GetComponent<CircleCollider2DComponent>()).RuntimeFixture = nullptr;
        if (src.HasComponent<RigidbodyComponent>())
            reg.emplace_or_replace<RigidbodyComponent>(h, src.GetComponent<RigidbodyComponent>()).RuntimeBody = nullptr;
        CQ_COPY(BoxColliderComponent)
        CQ_COPY(SphereColliderComponent)
        CQ_COPY(CapsuleColliderComponent)
        if (src.HasComponent<MeshColliderComponent>())
            reg.emplace_or_replace<MeshColliderComponent>(h, src.GetComponent<MeshColliderComponent>()).RuntimeShape = nullptr;

        // Ses
        if (src.HasComponent<AudioSourceComponent>())
        {
            auto& a = reg.emplace_or_replace<AudioSourceComponent>(h, src.GetComponent<AudioSourceComponent>());
            a.RuntimeSound = nullptr;
            a.IsPlaying = false;
            a.RoutingGeneration = 0;
            a.GraphInitialized = false;
            a.NeedsReconnection = false;
        }
        CQ_COPY(AudioListenerComponent)
        CQ_COPY(AudioChorusFilterComponent)
        CQ_COPY(AudioDistortionFilterComponent)
        CQ_COPY(AudioEchoFilterComponent)
        CQ_COPY(AudioHighPassFilterComponent)
        CQ_COPY(AudioLowPassFilterComponent)
        CQ_COPY(AudioReverbFilterComponent)
        CQ_COPY(AudioReverbZoneComponent)
        CQ_COPY(AudioGainComponent)
        CQ_COPY(AudioPanComponent)

        // Yapay zekâ
        CQ_COPY(NavMeshAgentComponent)
        CQ_COPY(NavMeshObstacleComponent)
        CQ_COPY(NavMeshSurfaceComponent)
#undef CQ_COPY

        // Betikler: yalnızca tanımlar (örnekler oyun döngüsünde oluşturulur)
        if (src.HasComponent<ConquerorScriptComponent>())
        {
            auto& dstScripts = reg.emplace_or_replace<ConquerorScriptComponent>(h);
            for (auto& script : src.GetComponent<ConquerorScriptComponent>().Scripts)
            {
                ConquerorScriptData data;
                data.ScriptPath = script.ScriptPath;
                data.ClassName = script.ClassName;
                data.FieldOverrides = script.FieldOverrides;
                dstScripts.Scripts.push_back(data);
            }
        }
        if (src.HasComponent<NativeScriptComponent>())
        {
            auto& dstScripts = reg.emplace_or_replace<NativeScriptComponent>(h);
            for (auto& script : src.GetComponent<NativeScriptComponent>().Scripts)
            {
                NativeScriptData data;
                data.ModuleName = script.ModuleName;
                data.ClassName = script.ClassName;
                data.ScriptPath = script.ScriptPath;
                // .cpp kaynağı: modülü Play başında NativeScriptCompiler derleyip yükler (kaynak DLL gibi açılmaz)
                if (!data.ScriptPath.empty() && !NativeScriptCompiler::IsSource(data.ScriptPath) && !ScriptEngine::IsModuleLoaded(data.ModuleName))
                    ScriptEngine::LoadModule(data.ModuleName, data.ScriptPath);
                if (!data.ModuleName.empty() && !data.ClassName.empty() && ScriptEngine::IsModuleLoaded(data.ModuleName))
                {
                    data.InstantiateScript = [moduleName = data.ModuleName, className = data.ClassName]() {
                        return ScriptEngine::CreateScriptInstance(moduleName, className);
                    };
                }
                dstScripts.Scripts.push_back(data);
            }
        }

        // Kayıtlı (yansımalı) bileşenler: eklemler, Character Controller, ızgara ...
        ComponentRegistry::CopyAll(src, dst);
    }

    Entity Scene::DuplicateEntityTree(Entity source, const std::string& newName, Entity parent, std::unordered_map<uint64_t, uint64_t>* uuidMap)
    {
        if (!source)
            return {};
        const std::string& srcName = source.GetComponent<TagComponent>().Tag;
        Entity dest = CreateEntity(newName.empty() ? srcName : newName);
        CopyEntityComponents(source, dest, false);
        if (uuidMap)
            (*uuidMap)[(uint64_t)source.GetComponent<IDComponent>().ID] = (uint64_t)dest.GetComponent<IDComponent>().ID;
        if (parent)
            SetEntityParent(dest, parent, false);
        for (Entity child : source.GetChildren())
            DuplicateEntityTree(child, "", dest, uuidMap);
        return dest;
    }

    void Scene::RemapEntityReferences(Entity root, const std::unordered_map<uint64_t, uint64_t>& uuidMap)
    {
        if (!root || uuidMap.empty())
            return;
        for (auto& info : ComponentRegistry::All())
        {
            if (!info.Has(root))
                continue;
            void* c = info.Get(root);
            for (auto& f : info.Fields)
            {
                if (f.Kind != FieldKind::EntityRef || !f.Ptr)
                    continue;
                auto* ref = (uint64_t*)f.Ptr(c);
                auto it = uuidMap.find(*ref);
                if (it != uuidMap.end())
                    *ref = it->second;
            }
        }
        for (Entity child : root.GetChildren())
            RemapEntityReferences(child, uuidMap);
    }

    void Scene::OnEntityTreeSpawned(Entity root)
    {
        if (!root)
            return;
        // Oyun sırasında oluşturulan gövdeler bir sonraki fizik adımında kurulur
        if (m_PhysicsWorld3D) m_PhysicsWorld3D->MarkDirty(root);
        if (m_PhysicsWorld2D) m_PhysicsWorld2D->MarkDirty(root);
        for (Entity child : root.GetChildren())
            OnEntityTreeSpawned(child);
    }

    void Scene::RemoveComponentsNotIn(Entity reference, Entity target)
    {
        if (!reference || !target)
            return;
#define CQ_REMOVE_IF_MISSING(T) if (target.HasComponent<T>() && !reference.HasComponent<T>()) target.RemoveComponent<T>();
        CQ_REMOVE_IF_MISSING(ActiveComponent)
        CQ_REMOVE_IF_MISSING(SpriteRendererComponent) CQ_REMOVE_IF_MISSING(MeshRendererComponent) CQ_REMOVE_IF_MISSING(CameraComponent)
        CQ_REMOVE_IF_MISSING(TextRendererComponent) CQ_REMOVE_IF_MISSING(ButtonComponent)
        CQ_REMOVE_IF_MISSING(CanvasComponent) CQ_REMOVE_IF_MISSING(CanvasScalerComponent) CQ_REMOVE_IF_MISSING(GraphicRaycasterComponent)
        CQ_REMOVE_IF_MISSING(AnimatorComponent) CQ_REMOVE_IF_MISSING(AnimationComponent)
        CQ_REMOVE_IF_MISSING(DirectionalLightComponent) CQ_REMOVE_IF_MISSING(PointLightComponent) CQ_REMOVE_IF_MISSING(SpotLightComponent)
        CQ_REMOVE_IF_MISSING(ReflectionProbeComponent) CQ_REMOVE_IF_MISSING(AdaptiveProbeVolumeComponent) CQ_REMOVE_IF_MISSING(ParticleSystemComponent)
        CQ_REMOVE_IF_MISSING(RectTransformComponent) CQ_REMOVE_IF_MISSING(UIImageComponent) CQ_REMOVE_IF_MISSING(UITextComponent)
        CQ_REMOVE_IF_MISSING(UIButtonComponent) CQ_REMOVE_IF_MISSING(UIToggleComponent) CQ_REMOVE_IF_MISSING(UISliderComponent)
        CQ_REMOVE_IF_MISSING(UIInputFieldComponent) CQ_REMOVE_IF_MISSING(UIDropdownComponent) CQ_REMOVE_IF_MISSING(UIScrollRectComponent)
        CQ_REMOVE_IF_MISSING(UILayoutGroupComponent) CQ_REMOVE_IF_MISSING(TerrainComponent)
        CQ_REMOVE_IF_MISSING(RigidBody2DComponent) CQ_REMOVE_IF_MISSING(BoxCollider2DComponent) CQ_REMOVE_IF_MISSING(CircleCollider2DComponent)
        CQ_REMOVE_IF_MISSING(RigidbodyComponent) CQ_REMOVE_IF_MISSING(BoxColliderComponent) CQ_REMOVE_IF_MISSING(SphereColliderComponent)
        CQ_REMOVE_IF_MISSING(CapsuleColliderComponent) CQ_REMOVE_IF_MISSING(MeshColliderComponent)
        CQ_REMOVE_IF_MISSING(AudioSourceComponent) CQ_REMOVE_IF_MISSING(AudioListenerComponent) CQ_REMOVE_IF_MISSING(AudioChorusFilterComponent)
        CQ_REMOVE_IF_MISSING(AudioDistortionFilterComponent) CQ_REMOVE_IF_MISSING(AudioEchoFilterComponent) CQ_REMOVE_IF_MISSING(AudioHighPassFilterComponent)
        CQ_REMOVE_IF_MISSING(AudioLowPassFilterComponent) CQ_REMOVE_IF_MISSING(AudioReverbFilterComponent) CQ_REMOVE_IF_MISSING(AudioReverbZoneComponent)
        CQ_REMOVE_IF_MISSING(AudioGainComponent) CQ_REMOVE_IF_MISSING(AudioPanComponent)
        CQ_REMOVE_IF_MISSING(NavMeshAgentComponent) CQ_REMOVE_IF_MISSING(NavMeshObstacleComponent) CQ_REMOVE_IF_MISSING(NavMeshSurfaceComponent)
        CQ_REMOVE_IF_MISSING(ConquerorScriptComponent) CQ_REMOVE_IF_MISSING(NativeScriptComponent)
#undef CQ_REMOVE_IF_MISSING
        for (auto& info : ComponentRegistry::All())
            if (!info.Builtin && info.Has(target) && !info.Has(reference))
                info.Remove(target);
    }

    void Scene::ClearEntities()
    {
        std::vector<entt::entity> roots;
        m_Registry.view<IDComponent>().each([&](auto e, auto&) {
            if (!Entity{ e, this }.GetParent())
                roots.push_back(e);
        });
        for (auto e : roots)
            if (m_Registry.valid(e))
                DestroyEntity(Entity{ e, this });
        m_Registry.clear();
        m_EntityChildren.clear();
        m_EntityParent.clear();
    }

    Entity Scene::DuplicateEntity(Entity source, const std::string& newName)
    {
        if (!source)
            return {};
        // Unity gibi: tüm alt ağaç kopyalanır, kopya aynı ebeveynin altında kardeş olur
        const std::string destName = newName.empty() ? source.GetComponent<TagComponent>().Tag + " (Copy)" : newName;
        Entity parent = source.GetScene() == this ? source.GetParent() : Entity{};
        std::unordered_map<uint64_t, uint64_t> uuidMap;
        Entity dest = DuplicateEntityTree(source, destName, parent, &uuidMap);
        RemapEntityReferences(dest, uuidMap);
        UpdateWorldTransforms();
        OnEntityTreeSpawned(dest);
        return dest;
    }

    void Scene::CloneSceneFrom(const std::shared_ptr<Scene>& source)
    {
        if (!source) return;
        m_IsRuntimeScene = true;
        m_UIScreenSize = source->m_UIScreenSize;

        std::unordered_map<uint64_t, entt::entity> uuidMap;
        std::vector<entt::entity> physicsEntities;

        // Kaynağın etiket deposundaki sırayı aynen koru: Hiyerarşi sırası ve betik çalışma sırası
        // oyun modunda düzenleme moduyla (ve her platformda) aynı kalır
        std::vector<entt::entity> order;
        {
            const auto& tags = source->m_Registry.storage<TagComponent>();
            order.reserve(tags.size());
            for (size_t i = 0; i < tags.size(); ++i)
            {
                entt::entity e = tags.data()[i];
                if (source->m_Registry.valid(e) && source->m_Registry.all_of<IDComponent>(e))
                    order.push_back(e);
            }
        }

        for (entt::entity entityID : order)
        {
            Entity srcEntity{ entityID, source.get() };
            const uint64_t uuid = srcEntity.GetComponent<IDComponent>().ID;
            Entity newEntity = CreateEntityWithUUID(uuid, srcEntity.GetComponent<TagComponent>().Tag);
            uuidMap[uuid] = newEntity;
        }

        for (entt::entity entityID : order)
        {
            Entity srcEntity{ entityID, source.get() };
            entt::entity dstHandle = uuidMap[srcEntity.GetComponent<IDComponent>().ID];
            // Oyun kopyası GPU kaynaklarını (terrain ağı vb.) paylaşabilir: hızlı Play
            CopyEntityComponents(srcEntity, Entity{ dstHandle, this }, true);
            if (srcEntity.HasComponent<RigidBody2DComponent>())
                physicsEntities.push_back(dstHandle);
        }

        for (auto& [srcParent, srcChildren] : source->m_EntityChildren)
        for (auto srcChild : srcChildren)
        {
            if (!source->m_Registry.valid(srcChild) || !source->m_Registry.valid(srcParent))
                continue;
            Entity srcChildEntity{ srcChild, source.get() };
            uint64_t childUUID = srcChildEntity.GetComponent<IDComponent>().ID;

            Entity srcParentEntity{ srcParent, source.get() };
            uint64_t parentUUID = srcParentEntity.GetComponent<IDComponent>().ID;

            if (uuidMap.count(childUUID) && uuidMap.count(parentUUID))
                SetEntityParent({ uuidMap[childUUID], this }, { uuidMap[parentUUID], this }, false);
        }

        // Fizik gövdeleri OnRuntimeStart'ta PhysicsWorld2D/3D tarafından kurulur
        (void)physicsEntities;
        UpdateWorldTransforms();

        SetSkybox(source->GetSkybox());
        SetSkyboxExposure(source->GetSkyboxExposure());
        SetSkyboxRotation(source->GetSkyboxRotation());
        SetSkyboxTint(source->GetSkyboxTint());
        SetEnvironmentLightingSource(source->GetEnvironmentLightingSource());
        SetAmbientColor(source->GetAmbientColor());
        SetAmbientSkyColor(source->GetAmbientSkyColor());
        SetAmbientEquatorColor(source->GetAmbientEquatorColor());
        SetAmbientGroundColor(source->GetAmbientGroundColor());
        SetAmbientIntensity(source->GetAmbientIntensity());
        SetFogEnabled(source->IsFogEnabled());
        SetFogColor(source->GetFogColor());
        SetFogDensity(source->GetFogDensity());
        SetFogStart(source->GetFogStart());
        SetFogEnd(source->GetFogEnd());
        SetSunSource(source->GetSunSource());
        SetHaloEnabled(source->IsHaloEnabled());
        SetHaloStrength(source->GetHaloStrength());
        SetFlareEnabled(source->IsFlareEnabled());
        SetFlareFadeSpeed(source->GetFlareFadeSpeed());
        SetFlareStrength(source->GetFlareStrength());

        // Play moduna geçişte tüm ışık/render ayarları korunmalı
        m_RenderSettings = source->m_RenderSettings;
        m_FlareElements = source->m_FlareElements;
        m_HaloTexture = source->m_HaloTexture;
        m_SpotCookie = source->m_SpotCookie;
        m_ReflectionSource = source->m_ReflectionSource;
        m_ReflectionResolution = source->m_ReflectionResolution;
        m_ReflectionCompression = source->m_ReflectionCompression;
        m_ReflectionBounces = source->m_ReflectionBounces;
        m_BakedLightmapPath = source->m_BakedLightmapPath;
        m_BakedLightmapTexture = source->m_BakedLightmapTexture;
        m_LightmapResolution = source->m_LightmapResolution;
        m_MaxLightmapSize = source->m_MaxLightmapSize;
        m_MaxBounces = source->m_MaxBounces;
        m_DirectSamples = source->m_DirectSamples;
        m_IndirectSamples = source->m_IndirectSamples;
        m_AmbientOcclusion = source->m_AmbientOcclusion;
        m_Filtering = source->m_Filtering;
        m_APV = source->m_APV;
        m_OcclusionData = source->m_OcclusionData;

        m_Layers = source->GetLayers();
    }

    void Scene::StartPendingScripts()
    {
        ScriptContext::SetActiveScene(this);
        // Önce bekleyenleri topla: örnek oluşturma ve Awake/Start varlık oluşturup silebilir
        // (görünüm yinelemesi ve bileşen başvuruları geçersizleşebilir) -> her adımda yeniden bul.
        std::vector<std::pair<entt::entity, size_t>> pending;
        {
            auto view = m_Registry.view<ConquerorScriptComponent>();
            for (auto entity : view)
            {
                const auto& scripts = view.get<ConquerorScriptComponent>(entity).Scripts;
                for (size_t i = 0; i < scripts.size(); ++i)
                    if (!scripts[i].Instance && !scripts[i].ScriptPath.empty() && !scripts[i].bLoaded)
                        pending.emplace_back(entity, i);
            }
        }
        if (pending.empty())
            return;
        // Script Execution Order (Project Settings): küçük sıra numarası önce
        if (!ProjectSettings::ScriptOrder().empty())
            std::stable_sort(pending.begin(), pending.end(), [this](const auto& a, const auto& b) {
                const auto& sa = m_Registry.get<ConquerorScriptComponent>(a.first).Scripts[a.second];
                const auto& sb = m_Registry.get<ConquerorScriptComponent>(b.first).Scripts[b.second];
                return ProjectSettings::GetScriptOrder(sa.ClassName) < ProjectSettings::GetScriptOrder(sb.ClassName);
            });

        auto findScript = [this](entt::entity entity, size_t index) -> ConquerorScriptData* {
            if (!m_Registry.valid(entity))
                return nullptr;
            auto* sc = m_Registry.try_get<ConquerorScriptComponent>(entity);
            return sc && index < sc->Scripts.size() ? &sc->Scripts[index] : nullptr;
        };

        std::vector<std::tuple<Scripting::ScriptRuntime*, void*, entt::entity>> created;
        std::vector<const char*> createdNames;   // profiler örnek adı (sınıf adı)
        for (auto [entity, index] : pending)
        {
            ConquerorScriptData* script = findScript(entity, index);
            if (!script || script->Instance || script->ScriptPath.empty() || script->bLoaded)
                continue;
            script->bLoaded = true; // Sadece bir kez dene (hata her karede tekrarlanmasın)

            std::string resolvedPath = script->ScriptPath;
            std::filesystem::path p(script->ScriptPath);
            if (p.is_relative())
            {
                auto projectDir = Project::GetActiveProjectDirectory();
                if (!projectDir.empty())
                {
                    auto absolute = projectDir / p;
                    if (std::filesystem::exists(absolute))
                        resolvedPath = absolute.string();
                }
            }

            // Profiler: betik başına yükleme + örnekleme (Play başındaki takılmanın hangi betikten geldiği görünsün)
            CQ_PROFILE_SAMPLE(Profiling::FrameProfiler::Intern("Load " + p.filename().string()), Scripts);
            auto* runtime = Scripting::ScriptRuntimes::ForPath(resolvedPath);
            if (!runtime || !runtime->IsAvailable())
            {
                CQ_CORE_ERROR("[Script] No runtime for {0}{1}", resolvedPath, runtime ? " (" + runtime->GetUnavailableReason() + ")" : std::string());
                continue;
            }
            if (!runtime->LoadScript(resolvedPath, nullptr))
                continue;
            script = findScript(entity, index);
            if (!script)
                continue;
            const std::string className = script->ClassName.empty() ? runtime->GetDefaultClass(resolvedPath) : script->ClassName;
            const auto overrides = script->FieldOverrides;
            void* instance = runtime->CreateInstance(resolvedPath, className, (uint32_t)entity);
            if (!instance)
                continue;
            // Alan başlatıcıları varlığı silmiş olabilir
            script = findScript(entity, index);
            if (!script || script->Instance)
            {
                runtime->DestroyInstance(instance);
                continue;
            }
            script->Instance = instance;
            script->Runtime = runtime;
            // Denetçide değiştirilmiş alan değerleri (Awake'ten önce)
            for (auto& [field, value] : overrides)
                runtime->SetField(instance, field, value);
            created.emplace_back(runtime, instance, entity);
            createdNames.push_back(Profiling::FrameProfiler::Intern(className));
        }

        // Unity sırası: bu partideki tüm Awake'ler, sonra tüm Start'lar (Start'ta diğer betiklerin Awake'i bitmiş olur)
        for (const char* phase : { "Awake", "Start" })
        {
            for (size_t ci = 0; ci < created.size(); ++ci)
            {
                auto& [runtime, instance, entity] = created[ci];
                if (Scripting::ScriptRuntimes::IsRetired(instance))
                    continue;
                ScriptContext::EntityScope scope((uint32_t)entity);
                CQ_PROFILE_SAMPLE(createdNames[ci], Scripts);
                if (phase[0] == 'A')
                {
                    runtime->Invoke(instance, "Awake", nullptr, 0);
                    if (!Scripting::ScriptRuntimes::IsRetired(instance))
                        runtime->Invoke(instance, "OnCreate", nullptr, 0);
                }
                else
                    runtime->Invoke(instance, "Start", nullptr, 0);
            }
        }
    }

    void Scene::OnUpdateRuntime([[maybe_unused]] Timestep ts, bool renderOnly)
    {
        // Oyun zamanı: zaman ölçeği (t.TimeScale, slomo, betikten SetTimeScale) fizik, animasyon, yapay zeka, parçacık,
        // tween ve betiklerin dt'sine uygulanır (Unity Time.timeScale / Unreal Global Time Dilation). Ağ, video ve ses
        // gerçek zamanla sürer. Eskiden ölçek yalnız betiklerin GetDeltaTime()'ını değiştiriyordu.
        const Timestep realTs = ts;
        ts = Timestep((float)ts * (TimeManager::IsPaused() ? 0.0f : TimeManager::GetTimeScale()));
        // Karakter hareket istekleri bu karenin süresiyle hıza çevrilir (betiklerden önce)
        if (!renderOnly && m_PhysicsWorld3D)
            m_PhysicsWorld3D->BeginFrame(ts);
        if (!renderOnly)
        {
        // Ağ: gelen mesajlar, uzak nesne oluşturma/silme ve ara değerleme (betiklerden önce)
        Net::NetworkSession::Get().Update(this, (float)realTs);
        // XR: baş / el pozları (gözlük yoksa simülatör), ışınlar, tutma, ışınlanma (betiklerden ve fizikten önce)
        XRInteraction::Update(*this, (float)ts);
        // MetaSound Source: oynat / konum / bitiş
        MetaSounds::Update(*this);
        // World Partition: akış kaynağına yakın hücreler yüklü, uzaktakiler boşaltılır
        WorldPartition::Update(*this, (float)ts);

        // Update Native Scripts - OnCreate
        {
            CQ_PROFILE_SAMPLE("Scripts.Start", Scripts);
            auto view = m_Registry.view<NativeScriptComponent>();
            
            for (auto entity : view)
            {
                auto& sc = view.get<NativeScriptComponent>(entity);
                for (auto& script : sc.Scripts)
                {
                    if (!script.Instance && script.InstantiateScript)
                    {
                        script.Instance = script.InstantiateScript();
                        if (!script.Instance)
                        {
                            // Fabrika bulunamadı (modül derlenemedi / yüklenemedi): her karede yeniden denenmesin
                            script.InstantiateScript = nullptr;
                            continue;
                        }
                        script.Instance->m_Entity = Entity{ entity, this };
                        script.Instance->OnCreate();
                    }
                }
            }
        }

        // ConquerorScript: yükle + örnekle + Awake/OnCreate/Start
        {
            CQ_PROFILE_SAMPLE("Scripts.Awake/Start", Scripts);
            StartPendingScripts();
        }

        // Update audio system
        {
            CQ_PROFILE_SAMPLE("Audio.Update", Audio);
            UpdateAudioSystem(realTs);
        }

        // Update physics
        if (m_PhysicsWarmedUp)
        {
            {
                CQ_PROFILE_SAMPLE("Physics2D.Step", Physics);
                m_PhysicsWorld2D->Step(ts);
            }
            {
                CQ_PROFILE_SAMPLE("Physics3D.Step", Physics);
                Articulations::PreStep(*this);
                WorldActors::Update(*this, (float)ts);   // Kill Z / Pain / Physics hacimleri, Radial Force
                ActorComponents::Update(*this, (float)ts, true);   // Floating Pawn / Interp To Movement, Cable, Cine Camera
                m_PhysicsWorld3D->Step(ts);
                Articulations::PostStep(*this);
            }
            CQ_PROFILE_SAMPLE("Physics.Events", Scripts);
            DispatchPhysicsEvents();
            DispatchPhysicsEvents2D();
        }
        else
        {
            m_PhysicsWarmedUp = true;
        }
        {
            CQ_PROFILE_SAMPLE("Particles.Update", Particles);
            UpdateParticles(ts.GetSeconds(), false);
        }

        // Animasyon: ViewportPanel her karede OnUpdateEditor ile çiziyor; burada da güncellenirse zaman 2× hızlanır.

        // Update Native Scripts - OnUpdate
        {
            CQ_PROFILE_SAMPLE("NativeScripts.Update", Scripts);
            auto view = m_Registry.view<NativeScriptComponent>();
            for (auto entity : view)
            {
                auto& sc = view.get<NativeScriptComponent>(entity);
                for (auto& script : sc.Scripts)
                {
                    if (script.Instance)
                        script.Instance->OnUpdate(ts);
                }
            }
        }

        // Fare olayları ve Player Input mesajları (Unity: Update'ten önce)
        {
            CQ_PROFILE_SAMPLE("Scripts.MouseEvents", Scripts);
            UpdateMouseEvents();
            PlayerInputs::Update(*this);
        }

        // ConquerorScript: Update / LateUpdate
        {
            ScriptContext::SetActiveScene(this);
            // Godot düğümleri: Remote Transform, Ray Cast (betiklerden önce taze), Parallax, ekran bildirimleri
            GodotNodes::Update(*this, (float)ts, true);
            CQS::Value dt = CQS::Value::MakeFloat((double)ts);
            // Betikler varlık oluşturup silebilir: önce örnek listesini topla (Script Execution Order sırasıyla)
            std::vector<std::pair<Scripting::ScriptRuntime*, void*>> instances;
            CollectScriptInstances(instances);
            {
                CQ_PROFILE_SAMPLE("Scripts.Update", Scripts);
                for (auto& [runtime, inst] : instances)
                {
                    // Önceki bir betik bu varlığı (ya da kendini) silmiş olabilir
                    if (Scripting::ScriptRuntimes::IsRetired(inst))
                        continue;
                    runtime->Invoke(inst, "OnUpdate", &dt, 1);
                    if (!Scripting::ScriptRuntimes::IsRetired(inst))
                        runtime->Invoke(inst, "Update", &dt, 1);
                }
            }
            // Zaman çizelgeleri (PlayableDirector): Update'ten sonra, LateUpdate'ten önce (Unity animasyon aşaması gibi)
            TimelineSystem::Update(*this, (float)ts);
            // Animation bileşenleri (.cqanim özellik klipleri)
            PropertyClips::Update(*this, (float)ts);
            // Bileşen sistemleri (döndürücü, mermi, kısıtlar, iz kayıtları, rüzgâr)
            {
                CQ_PROFILE_SAMPLE("GameplaySystems.Update", Other);
                GameplaySystems::Update(*this, (float)ts, true);
            }
            {
                CQ_PROFILE_SAMPLE("Scripts.LateUpdate", Scripts);
                for (auto& [runtime, inst] : instances)
                {
                    if (Scripting::ScriptRuntimes::IsRetired(inst))
                        continue;
                    runtime->Invoke(inst, "OnLateUpdate", &dt, 1);
                    if (!Scripting::ScriptRuntimes::IsRetired(inst))
                        runtime->Invoke(inst, "LateUpdate", &dt, 1);
                }
            }
            // Kamera takibi betiklerin LateUpdate'inden sonra
            {
                CQ_PROFILE_SAMPLE("GameplaySystems.LateUpdate", Other);
                GameplaySystems::LateUpdate(*this, (float)ts, true);
                GameFramework::LateUpdate(*this, (float)ts);   // Spring Arm kamera kolları
                Skeleton2D::Update(*this, true);               // 2D IK: animasyon ve betiklerden sonra
                SpriteAnimation2D::Update(*this);              // Sprite Resolver: kütüphaneden sprite seçimi
                SpriteShapes::Update(*this);                   // Sprite Shape -> Edge / Polygon Collider 2D
            }
            {
                CQ_PROFILE_SAMPLE("UI.Events", UI);
                DispatchUIEventsToScripts(instances);
            }
            CQ_PROFILE_SAMPLE("Scripts.EndFrame", GC);
            // Bu karede silinen varlıkların betik örnekleri (artık hiçbir betik çalışmıyor)
            Scripting::ScriptRuntimes::FlushRetired();
            Scripting::ScriptRuntimes::EndFrame();
        }

        // NavMesh ajanları: yol takibi, ivme, dönüş, link geçişi, kaçınma, Carve engeller
        {
            CQ_PROFILE_SAMPLE("AI.NavMeshAgents", AI);
            NavAgents::Update(*this, (float)ts);
            Navigation2D::Update(*this, (float)ts);
            Foliage::Update(*this);
            BehaviorTrees::Update(*this, (float)ts);
            AssetRuntime::Update(*this, (float)ts);
            AnimationAssets::Update(*this);
        }

        // Video oynatıcılar (kare çözme + hedef dokular)
        {
            CQ_PROFILE_SAMPLE("Video.Update", Other);
            VideoPlayers::Update(*this, (float)realTs);
        }

        // Ability System: efekt süreleri, periyodik tikler, bekleme süreleri
        {
            CQ_PROFILE_SAMPLE("Gameplay.Abilities", Other);
            AbilitySystem::Update(*this, (float)ts);
            GameFramework::Update(*this, (float)ts);
            LevelStreaming::Update(*this, (float)ts);
            AutoLOD::Update(*this);
            VirtualGeometry::Update();
            CharacterCustomizer::Update(*this);
            CSG::Update(*this);
            MultiMesh::Update(*this);
            Signals::Update(*this);
            Tweens::Update(*this, (float)ts);
            Fracture::Update(*this, (float)ts);
            Ragdoll::Update(*this);
        }

        // Animators
        {
            CQ_PROFILE_SAMPLE("Animators.Update", Animation);
            UpdateAnimators(ts);
            // Leader Pose (MetaHuman parçaları gövdeyi izler) ve Live Link Face yüz kaydı
            MetaHuman::Update(*this, (float)ts);
        }

        // Ağ: durum gönderimi (betikler ve animasyondan sonra)
        Net::NetworkSession::Get().PostUpdate(this, (float)realTs);

        } // !renderOnly

        // Dünya dönüşümleri her karede güncellenir (kamera olmasa da): betikler, fizik eşitlemesi, ses dinleyicisi
        // ve otomasyon dünya konumunu okur. Eskiden yalnızca kamera varken render sırasında güncelleniyordu.
        {
            CQ_PROFILE_SAMPLE("Transforms.Update", Other);
            UpdateWorldTransforms();
        }

        // Render Scene (Runtime)
        Entity cameraEntity = GetPrimaryCameraEntity();
        if (!cameraEntity)
        {
            auto view = m_Registry.view<CameraComponent>();
            if (!view.empty())
                cameraEntity = Entity{ view.front(), this };
        }

        if (cameraEntity)
        {
            CQ_PROFILE_SAMPLE("Render.Scene", Rendering);
            auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;

            // Kamera -> doku hedefleri (ana görünüm onları bu karede örnekleyebilsin)
            CameraRenderTargets::Render(*this);
            // OpenXR: iki göz gözlüğe (kare bekleme + gönderim; oyun penceresi aynalı görünümü aşağıda çizer)
            if (!renderOnly)
            {
                CQ_PROFILE_SAMPLE("Render.XR", Rendering);
                OpenXR::RenderFrame(*this);
            }

            // Stats'ı sıfırla
            Renderer::ResetStats();

            // Kamera DÜNYA transform'u (oyuncuya bağlı kameralar için şart; yukarıda güncellendi)
            glm::mat4 cameraTransform = cameraEntity.GetComponent<TransformComponent>().WorldTransform;
            // Pixel Perfect Camera: kamera ve sprite konumları piksel ızgarasına oturur (titreme yok)
            const float pixelGrid = PixelPerfect::SnapGrid(*this, cameraEntity);
            if (pixelGrid > 0.0f)
            {
                cameraTransform[3][0] = std::round(cameraTransform[3][0] * pixelGrid) / pixelGrid;
                cameraTransform[3][1] = std::round(cameraTransform[3][1] * pixelGrid) / pixelGrid;
            }

            // 3D sahne (gölge, gökyüzü, ışıklar, meshler, şeffaflar)
            RenderCamera renderCamera = RenderCamera::FromSceneCamera(camera, cameraTransform, m_ViewportWidth, m_ViewportHeight);
            m_BackgroundCamera = (entt::entity)cameraEntity;
            RenderScene3D(renderCamera, false);
            m_BackgroundCamera = entt::null;

            // 2D Rendering - Depth test AÇIK, depth write AÇIK (örnek bu bloğun sonuna kadar sürer)
            CQ_PROFILE_SAMPLE("Render.2D", Rendering);
            CQ_PROFILE_GPU("2D");
            // Light 2D ışık dokusu + Sprite Mask stencil'i (sahnede yoksa hiçbir şey yapmaz)
            const glm::mat4 viewProjection2D = camera.GetProjection() * glm::inverse(cameraTransform);
            const bool lit2D = Lighting2D::BeginPass(*this, viewProjection2D);
            CustomRenderTextures::UpdateAll();   // 2D sahnelerde de
            Renderer2D::BeginScene(camera, cameraTransform, true, true);
            TextRenderer::BeginScene(camera, cameraTransform, true);

            // Tüm render edilebilir entity'leri topla (sprite + image + text)
            struct RenderItem
            {
                enum class Type { Sprite, Image, Text, Tilemap, SpriteShape };
                Type ItemType;
                entt::entity Entity;
                TransformComponent* Transform;
                void* Component;
                int32_t Layer;
                int32_t OrderInLayer;
                float ZPosition;
            };

            std::vector<RenderItem> renderItems;

            // Sprite'ları topla
            auto spriteGroup = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            for (auto entity : spriteGroup)
            {
                auto [transform, sprite] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entity);
                if (!IsEntityActive(entity))
                    continue;

                // Dokusuz sprite düz renkli dikdörtgen çizilir (editör görünümüyle aynı)
                int32_t layer = 0;
                int32_t orderInLayer = 0;
                if (m_Registry.all_of<LayerComponent>(entity))
                {
                    auto& layerComp = m_Registry.get<LayerComponent>(entity);
                    layer = layerComp.Layer;
                    orderInLayer = layerComp.OrderInLayer;
                }

                renderItems.push_back({ RenderItem::Type::Sprite, entity, &transform, (void*)&sprite, layer, orderInLayer, transform.WorldTransform[3][2] });
            }


            // Text'leri topla
            auto textView = m_Registry.view<TransformComponent, TextRendererComponent>();
            for (auto entity : textView)
            {
                auto [transform, text] = textView.get<TransformComponent, TextRendererComponent>(entity);
                if (!IsEntityActive(entity))
                    continue;

                int32_t layer = 5;
                int32_t orderInLayer = 0;
                if (m_Registry.all_of<LayerComponent>(entity))
                {
                    auto& layerComp = m_Registry.get<LayerComponent>(entity);
                    layer = layerComp.Layer;
                    orderInLayer = layerComp.OrderInLayer;
                }

                renderItems.push_back({ RenderItem::Type::Text, entity, &transform, (void*)&text, layer, orderInLayer, transform.WorldTransform[3][2] });
            }

            // Karo haritaları (2D)
            auto tilemapView = m_Registry.view<TransformComponent, TilemapComponent>();
            for (auto entity : tilemapView)
            {
                auto [transform, tilemap] = tilemapView.get<TransformComponent, TilemapComponent>(entity);
                if (!IsEntityActive(entity) || tilemap.Render3D)
                    continue;
                int32_t layer = m_Registry.all_of<LayerComponent>(entity) ? m_Registry.get<LayerComponent>(entity).Layer : 0;
                renderItems.push_back({ RenderItem::Type::Tilemap, entity, &transform, (void*)&tilemap, layer, tilemap.OrderInLayer, transform.WorldTransform[3][2] });
            }
            // Sprite Shape'ler
            for (auto entity : m_Registry.view<TransformComponent, SpriteShapeRendererComponent, SpriteShapeControllerComponent>())
            {
                if (!IsEntityActive(entity))
                    continue;
                auto& transform = m_Registry.get<TransformComponent>(entity);
                int32_t layer = 0, order = 0;
                if (const auto* lc = m_Registry.try_get<LayerComponent>(entity)) { layer = lc->Layer; order = lc->OrderInLayer; }
                renderItems.push_back({ RenderItem::Type::SpriteShape, entity, &transform, nullptr, layer, order, transform.WorldTransform[3][2] });
            }

            // Layer ve OrderInLayer'a göre sırala
            std::sort(renderItems.begin(), renderItems.end(), 
                [this](const RenderItem& a, const RenderItem& b) {
                    int32_t aSortOrder = 0;
                    int32_t bSortOrder = 0;
                    
                    Entity aEntity{ a.Entity, this };
                    Entity bEntity{ b.Entity, this };
                    
                    if (IsCanvasOrCanvasChild(aEntity))
                    {
                        Entity canvasA = aEntity;
                        while (canvasA && !canvasA.HasComponent<CanvasComponent>())
                        {
                            canvasA = GetEntityParent(canvasA);
                        }
                        if (canvasA && canvasA.HasComponent<CanvasComponent>())
                        {
                            aSortOrder = canvasA.GetComponent<CanvasComponent>().SortOrder;
                        }
                    }
                    
                    if (IsCanvasOrCanvasChild(bEntity))
                    {
                        Entity canvasB = bEntity;
                        while (canvasB && !canvasB.HasComponent<CanvasComponent>())
                        {
                            canvasB = GetEntityParent(canvasB);
                        }
                        if (canvasB && canvasB.HasComponent<CanvasComponent>())
                        {
                            bSortOrder = canvasB.GetComponent<CanvasComponent>().SortOrder;
                        }
                    }
                    
                    if (aSortOrder != bSortOrder) return aSortOrder < bSortOrder;
                    if (a.Layer != b.Layer) return a.Layer < b.Layer;
                    if (a.OrderInLayer != b.OrderInLayer) return a.OrderInLayer < b.OrderInLayer;
                    return a.ZPosition < b.ZPosition;
                });

            // TÜM objeleri render et (Canvas kontrolü ile depth test)
            // Screen-Space Ortho VP: Canvas'ın dünya konumundan bağımsız olarak
            // GamePanel'e tam sığacak şekilde render edilmesi için kullanılır.
            // Referans canvas boyutu: 1920 x 1080 birim (world-unit cinsinden 19.2 x 10.8)
            // 10x visual scale uygulandığı için ortho'yu da 10x genişletiyoruz
            float uiHalfW = 1920.0f / 200.0f * 10.0f; // 96.0
            float uiHalfH = 1080.0f / 200.0f * 10.0f; // 54.0
            glm::mat4 screenSpaceVP = glm::ortho(-uiHalfW, uiHalfW, -uiHalfH, uiHalfH, -100.0f, 100.0f);

            // Geçiş durumu yalnızca değişince yeniden kurulur (eskiden her sprite'tan sonra
            // EndScene/BeginScene yapılıyordu: sprite başına bir draw call)
            enum class Pass2D { World, Screen, WorldNoDepth };
            Pass2D currentPass = Pass2D::World;
            bool lastWasText = false;
            auto switchPass = [&](Pass2D pass, bool force) {
                if (pass == currentPass && !force)
                    return;
                Renderer2D::EndScene();
                TextRenderer::EndScene();
                if (pass == Pass2D::Screen)
                {
                    Renderer2D::BeginScene(screenSpaceVP, false);
                    TextRenderer::BeginScene(screenSpaceVP, false);
                }
                else
                {
                    const bool depth = pass == Pass2D::World;
                    Renderer2D::BeginScene(camera, cameraTransform, depth, true);
                    TextRenderer::BeginScene(camera, cameraTransform, depth);
                }
                currentPass = pass;
            };

            for (auto& item : renderItems)
            {
                Entity itemEntity{ item.Entity, this };
                bool isCanvasChild = IsCanvasOrCanvasChild(itemEntity);
                
                bool depthTestEnabled = true;
                bool isScreenSpace = false;
                glm::vec3 canvasWorldPos(0.0f);

                if (isCanvasChild)
                {
                    Entity canvasEntity = itemEntity;
                    while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                        canvasEntity = GetEntityParent(canvasEntity);
                    
                    if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                    {
                        auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                        if (canvas.Mode == CanvasComponent::RenderMode::WorldSpace)
                            depthTestEnabled = true;
                        else
                        {
                            depthTestEnabled = false;
                            isScreenSpace = true;
                            // Canvas'ın dünya pozisyonunu al (relative transform için)
                            canvasWorldPos = glm::vec3(canvasEntity.GetComponent<TransformComponent>().WorldTransform[3]);
                        }
                    }
                }
                
                // Metin ile sprite iki ayrı toplu çizicide: tür değişince önceki toplu çizim boşaltılır (sıra korunur)
                const bool isText = item.ItemType == RenderItem::Type::Text;
                switchPass(isScreenSpace ? Pass2D::Screen : (depthTestEnabled ? Pass2D::World : Pass2D::WorldNoDepth), isText != lastWasText);
                lastWasText = isText;
                // Sprite ve karo haritaları 2D ışıkla çarpılır; UI ve metin ışıksız
                const bool litItem = lit2D && !isCanvasChild && (item.ItemType == RenderItem::Type::Sprite || item.ItemType == RenderItem::Type::Tilemap || item.ItemType == RenderItem::Type::SpriteShape);
                Renderer2D::SetLit(litItem);
                Renderer2D::SetMaskInteraction(item.ItemType == RenderItem::Type::Sprite ? (int)((SpriteRendererComponent*)item.Component)->MaskInteraction : 0);
                
                glm::mat4 finalTransform = item.Transform->WorldTransform;
                if (isCanvasChild)
                {
                    Entity canvasEntity = itemEntity;
                    while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                        canvasEntity = GetEntityParent(canvasEntity);
                    
                    if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                    {
                        auto& canvas = canvasEntity.GetComponent<CanvasComponent>();

                        if (isScreenSpace)
                        {
                            // Canvas'ın dünya konumundan bağımsız: child'ın Canvas'a göreli konumunu al
                            // finalTransform[3] = child worldPos - canvasWorldPos → canvas-local pos
                            finalTransform[3][0] -= canvasWorldPos.x;
                            finalTransform[3][1] -= canvasWorldPos.y;
                            finalTransform[3][2]  = 0.0f; // UI'ı Z=0'a sabitle
                        }
                        
                        if (canvasEntity.HasComponent<CanvasScalerComponent>())
                        {
                            auto& scaler = canvasEntity.GetComponent<CanvasScalerComponent>();
                            float pixelToWorldScale = 100.0f / scaler.ReferencePixelsPerUnit;
                            
                            if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPixelSize)
                            {
                                glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                                scale *= scaler.ScaleFactor * pixelToWorldScale;
                                finalTransform[0][0] = scale.x; finalTransform[1][1] = scale.y; finalTransform[2][2] = scale.z;
                            }
                            else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize)
                            {
                                // Hem genişlik hem yüksekliği kullan (aspect-ratio aware, 1920x1080 referans)
                                float scaleX = (float)m_ViewportWidth  / 1920.0f;
                                float scaleY = (float)m_ViewportHeight / 1080.0f;
                                float screenScale = glm::sqrt(scaleX * scaleY);
                                screenScale *= scaler.ScaleFactor * pixelToWorldScale;
                                glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                                scale *= screenScale;
                                finalTransform[0][0] = scale.x; finalTransform[1][1] = scale.y; finalTransform[2][2] = scale.z;
                            }
                            else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPhysicalSize)
                            {
                                float viewportDiagPixels = glm::sqrt(
                                    (float)(m_ViewportWidth * m_ViewportWidth + m_ViewportHeight * m_ViewportHeight));
                                float refDiagPixels = glm::sqrt(1920.0f * 1920.0f + 1080.0f * 1080.0f);
                                float physicalScale = viewportDiagPixels / refDiagPixels;
                                physicalScale *= scaler.ScaleFactor * pixelToWorldScale;
                                glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                                scale *= physicalScale;
                                finalTransform[0][0] = scale.x; finalTransform[1][1] = scale.y; finalTransform[2][2] = scale.z;
                            }
                        }
                        
                        if (canvas.PixelPerfect)
                        {
                            finalTransform[3][0] = glm::round(finalTransform[3][0]);
                            finalTransform[3][1] = glm::round(finalTransform[3][1]);
                        }
                    }
                    
                    // Ekranda 10 kat büyük görünsün (Transform değeri değişmeden)
                    for (int i = 0; i < 3; i++) {
                        finalTransform[i][0] *= 10.0f;
                        finalTransform[i][1] *= 10.0f;
                        finalTransform[i][2] *= 10.0f;
                    }
                }
                
                if (item.ItemType == RenderItem::Type::Tilemap)
                {
                    GridSystem::DrawTilemap2D(*this, itemEntity);
                }
                else if (item.ItemType == RenderItem::Type::SpriteShape)
                {
                    SpriteShapes::Draw(*this, item.Entity);
                }
                else if (item.ItemType == RenderItem::Type::Sprite)
                {
                    auto* sprite = (SpriteRendererComponent*)item.Component;
                    if (pixelGrid > 0.0f && !isCanvasChild)
                    {
                        finalTransform[3][0] = std::round(finalTransform[3][0] * pixelGrid) / pixelGrid;
                        finalTransform[3][1] = std::round(finalTransform[3][1] * pixelGrid) / pixelGrid;
                    }
                    // Eski (Legacy) düğme: arka planı Sprite Renderer; renk düğme durumundan, isteğe bağlı 9 dilim
                    if (m_Registry.all_of<ButtonComponent>(item.Entity) && sprite->Texture)
                    {
                        auto& button = m_Registry.get<ButtonComponent>(item.Entity);
                        if (button.Use9Slice)
                            Renderer2D::DrawSprite9Slice(finalTransform, sprite->Texture, button.BorderSize, button.CurrentBackgroundColor, *sprite);
                        else
                            Renderer2D::DrawSprite(finalTransform, sprite->Texture, sprite->TilingFactor, button.CurrentBackgroundColor, *sprite);
                    }
                    // Sprite Skin: kemiklerle bükülmüş ağ (bağlıysa)
                    else if (!SpriteAnimation2D::DrawSkinned(*this, item.Entity, *sprite, sprite->Color))
                        Renderer2D::DrawSprite(finalTransform, sprite->Texture, sprite->TilingFactor, sprite->Color, *sprite);
                }
                else if (item.ItemType == RenderItem::Type::Text)
                {
                    auto* text = (TextRendererComponent*)item.Component;
                    glm::vec4 finalTextColor = text->Color;
                    glm::mat4 textTransform = finalTransform;
                    
                    Entity textEntity{ item.Entity, this };
                    Entity parent = GetEntityParent(textEntity);
                    
                    if (parent && parent.HasComponent<ButtonComponent>() && parent.HasComponent<SpriteRendererComponent>())
                    {
                        auto& button = parent.GetComponent<ButtonComponent>();
                        auto& parentImage = parent.GetComponent<SpriteRendererComponent>();
                        finalTextColor = button.CurrentTextColor;
                        
                        if (parentImage.Texture)
                        {
                            float halfWidth = (float)parentImage.Texture->GetWidth() / 200.0f;
                            float halfHeight = (float)parentImage.Texture->GetHeight() / 200.0f;
                            glm::vec2 offset = glm::vec2(-halfWidth * 0.93f, -halfHeight * 0.5f);
                            textTransform[3][0] += offset.x;
                            textTransform[3][1] += offset.y;
                        }
                    }
                    
                    TextRenderer::DrawText(text->AutoTranslate ? Localization::Translate(text->Text) : text->Text, textTransform, text->FontSize / 100.0f, finalTextColor, (int)text->Alignment);
                }
            }

            Renderer2D::EndScene();
            TextRenderer::EndScene();
            Lighting2D::EndPass(*this, viewProjection2D);
        }
    }

    void Scene::OnUpdateSimulation([[maybe_unused]] Timestep ts)
    {
        // Update physics only
        m_PhysicsWorld2D->Step(ts);
        m_PhysicsWorld3D->Step(ts);
    }


    // Unity gibi: seçili varlıkların çarpıştırıcıları yeşil tel kafes, tetikleyiciler açık mavi,
    // karakter denetleyici kapsülü, eklem çapası ve ekseni
    Scene::RuntimeDebugView& Scene::RuntimeDebug()
    {
        static RuntimeDebugView view;
        return view;
    }

    void Scene::DrawRuntimeDebug()
    {
        const RuntimeDebugView& d = RuntimeDebug();
        if (d.CollisionShapes)
        {
            DrawPhysicsGizmos(true);
            // 2D çarpıştırıcılar (Godot Visible Collision Shapes)
            const glm::vec4 k2D(0.45f, 0.85f, 1.0f, 0.95f);
            auto worldOf = [&](Entity e) {
                glm::mat4 m = e.GetComponent<TransformComponent>().GetTransform();
                for (Entity p = e.GetParent(); p; p = p.GetParent())
                    m = p.GetComponent<TransformComponent>().GetTransform() * m;
                return m;
            };
            auto toWorld = [](const glm::mat4& m, const glm::vec2& p) { return glm::vec3(m * glm::vec4(p, 0.0f, 1.0f)); };
            for (auto h : m_Registry.view<TransformComponent, BoxCollider2DComponent>())
            {
                Entity e{ h, this };
                const auto& c = e.GetComponent<BoxCollider2DComponent>();
                const glm::mat4 m = worldOf(e);
                const glm::vec2 hs = c.Size * 0.5f;
                DebugDraw::Polygon({ toWorld(m, c.Offset + glm::vec2(-hs.x, -hs.y)), toWorld(m, c.Offset + glm::vec2(hs.x, -hs.y)),
                                     toWorld(m, c.Offset + glm::vec2(hs.x, hs.y)), toWorld(m, c.Offset + glm::vec2(-hs.x, hs.y)) }, k2D, true, 0.0f, false);
            }
            for (auto h : m_Registry.view<TransformComponent, CircleCollider2DComponent>())
            {
                Entity e{ h, this };
                const auto& c = e.GetComponent<CircleCollider2DComponent>();
                const glm::mat4 m = worldOf(e);
                const float scale = std::max(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])));
                DebugDraw::Circle(toWorld(m, c.Offset), glm::vec3(0.0f, 0.0f, 1.0f), c.Radius * scale, k2D, 0.0f, false);
            }
            for (auto h : m_Registry.view<TransformComponent, CapsuleCollider2DComponent>())
            {
                Entity e{ h, this };
                const auto& c = e.GetComponent<CapsuleCollider2DComponent>();
                const glm::mat4 m = worldOf(e);
                const glm::vec2 hs = c.Size * 0.5f;
                DebugDraw::Polygon({ toWorld(m, c.Offset + glm::vec2(-hs.x, -hs.y)), toWorld(m, c.Offset + glm::vec2(hs.x, -hs.y)),
                                     toWorld(m, c.Offset + glm::vec2(hs.x, hs.y)), toWorld(m, c.Offset + glm::vec2(-hs.x, hs.y)) }, k2D, true, 0.0f, false);
            }
            for (auto h : m_Registry.view<TransformComponent, PolygonCollider2DComponent>())
            {
                Entity e{ h, this };
                const auto& c = e.GetComponent<PolygonCollider2DComponent>();
                const glm::mat4 m = worldOf(e);
                std::vector<glm::vec3> pts;
                for (const auto& p : c.Points)
                    pts.push_back(toWorld(m, c.Offset + p));
                if (pts.size() >= 2)
                    DebugDraw::Polygon(pts, k2D, true, 0.0f, false);
            }
        }
        if (d.Paths)
        {
            // Gezinme ajanlarının yolları (Godot Visible Paths)
            const glm::vec4 kPath(1.0f, 0.85f, 0.25f, 1.0f);
            for (auto h : m_Registry.view<NavMeshAgentComponent>())
            {
                const auto& a = m_Registry.get<NavMeshAgentComponent>(h);
                for (size_t i = 1; a.HasPath && i < a.Path.size(); ++i)
                    DebugDraw::Line(a.Path[i - 1] + glm::vec3(0.0f, 0.05f, 0.0f), a.Path[i] + glm::vec3(0.0f, 0.05f, 0.0f), kPath, 0.0f, false);
            }
            for (auto h : m_Registry.view<NavigationAgent2DComponent>())
            {
                const auto& a = m_Registry.get<NavigationAgent2DComponent>(h);
                for (size_t i = 1; i < a.Path.size(); ++i)
                    DebugDraw::Line({ a.Path[i - 1], 0.0f }, { a.Path[i], 0.0f }, kPath, 0.0f, false);
            }
        }
    }

    void Scene::DrawPhysicsGizmos(bool all)
    {
        const glm::vec4 kCollider(0.55f, 1.0f, 0.45f, 0.95f);
        const glm::vec4 kTrigger(0.45f, 0.85f, 1.0f, 0.95f);
        const glm::vec4 kCharacter(0.45f, 1.0f, 0.75f, 0.95f);
        const glm::vec4 kJoint(1.0f, 0.75f, 0.25f, 1.0f);
        auto worldOf = [&](Entity e) {
            glm::mat4 m = e.GetComponent<TransformComponent>().GetTransform();
            for (Entity p = e.GetParent(); p; p = p.GetParent())
                m = p.GetComponent<TransformComponent>().GetTransform() * m;
            return m;
        };
        auto draw = [&](entt::entity handle) {
            if (!m_Registry.valid(handle) || !m_Registry.all_of<TransformComponent>(handle))
                return;
            Entity e{ handle, this };
            glm::mat4 world = worldOf(e);
            glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
            glm::mat4 rotOnly = world;
            for (int i = 0; i < 3; ++i)
                rotOnly[i] = glm::vec4(glm::vec3(world[i]) / std::max(scale[i], 1e-6f), 0.0f);
            if (e.HasComponent<BoxColliderComponent>())
            {
                auto& c = e.GetComponent<BoxColliderComponent>();
                glm::mat4 m = world * glm::translate(glm::mat4(1.0f), c.Offset) * glm::scale(glm::mat4(1.0f), glm::max(c.Size, glm::vec3(1e-4f)));
                DebugDraw::Box(m, c.IsTrigger ? kTrigger : kCollider, 0.0f, false);
            }
            if (e.HasComponent<SphereColliderComponent>())
            {
                auto& c = e.GetComponent<SphereColliderComponent>();
                glm::vec3 center = glm::vec3(world * glm::vec4(c.Offset, 1.0f));
                float r = c.Radius * std::max(scale.x, std::max(scale.y, scale.z));
                glm::vec4 col = c.IsTrigger ? kTrigger : kCollider;
                glm::vec3 ax(rotOnly[0]), ay(rotOnly[1]), az(rotOnly[2]);
                DebugDraw::Circle(center, ax, r, col, 0.0f, false);
                DebugDraw::Circle(center, ay, r, col, 0.0f, false);
                DebugDraw::Circle(center, az, r, col, 0.0f, false);
            }
            if (e.HasComponent<CapsuleColliderComponent>() || e.HasComponent<CharacterControllerComponent>())
            {
                glm::vec3 offset(0.0f);
                float radius = 0.5f, height = 2.0f;
                int dir = 1;
                glm::vec4 col = kCollider;
                if (e.HasComponent<CharacterControllerComponent>())
                {
                    auto& c = e.GetComponent<CharacterControllerComponent>();
                    offset = c.Center; radius = c.Radius; height = c.Height; col = kCharacter;
                }
                else
                {
                    auto& c = e.GetComponent<CapsuleColliderComponent>();
                    offset = c.Offset; radius = c.Radius; height = c.Height; dir = std::clamp(c.Direction, 0, 2);
                    col = c.IsTrigger ? kTrigger : kCollider;
                }
                glm::vec3 axis = glm::normalize(glm::vec3(rotOnly[dir]));
                float radial = dir == 0 ? std::max(scale.y, scale.z) : dir == 1 ? std::max(scale.x, scale.z) : std::max(scale.x, scale.y);
                float r = radius * radial;
                float half = std::max(0.0f, height * scale[dir] * 0.5f - r);
                glm::vec3 center = glm::vec3(world * glm::vec4(offset, 1.0f));
                glm::vec3 a = center + axis * half, b = center - axis * half;
                glm::vec3 side1 = glm::normalize(glm::vec3(rotOnly[(dir + 1) % 3])), side2 = glm::normalize(glm::vec3(rotOnly[(dir + 2) % 3]));
                DebugDraw::Circle(a, axis, r, col, 0.0f, false);
                DebugDraw::Circle(b, axis, r, col, 0.0f, false);
                for (glm::vec3 s : { side1, -side1, side2, -side2 })
                    DebugDraw::Line(a + s * r, b + s * r, col, 0.0f, false);
                // Uç yarım küreler
                DebugDraw::Arc(a, side1, r, 0.0f, 180.0f, col, 0.0f, false);
                DebugDraw::Arc(a, side2, r, 0.0f, 180.0f, col, 0.0f, false);
                DebugDraw::Arc(b, side1, r, 180.0f, 360.0f, col, 0.0f, false);
                DebugDraw::Arc(b, side2, r, 180.0f, 360.0f, col, 0.0f, false);
            }
            if (e.HasComponent<MeshColliderComponent>())
            {
                // Ağ sınır kutusu (tam tel kafes pahalı)
                glm::mat4 m = world * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
                if (e.HasComponent<MeshRendererComponent>() && !e.GetComponent<MeshRendererComponent>().IsModel()
                    && e.GetComponent<MeshRendererComponent>().Type == MeshType::Plane)
                    m = world * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 0.001f, 2.0f));
                DebugDraw::Box(m, e.GetComponent<MeshColliderComponent>().IsTrigger ? kTrigger : kCollider, 0.0f, false);
            }
            // Eklemler: çapa noktası + eksen
            auto joint = [&](const JointCommon& j, const glm::vec3* axisLocal) {
                glm::vec3 anchor = glm::vec3(world * glm::vec4(j.Anchor, 1.0f));
                DebugDraw::Sphere(anchor, 0.08f, kJoint, 0.0f, false);
                if (axisLocal && glm::length(*axisLocal) > 1e-6f)
                {
                    glm::vec3 ax = glm::normalize(glm::mat3(rotOnly) * *axisLocal);
                    DebugDraw::Line(anchor - ax * 0.6f, anchor + ax * 0.6f, kJoint, 0.0f, false);
                }
                if (j.ConnectedBody)
                    if (Entity other = FindEntityByUUID(j.ConnectedBody))
                        DebugDraw::Line(anchor, glm::vec3(worldOf(other)[3]), glm::vec4(kJoint.r, kJoint.g, kJoint.b, 0.5f), 0.0f, false);
            };
            if (e.HasComponent<HingeJointComponent>()) joint(e.GetComponent<HingeJointComponent>(), &e.GetComponent<HingeJointComponent>().Axis);
            if (e.HasComponent<SliderJointComponent>()) joint(e.GetComponent<SliderJointComponent>(), &e.GetComponent<SliderJointComponent>().Axis);
            if (e.HasComponent<CharacterJointComponent>()) joint(e.GetComponent<CharacterJointComponent>(), &e.GetComponent<CharacterJointComponent>().Axis);
            if (e.HasComponent<FixedJointComponent>()) joint(e.GetComponent<FixedJointComponent>(), nullptr);
            if (e.HasComponent<SpringJointComponent>()) joint(e.GetComponent<SpringJointComponent>(), nullptr);
            if (e.HasComponent<BallJointComponent>()) joint(e.GetComponent<BallJointComponent>(), nullptr);
            if (e.HasComponent<ConfigurableJointComponent>())
            {
                auto& cj = e.GetComponent<ConfigurableJointComponent>();
                joint(cj, &cj.Axis);
                // İkincil eksen (Y) daha kısa çizgi
                if (glm::length(cj.SecondaryAxis) > 1e-6f)
                {
                    glm::vec3 anchor = glm::vec3(world * glm::vec4(cj.Anchor, 1.0f));
                    glm::vec3 ay = glm::normalize(glm::mat3(rotOnly) * cj.SecondaryAxis);
                    DebugDraw::Line(anchor, anchor + ay * 0.4f, glm::vec4(0.45f, 0.95f, 0.45f, 1.0f), 0.0f, false);
                }
            }
            // Wheel Collider: dinlenme konumundaki tekerlek dairesi + süspansiyon kursu
            if (e.HasComponent<WheelColliderComponent>())
            {
                const auto& w = e.GetComponent<WheelColliderComponent>();
                const glm::vec4 kWheel{ 0.45f, 0.95f, 0.55f, 1.0f };
                glm::vec3 top = glm::vec3(world * glm::vec4(w.Center, 1.0f));
                glm::vec3 up = glm::normalize(glm::mat3(rotOnly) * glm::vec3(0, 1, 0));
                glm::vec3 fwd = glm::normalize(glm::mat3(rotOnly) * glm::vec3(0, 0, -1));
                const float travel = std::max(w.SuspensionDistance, 0.0f);
                glm::vec3 center = top - up * (travel * (1.0f - std::clamp(w.Compression, 0.0f, 1.0f)));
                DebugDraw::Line(top, top - up * travel, kWheel, 0.0f, false);
                const int segs = 24;
                for (int i = 0; i < segs; ++i)
                {
                    float a0 = 6.2831853f * i / segs, a1 = 6.2831853f * (i + 1) / segs;
                    glm::vec3 p0 = center + (up * std::cos(a0) + fwd * std::sin(a0)) * w.Radius;
                    glm::vec3 p1 = center + (up * std::cos(a1) + fwd * std::sin(a1)) * w.Radius;
                    DebugDraw::Line(p0, p1, kWheel, 0.0f, false);
                }
                DebugDraw::Line(center, center - up * w.Radius, kWheel, 0.0f, false);
            }
        };
        if (all || m_EditorViewOptions.AllColliders)
        {
            auto view = m_Registry.view<TransformComponent>();
            for (auto h : view)
                draw(h);
        }
        else
        {
            for (uint32_t id : m_EditorSelection)
            {
                draw((entt::entity)id);
                // Rigidbody seçiliyse bileşik çocuk çarpıştırıcılar da
                Entity e{ (entt::entity)id, this };
                if (m_Registry.valid((entt::entity)id) && e.HasComponent<RigidbodyComponent>())
                    for (Entity child : e.GetChildren())
                        if (!child.HasComponent<RigidbodyComponent>())
                            draw((entt::entity)child);
            }
        }
        // Eğriler (Spline Container): seçiliyse düğümlerle, değilse "Show Gizmo" açıksa soluk
        for (auto h : m_Registry.view<SplineContainerComponent>())
            Splines::DrawGizmo(*this, (uint32_t)h, std::find(m_EditorSelection.begin(), m_EditorSelection.end(), (uint32_t)h) != m_EditorSelection.end());
        // Post Process Volume sınırları (seçiliyken)
        for (uint32_t id : m_EditorSelection)
        {
            PostProcessVolumes::DrawGizmo(*this, id);
            Decals::DrawGizmo(*this, id);
        }
    }

    void Scene::OnUpdateEditor([[maybe_unused]] Timestep ts, EditorCamera& camera)
    {
        OnUpdateEditor(ts, camera, nullptr);
    }

    // Işık / kamera simgeleri (Unity / Unreal gibi): ekranda sabit boyda (~34 piksel), kameraya dönük düz simge, ışık renginde,
    // her şeyin üstünde. Unity'deki gibi işlenmiş son görüntünün üstüne çizilir (TAA, ton eşleme, bloom sonrası): sahneyle
    // birlikte TAA'ya girince derinliğe yazmayan simge arkasındaki yüzeyin parçası sanılıyor, kamera yaklaşıp uzaklaşınca ya da
    // dönünce eski boyutları kırpılmadan üst üste kalıp silikleşiyor, titreyerek kat kat büyüyordu. Gizli nesnelerinki çizilmez
    void Scene::RenderEditorIcons(EditorCamera& camera, const glm::vec2* viewportBounds)
    {
        if (!m_EditorViewOptions.Gizmos)
            return;
        const glm::vec3 eye = camera.GetPosition();
        const glm::vec3 camRight = camera.GetRightDirection();
        const glm::vec3 camUp = camera.GetUpDirection();
        const glm::mat4 viewProjection = camera.GetViewProjection();
        const float gizmoVpH = viewportBounds ? std::max(1.0f, viewportBounds[1].y - viewportBounds[0].y) : (float)std::max(m_ViewportHeight, 1u);
        const float pixelWorld = 2.0f * std::tan(glm::radians(camera.GetFOV()) * 0.5f) / gizmoVpH;
        auto iconSize = [&](const glm::vec3& p) {
            return 34.0f * pixelWorld * std::max(glm::distance(eye, p), 0.05f);
        };
        auto selected = [&](entt::entity e) {
            return std::find(m_EditorSelection.begin(), m_EditorSelection.end(), (uint32_t)e) != m_EditorSelection.end();
        };
        auto icon = [&](const char* path, entt::entity e, const glm::vec3& color) {
            const glm::vec3 p = glm::vec3(m_Registry.get<TransformComponent>(e).WorldTransform[3]);
            // Renk çok koyuysa simge görünsün diye açılır; seçiliyken tam parlak
            glm::vec3 c = glm::max(color, glm::vec3(0.0f));
            const float m = std::max({ c.r, c.g, c.b, 1e-3f });
            c = glm::mix(glm::vec3(1.0f), c / m, 0.75f);
            Renderer3D::DrawGizmoIconOverlay(path, p, camRight, camUp, iconSize(p), c, selected(e) ? 1.0f : 0.85f, viewProjection);
        };
        for (auto e : m_Registry.view<TransformComponent, PointLightComponent>())
            if (IsEntityActive(e))
                icon("Resources/Icons/Gizmos/light_point.png", e, m_Registry.get<PointLightComponent>(e).Color);
        for (auto e : m_Registry.view<TransformComponent, SpotLightComponent>())
            if (IsEntityActive(e))
                icon("Resources/Icons/Gizmos/light_spot.png", e, m_Registry.get<SpotLightComponent>(e).Color);
        for (auto e : m_Registry.view<TransformComponent, DirectionalLightComponent>())
            if (IsEntityActive(e))
                icon("Resources/Icons/Gizmos/light_directional.png", e, m_Registry.get<DirectionalLightComponent>(e).Color);
        for (auto e : m_Registry.view<TransformComponent, AreaLightComponent>())
            if (IsEntityActive(e))
                icon("Resources/Icons/Gizmos/light_area.png", e, m_Registry.get<AreaLightComponent>(e).Color);
        for (auto e : m_Registry.view<TransformComponent, CameraComponent>())
            if (IsEntityActive(e))
                icon("Resources/Icons/Gizmos/camera.png", e, m_Registry.get<CameraComponent>(e).Primary ? glm::vec3(1.0f, 0.95f, 0.55f) : glm::vec3(1.0f));
    }

    void Scene::OnUpdateEditor([[maybe_unused]] Timestep ts, EditorCamera& camera, const glm::vec2* viewportBounds)
    {
        // Stats'ı sıfırla
        Renderer::ResetStats();
        UpdateEditorSystems(ts);
        // Instantiate GameObject Runtime Only kapalı karoların düzenleyici önizlemeleri (harita değişince)
        GridSystem::SyncTileObjects(*this, false);

        // Buton input sistemi
        UpdateButtonInput(camera, ts, viewportBounds);

        // UI (RectTransform) yerleşimi: sahne görünümünde seçim/gizmo için dünya dönüşümleri
        UISystem::UpdateEditorTransforms(*this, m_UIScreenSize);

        RenderEditorView(ts, camera, viewportBounds);
    }

    void Scene::UpdateEditorSystems([[maybe_unused]] Timestep ts)
    {
        // Kumaşların düz dinlenme hali (oyunda Jolt yumuşak gövdesi sürer)
        if (!m_IsRuntimeScene)
            ClothMesh::EnsureRestMeshes(*this);
        
        // Transform hierarchy'yi güncelle (root entity'lerden başla)
        auto view = m_Registry.view<TransformComponent>();
        for (auto entity : view)
        {
            Entity e{ entity, this };
            
            // Sadece root entity'ler için (parent'ı olmayan)
            if (!GetEntityParent(e))
            {
                UpdateTransformHierarchy(e, glm::mat4(1.0f));
            }
        }

        // Düzenleyicide de çalışan bileşen sistemleri (kısıtlar, RunInEditor döndürücüler)
        if (!m_IsRuntimeScene)
        {
            GameplaySystems::Update(*this, (float)ts, false);
            GodotNodes::Update(*this, (float)ts, false);
            Skeleton2D::Update(*this, false);
            SpriteAnimation2D::Update(*this);
            SpriteShapes::Update(*this);
            if (!m_IsRuntimeScene)
                ActorComponents::Update(*this, (float)ts, false);   // Cable sarkması, Cine Camera görüş açısı
        }
        // Auto LOD / Virtual Geometry: bekleyen arka plan yapımları (düzenleyicide de)
        AutoLOD::Update(*this);
        VirtualGeometry::Update();
        CharacterCustomizer::Update(*this);
        CSG::Update(*this);
        MultiMesh::Update(*this);

        // Seçili parçacık sistemlerinin editör önizlemesi (oyun modunda çalışma zamanı günceller)
        if (!m_IsRuntimeScene)
        {
            if (m_EditorViewOptions.Realtime)
                m_EditorClock += ts.GetSeconds();
            UpdateParticles(ts.GetSeconds(), true);
            // Karakterler bekleme pozunda (Realtime açıkken bekleme animasyonu oynar)
            UpdateEditorAnimationPreview(m_EditorViewOptions.Realtime ? ts.GetSeconds() : 0.0f);
        }

        UpdateAudioSystem(ts);
    }

    void Scene::RenderEditorView(Timestep ts, EditorCamera& camera, const glm::vec2* viewportBounds)
    {
        // 3D sahne
        {
            const uint32_t vpW = viewportBounds ? (uint32_t)std::max(1.0f, viewportBounds[1].x - viewportBounds[0].x) : std::max(m_ViewportWidth, 1u);
            const uint32_t vpH = viewportBounds ? (uint32_t)std::max(1.0f, viewportBounds[1].y - viewportBounds[0].y) : std::max(m_ViewportHeight, 1u);
            m_EditorView2D = camera.Is2D();
            {
                CQ_PROFILE_SAMPLE("Render.CameraTargets", Rendering);
                CameraRenderTargets::Render(*this);
            }
            {
                CQ_PROFILE_SAMPLE("Render.SceneEditor", Rendering);
                RenderScene3D(RenderCamera::FromEditorCamera(camera, vpW, vpH), true);
            }

            // 2D görünüm: ızgara arka plan katmanıdır (Unity gibi); sprite ve modeller hep önünde kalır
            if (camera.Is2D() && m_EditorViewOptions.Grid)
                EditorGrid::Render(RenderCamera::FromEditorCamera(camera, vpW, vpH), true, true, true);
        }

        // Işık / kamera simgeleri (Unity / Unreal gibi): ekranda sabit boyda (~34 piksel), kameraya dönük düz simge, ışık
        // renginde; her şeyin üstünde çizilir. Gizli (etkin olmayan) nesnelerinkiler çizilmez. Seçiliyken menzil / koni /
        // görüş alanı tel çizgileri (DrawSelectionGizmos benzeri, aşağıda).
        if (m_EditorViewOptions.Gizmos)
        {
            Renderer3D::BeginScene(camera);
            const glm::vec3 eye = camera.GetPosition();
            const glm::vec3 camRight = camera.GetRightDirection();
            const float gizmoVpH = viewportBounds ? std::max(1.0f, viewportBounds[1].y - viewportBounds[0].y) : (float)std::max(m_ViewportHeight, 1u);
            const float pixelWorld = 2.0f * std::tan(glm::radians(camera.GetFOV()) * 0.5f) / gizmoVpH;
            auto iconSize = [&](const glm::vec3& p) {
                return 34.0f * pixelWorld * std::max(glm::distance(eye, p), 0.05f);
            };
            // Simgelerin kendisi burada çizilmez: RenderEditorIcons (işlenmiş son görüntünün üstüne, TAA'dan sonra)

            // Seçili ışıkların / kameraların tel şekilleri
            for (uint32_t id : m_EditorSelection)
            {
                const entt::entity e = (entt::entity)id;
                if (!m_Registry.valid(e) || !IsEntityActive(e))
                    continue;
                const glm::mat4& world = m_Registry.get<TransformComponent>(e).WorldTransform;
                const glm::vec3 p = glm::vec3(world[3]);
                if (auto* pl = m_Registry.try_get<PointLightComponent>(e))
                    DebugDraw::Sphere(p, pl->Range, glm::vec4(pl->Color, 0.9f));
                if (auto* sl = m_Registry.try_get<SpotLightComponent>(e))
                {
                    const glm::vec3 dir = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    const glm::vec3 ref = std::fabs(dir.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                    const glm::vec3 u = glm::normalize(glm::cross(dir, ref)), v = glm::cross(u, dir);
                    const float radius = sl->Range * std::tan(glm::radians(std::clamp(sl->OuterConeAngle, 1.0f, 89.0f)));
                    const glm::vec3 c = p + dir * sl->Range;
                    const glm::vec4 col(sl->Color, 0.9f);
                    glm::vec3 prev = c + u * radius;
                    for (int k = 1; k <= 24; ++k)
                    {
                        const float a = 6.2831853f * (float)k / 24.0f;
                        const glm::vec3 q = c + (u * std::cos(a) + v * std::sin(a)) * radius;
                        DebugDraw::Line(prev, q, col);
                        if (k % 6 == 0) DebugDraw::Line(p, q, col);
                        prev = q;
                    }
                }
                if (auto* dl = m_Registry.try_get<DirectionalLightComponent>(e))
                {
                    const glm::vec3 dir = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    const float len = iconSize(p) * 4.0f;
                    for (int k = -1; k <= 1; ++k)
                    {
                        const glm::vec3 o = p + camRight * (float)k * iconSize(p) * 0.35f;
                        DebugDraw::Line(o, o + dir * len, glm::vec4(dl->Color, 0.9f));
                    }
                }
                if (m_Registry.all_of<CameraComponent>(e))
                {
                    // Görüş alanı (3 m'ye kadar)
                    auto& cc = m_Registry.get<CameraComponent>(e);
                    const glm::vec3 fwd = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    const glm::vec3 rgt = glm::normalize(glm::vec3(world * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
                    const glm::vec3 upv = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
                    const float d = 3.0f;
                    const float hh = d * std::tan(cc.Camera.GetPerspectiveVerticalFOV() * 0.5f);
                    const float hw = hh * 16.0f / 9.0f;
                    const glm::vec3 c = p + fwd * d;
                    const glm::vec3 corners[4] = { c + rgt * hw + upv * hh, c - rgt * hw + upv * hh, c - rgt * hw - upv * hh, c + rgt * hw - upv * hh };
                    const glm::vec4 col(1.0f, 1.0f, 1.0f, 0.8f);
                    for (int k = 0; k < 4; ++k)
                    {
                        DebugDraw::Line(p, corners[k], col);
                        DebugDraw::Line(corners[k], corners[(k + 1) % 4], col);
                    }
                }
            }

            Renderer3D::EndScene();
        }

        // 2D Rendering - Depth test AÇIK, depth write AÇIK
        const glm::mat4 viewProjection2D = camera.GetViewProjection();
        const bool lit2D = m_EditorViewOptions.Sprites2D && Lighting2D::BeginPass(*this, viewProjection2D);
        Renderer2D::BeginScene(camera, true, true); // Depth test enabled, depth write enabled
        TextRenderer::BeginScene(camera, true); // Depth test enabled

        // Tüm render edilebilir entity'leri topla (sprite + text + karo haritası)
        struct RenderItem
        {
            enum class Type { Sprite, Text, Tilemap, SpriteShape };
            Type ItemType;
            entt::entity Entity;
            TransformComponent* Transform;
            void* Component; // SpriteRendererComponent* ya da TextRendererComponent*
            int32_t Layer;
            int32_t OrderInLayer;
            float ZPosition; // Z-depth sorting için
        };

        std::vector<RenderItem> renderItems;

        // Sprite'ları topla
        auto spriteGroup = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
        for (auto entity : spriteGroup)
        {
            auto [transform, sprite] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entity);
            if (!IsEntityActive(entity))
                continue;

            int32_t layer = 0;
            int32_t orderInLayer = 0;
            if (m_Registry.all_of<LayerComponent>(entity))
            {
                auto& layerComp = m_Registry.get<LayerComponent>(entity);
                layer = layerComp.Layer;
                orderInLayer = layerComp.OrderInLayer;
            }

            renderItems.push_back({ 
                RenderItem::Type::Sprite, 
                entity, 
                &transform, 
                (void*)&sprite, 
                layer, 
                orderInLayer,
                transform.WorldTransform[3][2] // Z pozisyonu
            });
        }


        // Text'leri topla
        auto textView = m_Registry.view<TransformComponent, TextRendererComponent>();
        for (auto entity : textView)
        {
            auto [transform, text] = textView.get<TransformComponent, TextRendererComponent>(entity);
            if (!IsEntityActive(entity))
                continue;

            int32_t layer = 5; // Default UI layer
            int32_t orderInLayer = 0;
            if (m_Registry.all_of<LayerComponent>(entity))
            {
                auto& layerComp = m_Registry.get<LayerComponent>(entity);
                layer = layerComp.Layer;
                orderInLayer = layerComp.OrderInLayer;
            }

            renderItems.push_back({ 
                RenderItem::Type::Text, 
                entity, 
                &transform, 
                (void*)&text, 
                layer, 
                orderInLayer,
                transform.WorldTransform[3][2] // Z pozisyonu
            });
        }

        // Karo haritaları (2D)
        {
            auto tilemapView = m_Registry.view<TransformComponent, TilemapComponent>();
            for (auto entity : tilemapView)
            {
                auto [transform, tilemap] = tilemapView.get<TransformComponent, TilemapComponent>(entity);
                if (!IsEntityActive(entity) || tilemap.Render3D)
                    continue;
                int32_t layer = m_Registry.all_of<LayerComponent>(entity) ? m_Registry.get<LayerComponent>(entity).Layer : 0;
                renderItems.push_back({ RenderItem::Type::Tilemap, entity, &transform, (void*)&tilemap, layer, tilemap.OrderInLayer, transform.WorldTransform[3][2] });
            }
            // Sprite Shape'ler
            for (auto entity : m_Registry.view<TransformComponent, SpriteShapeRendererComponent, SpriteShapeControllerComponent>())
            {
                if (!IsEntityActive(entity))
                    continue;
                auto& transform = m_Registry.get<TransformComponent>(entity);
                int32_t layer = 0, order = 0;
                if (const auto* lc = m_Registry.try_get<LayerComponent>(entity)) { layer = lc->Layer; order = lc->OrderInLayer; }
                renderItems.push_back({ RenderItem::Type::SpriteShape, entity, &transform, nullptr, layer, order, transform.WorldTransform[3][2] });
            }
        }

        if (!m_EditorViewOptions.Sprites2D)
            renderItems.clear();

        // Layer ve OrderInLayer'a göre sırala
        std::sort(renderItems.begin(), renderItems.end(), 
            [this](const RenderItem& a, const RenderItem& b) {
                // Canvas Sort Order kontrolü
                int32_t aSortOrder = 0;
                int32_t bSortOrder = 0;
                
                Entity aEntity{ a.Entity, this };
                Entity bEntity{ b.Entity, this };
                
                // A için Canvas Sort Order
                if (IsCanvasOrCanvasChild(aEntity))
                {
                    Entity canvasA = aEntity;
                    while (canvasA && !canvasA.HasComponent<CanvasComponent>())
                    {
                        canvasA = GetEntityParent(canvasA);
                    }
                    if (canvasA && canvasA.HasComponent<CanvasComponent>())
                    {
                        aSortOrder = canvasA.GetComponent<CanvasComponent>().SortOrder;
                    }
                }
                
                // B için Canvas Sort Order
                if (IsCanvasOrCanvasChild(bEntity))
                {
                    Entity canvasB = bEntity;
                    while (canvasB && !canvasB.HasComponent<CanvasComponent>())
                    {
                        canvasB = GetEntityParent(canvasB);
                    }
                    if (canvasB && canvasB.HasComponent<CanvasComponent>())
                    {
                        bSortOrder = canvasB.GetComponent<CanvasComponent>().SortOrder;
                    }
                }
                
                // Canvas Sort Order farklıysa ona göre sırala
                if (aSortOrder != bSortOrder)
                    return aSortOrder < bSortOrder;
                
                // Canvas Sort Order aynıysa Layer'a göre
                if (a.Layer != b.Layer)
                    return a.Layer < b.Layer;
                
                // Layer aynıysa OrderInLayer'a göre
                if (a.OrderInLayer != b.OrderInLayer)
                    return a.OrderInLayer < b.OrderInLayer;
                
                // OrderInLayer da aynıysa Z pozisyonuna göre (back-to-front, uzak objeler önce)
                return a.ZPosition < b.ZPosition;
            });

        // TÜM objeleri render et (Canvas kontrolü ile depth test)
        // Geçiş durumu yalnızca değişince yeniden kurulur (sprite başına draw call olmasın)
        bool currentDepth = true, lastWasText = false;
        for (auto& item : renderItems)
        {
            Entity itemEntity{ item.Entity, this };
            bool isCanvasChild = IsCanvasOrCanvasChild(itemEntity);
            
            // Canvas child ama World Space ise depth test AÇIK
            bool depthTestEnabled = true;
            if (isCanvasChild)
            {
                // Canvas'ın Render Mode'unu kontrol et
                Entity canvasEntity = itemEntity;
                while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                {
                    canvasEntity = GetEntityParent(canvasEntity);
                }
                
                if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                {
                    auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                    if (canvas.Mode == CanvasComponent::RenderMode::WorldSpace)
                    {
                        depthTestEnabled = true; // World Space: depth test AÇIK
                    }
                    else
                    {
                        depthTestEnabled = false; // Overlay/Camera: depth test KAPALI
                    }
                }
            }
            
            const bool isText = item.ItemType == RenderItem::Type::Text;
            if (depthTestEnabled != currentDepth || isText != lastWasText)
            {
                Renderer2D::EndScene();
                TextRenderer::EndScene();
                Renderer2D::BeginScene(camera, depthTestEnabled);
                TextRenderer::BeginScene(camera, depthTestEnabled);
                currentDepth = depthTestEnabled;
            }
            lastWasText = isText;
            Renderer2D::SetLit(lit2D && !isCanvasChild && (item.ItemType == RenderItem::Type::Sprite || item.ItemType == RenderItem::Type::Tilemap || item.ItemType == RenderItem::Type::SpriteShape));
            Renderer2D::SetMaskInteraction(item.ItemType == RenderItem::Type::Sprite ? (int)((SpriteRendererComponent*)item.Component)->MaskInteraction : 0);
            
            // Pixel Perfect: Canvas child ise ve Pixel Perfect açıksa pozisyonu yuvarla
            glm::mat4 finalTransform = item.Transform->WorldTransform;
            if (isCanvasChild)
            {
                Entity canvasEntity = itemEntity;
                while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                {
                    canvasEntity = GetEntityParent(canvasEntity);
                }
                
                if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                {
                    auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                    
                    // UI Scale Mode: Canvas Scaler varsa scale uygula
                    if (canvasEntity.HasComponent<CanvasScalerComponent>())
                    {
                        auto& scaler = canvasEntity.GetComponent<CanvasScalerComponent>();
                        
                        // Reference Pixels Per Unit: Sprite boyutunu dünya birimine çevir
                        float pixelToWorldScale = 100.0f / scaler.ReferencePixelsPerUnit;
                        
                        if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPixelSize)
                        {
                            // Scale Factor uygula
                            glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                            scale *= scaler.ScaleFactor * pixelToWorldScale;
                            finalTransform[0][0] = scale.x;
                            finalTransform[1][1] = scale.y;
                            finalTransform[2][2] = scale.z;
                        }
                        else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize)
                        {
                            // Viewport boyutuna göre scale
                            float screenScale = (float)m_ViewportWidth / 1920.0f; // Reference: 1920x1080
                            screenScale *= scaler.ScaleFactor * pixelToWorldScale;
                            
                            glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                            scale *= screenScale;
                            finalTransform[0][0] = scale.x;
                            finalTransform[1][1] = scale.y;
                            finalTransform[2][2] = scale.z;
                        }
                        else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPhysicalSize)
                        {
                            // DPI bazlı scale (96 DPI referans)
                            float dpi = 96.0f; // TODO: Platform'dan gerçek DPI al
                            float dpiScale = dpi / 96.0f;
                            dpiScale *= scaler.ScaleFactor * pixelToWorldScale;
                            
                            glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                            scale *= dpiScale;
                            finalTransform[0][0] = scale.x;
                            finalTransform[1][1] = scale.y;
                            finalTransform[2][2] = scale.z;
                        }
                    }
                    
                    // Pixel Perfect
                    if (canvas.PixelPerfect)
                    {
                        // Pozisyonu tam piksele yuvarla
                        finalTransform[3][0] = glm::round(finalTransform[3][0]);
                        finalTransform[3][1] = glm::round(finalTransform[3][1]);
                    }
                }
                
                if (isCanvasChild)
                {
                    // Ekranda 10 kat büyük görünsün (Transform değeri değişmeden)
                    for (int i = 0; i < 3; i++) {
                        finalTransform[i][0] *= 10.0f;
                        finalTransform[i][1] *= 10.0f;
                        finalTransform[i][2] *= 10.0f;
                    }
                }
            }
            
            if (item.ItemType == RenderItem::Type::Tilemap)
            {
                GridSystem::DrawTilemap2D(*this, itemEntity);
            }
            else if (item.ItemType == RenderItem::Type::SpriteShape)
            {
                SpriteShapes::Draw(*this, item.Entity);
            }
            else if (item.ItemType == RenderItem::Type::Sprite)
            {
                auto* sprite = (SpriteRendererComponent*)item.Component;
                // Eski (Legacy) düğme: arka planı Sprite Renderer; renk düğme durumundan, isteğe bağlı 9 dilim
                if (m_Registry.all_of<ButtonComponent>(item.Entity) && sprite->Texture)
                {
                    auto& button = m_Registry.get<ButtonComponent>(item.Entity);
                    if (button.Use9Slice)
                        Renderer2D::DrawSprite9Slice(finalTransform, sprite->Texture, button.BorderSize, button.CurrentBackgroundColor, *sprite);
                    else
                        Renderer2D::DrawSprite(finalTransform, sprite->Texture, sprite->TilingFactor, button.CurrentBackgroundColor, *sprite);
                }
                // Sprite Skin: kemiklerle bükülmüş ağ (bağlıysa)
                else if (!SpriteAnimation2D::DrawSkinned(*this, item.Entity, *sprite, sprite->Color))
                    Renderer2D::DrawSprite(finalTransform, sprite->Texture, sprite->TilingFactor, sprite->Color, *sprite);
            }
            else if (item.ItemType == RenderItem::Type::Text)
            {
                auto* text = (TextRendererComponent*)item.Component;
                glm::vec4 finalTextColor = text->Color;
                glm::mat4 textTransform = finalTransform;
                
                Entity textEntity{ item.Entity, this };
                Entity parent = GetEntityParent(textEntity);
                
                if (parent && parent.HasComponent<ButtonComponent>() && parent.HasComponent<SpriteRendererComponent>())
                {
                    auto& button = parent.GetComponent<ButtonComponent>();
                    auto& parentImage = parent.GetComponent<SpriteRendererComponent>();
                    
                    finalTextColor = button.CurrentTextColor;
                    
                    if (parentImage.Texture)
                    {
                        float halfWidth = (float)parentImage.Texture->GetWidth() / 200.0f;
                        float halfHeight = (float)parentImage.Texture->GetHeight() / 200.0f;
                        
                        float offsetMultiplierX = 0.93f;
                        float offsetMultiplierY = 0.5f;
                        glm::vec2 offset = glm::vec2(-halfWidth * offsetMultiplierX, -halfHeight * offsetMultiplierY);
                        
                        textTransform[3][0] += offset.x;
                        textTransform[3][1] += offset.y;
                    }
                }
                
                TextRenderer::DrawText(text->AutoTranslate ? Localization::Translate(text->Text) : text->Text, textTransform, 
                                       text->FontSize / 100.0f, finalTextColor, (int)text->Alignment);
            }
        }
        Renderer2D::EndScene();
        TextRenderer::EndScene();
        Lighting2D::EndPass(*this, viewProjection2D);
        Renderer2D::BeginScene(camera, true);
        TextRenderer::BeginScene(camera, true);

        DebugDraw::SetCameraPosition(camera.GetPosition());
        DebugDraw::Update(ts);

        // Sonsuz, çok seviyeli editör ızgarası (shader tabanlı)
        {
            const uint32_t vpW = viewportBounds ? (uint32_t)std::max(1.0f, viewportBounds[1].x - viewportBounds[0].x) : std::max(m_ViewportWidth, 1u);
            const uint32_t vpH = viewportBounds ? (uint32_t)std::max(1.0f, viewportBounds[1].y - viewportBounds[0].y) : std::max(m_ViewportHeight, 1u);
            if (m_EditorViewOptions.Grid && !camera.Is2D())
                EditorGrid::Render(RenderCamera::FromEditorCamera(camera, vpW, vpH), false, true);

            // Ekran üstü canvas'lar sahne görünümünde de görünür (Unity sahne görünümü gibi: seçim çerçevesi ile içerik
            // birlikte). 2D'de ızgaranın üstünde; 3D'de dünya içinde derinlik testiyle (nesnelerin arkasında kalan kısım gizlenir)
            if (m_EditorViewOptions.UI)
            {
                Renderer2D::Flush();
                {
                    CQ_PROFILE_SAMPLE("Render.EditorCanvases", UI);
                    UISystem::RenderEditorOverlayCanvases(*this, camera.GetViewProjection(), m_UIScreenSize, !camera.Is2D());
                }
                Renderer2D::BeginScene(camera, true);
            }
        }

        if (m_EditorViewOptions.Colliders || m_EditorViewOptions.AllColliders)
        {
            CQ_PROFILE_SAMPLE("Render.PhysicsGizmos", Rendering);
            DrawPhysicsGizmos();
        }
        GridSystem::DrawEditorGizmos(*this, m_EditorSelection);
        Skeleton2D::DrawEditorGizmos(*this, m_EditorSelection);
        // Occlusion Culling görselleştirme: pişirilmiş hücre ızgarası ve kameranın hücresi
        if (m_OcclusionVisualize)
            OcclusionCulling::DrawCells(*this, camera.GetPosition());
        WorldActors::DrawEditorGizmos(*this, m_EditorSelection);
        Navigation2D::DrawEditorGizmos(*this, m_EditorSelection);
        for (uint32_t id : m_EditorSelection)
        {
            Lighting2D::DrawGizmo(*this, id);
            AreaLights::DrawGizmo(*this, id);
            LightProbeGroups::DrawGizmo(*this, id);
            SpriteShapes::DrawGizmo(*this, id);
            OcclusionCulling::DrawGizmo(*this, id);
            WaterBodies::DrawGizmo(*this, id);
            WorldPartition::DrawGizmo(*this, id);
            // Gezinme: Link uçları ve yayı, Modifier Volume / Obstacle kutusu
            entt::entity e = (entt::entity)id;
            if (!m_Registry.valid(e) || !m_Registry.all_of<TransformComponent>(e))
                continue;
            const glm::mat4& w = m_Registry.get<TransformComponent>(e).WorldTransform;
            if (auto* link = m_Registry.try_get<NavMeshLinkComponent>(e))
            {
                const glm::vec3 a = glm::vec3(w * glm::vec4(link->StartPoint, 1.0f)), b = glm::vec3(w * glm::vec4(link->EndPoint, 1.0f));
                const glm::vec4 col(1.0f, 0.8f, 0.2f, 1.0f);
                glm::vec3 prev = a;
                for (int k = 1; k <= 16; ++k)
                {
                    float t = (float)k / 16.0f;
                    glm::vec3 p = glm::mix(a, b, t) + glm::vec3(0.0f, 4.0f * std::max(0.3f, glm::length(b - a) * 0.25f) * t * (1.0f - t), 0.0f);
                    DebugDraw::Line(prev, p, col, 0.0f, false);
                    prev = p;
                }
                DebugDraw::Sphere(a, 0.12f, col, 0.0f, false);
                DebugDraw::Sphere(b, 0.12f, col, 0.0f, false);
            }
            if (auto* vol = m_Registry.try_get<NavMeshModifierVolumeComponent>(e))
                DebugDraw::Box(w * glm::translate(glm::mat4(1.0f), vol->Center) * glm::scale(glm::mat4(1.0f), glm::abs(vol->Size)), glm::vec4(0.3f, 0.9f, 0.5f, 1.0f), 0.0f, false);
            if (auto* ob = m_Registry.try_get<NavMeshObstacleComponent>(e))
                DebugDraw::Box(w * glm::translate(glm::mat4(1.0f), ob->Center) * glm::scale(glm::mat4(1.0f), glm::abs(ob->Size)), glm::vec4(0.95f, 0.35f, 0.3f, 1.0f), 0.0f, false);
        }
        if (m_ShowNavMesh)
            NavAgents::DrawDebug(m_NavMesh, camera.GetViewProjection());
        {
            CQ_PROFILE_SAMPLE("Render.DebugDraw", Rendering);
            DebugDraw::Render(camera.GetViewProjection());
        }
        {
            CQ_PROFILE_SAMPLE("Render.2DText", Rendering);
            Renderer2D::EndScene();
            TextRenderer::EndScene();
        }
        DebugMetrics::SyncToRendererStats(Renderer::GetStats());
        DebugDraw::Clear();
    }

    // ===================================================================== 3D render

    void Scene::UpdateWorldTransforms()
    {
        auto view = m_Registry.view<TransformComponent>();
        for (auto entity : view)
        {
            Entity e{ entity, this };
            if (!GetEntityParent(e))
                UpdateTransformHierarchy(e, glm::mat4(1.0f));
        }
    }

    bool Scene::HasRenderable3D() const
    {
        auto& reg = const_cast<entt::registry&>(m_Registry);
        return !reg.view<MeshRendererComponent>().empty() || !reg.view<TerrainComponent>().empty();
    }

    SceneEnvironment Scene::BuildRenderEnvironment(bool hdrOutput) const
    {
        SceneEnvironment env;
        env.AmbientSource = (int)m_EnvironmentLightingSource;
        env.AmbientColor = m_AmbientColor;
        env.SkyColor = m_AmbientSkyColor;
        env.EquatorColor = m_AmbientEquatorColor;
        env.GroundColor = m_AmbientGroundColor;
        env.AmbientIntensity = m_AmbientIntensity;
        env.Skybox = m_Skybox;
        env.SkyboxExposure = m_SkyboxExposure;
        env.SkyboxRotation = m_SkyboxRotation;
        env.SkyboxTint = m_SkyboxTint;
        env.Settings = m_RenderSettings;
        ConsoleSystem::ApplyRenderOverrides(env.Settings);   // r.* konsol değişkenleri (oturum katmanı)
        // Ortam aktörleri (Exponential Height Fog, Sky Atmosphere, Volumetric Cloud, Sky Light) Lighting ayarlarının yerine geçer
        WorldActors::ApplyEnvironment(*this, env);
        ActorComponents::ApplyEnvironment(*this, env);   // Cine Camera: alan derinliği
        env.Lightmap = m_BakedLightmapTexture;
        env.APV = m_APV;
        env.HDROutput = hdrOutput;
        // Düzenleyicide malzeme/gökyüzü zamanı Realtime açıkken akar (oyunda her zaman gerçek zaman)
        env.Time = m_IsRuntimeScene ? TimeManager::GetTime() : m_EditorClock;

        // Gökyüzü ortam aydınlatmasının kaynağıdır. Arka planda çizilip çizilmeyeceğine sahne içeriği değil,
        // editörde 2D/3D düğmesi, oyunda kameranın Background ayarı karar verir (eskiden yalnızca sprite olan
        // sahneler kendiliğinden "2D görünüme" geçiyordu).
        env.UseProceduralSky = env.Settings.Environment.Sky != SkyMode::SolidColor;
        // Düz renk: gökyüzü yok; Sky Atmosphere: skybox yerine fiziksel gökyüzü (güneşi izler)
        if (env.Settings.Environment.Sky == SkyMode::SolidColor || env.Settings.Environment.Sky == SkyMode::Atmosphere)
            env.Skybox = nullptr;
        return env;
    }

    Scene::CameraBackground Scene::ResolveCameraBackground(Entity cameraEntity)
    {
        CameraBackground bg;
        bg.Color = m_RenderSettings.Environment.BackgroundColor;
        if (m_RenderSettings.Environment.Sky == SkyMode::SolidColor)
        {
            bg.DrawSky = false;
            return bg;
        }
        if (!cameraEntity || !m_Registry.valid((entt::entity)cameraEntity) || !cameraEntity.HasComponent<CameraComponent>())
            return bg;
        const auto& cc = cameraEntity.GetComponent<CameraComponent>();
        switch (cc.Background)
        {
            case CameraComponent::BackgroundMode::Skybox: bg.DrawSky = true; break;
            case CameraComponent::BackgroundMode::SolidColor: bg.DrawSky = false; break;
            default: bg.DrawSky = cc.Camera.GetProjectionType() != SceneCamera::ProjectionType::Orthographic; break;
        }
        if (!bg.DrawSky)
            bg.Color = glm::vec3(cc.BackgroundColor);
        return bg;
    }

    bool Scene::GetRuntimeRenderCamera(uint32_t width, uint32_t height, RenderCamera& out)
    {
        Entity cameraEntity = GetPrimaryCameraEntity();
        if (!cameraEntity)
        {
            auto view = m_Registry.view<CameraComponent>();
            if (view.empty())
                return false;
            cameraEntity = Entity{ view.front(), this };
        }
        auto& cc = cameraEntity.GetComponent<CameraComponent>();
        auto& tc = cameraEntity.GetComponent<TransformComponent>();
        out = RenderCamera::FromSceneCamera(cc.Camera, tc.WorldTransform, width, height);
        return true;
    }

    void Scene::InvalidateReflectionProbes()
    {
        auto view = m_Registry.view<ReflectionProbeComponent>();
        for (auto e : view)
            view.get<ReflectionProbeComponent>(e).NeedsCapture = true;
    }

    void Scene::CaptureReflectionProbes()
    {
        auto view = m_Registry.view<TransformComponent, ReflectionProbeComponent>();
        if (view.begin() == view.end())
            return;

        const float dt = TimeManager::GetUnscaledDeltaTime();
        std::vector<entt::entity> pending;
        for (auto e : view)
        {
            auto& probe = view.get<ReflectionProbeComponent>(e);
            probe.TimeSinceCapture += dt;
            if (probe.RealtimeUpdate && probe.TimeSinceCapture >= std::max(probe.UpdateInterval, 0.0f))
                probe.NeedsCapture = true;
            if (probe.NeedsCapture && IsEntityActive(e))
                pending.push_back(e);
        }
        if (pending.empty())
            return;

        m_CapturingProbes = true;
        GLint prevFBO = 0;
        GLint prevViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        // Kare başına en fazla bir prob (gerçek zamanlı problar kareyi kilitlemesin)
        size_t budget = 1;
        for (auto e : pending)
        {
            if (budget-- == 0)
                break;
            auto [transform, probe] = view.get<TransformComponent, ReflectionProbeComponent>(e);
            const uint32_t res = std::clamp<uint32_t>(probe.Resolution, 16u, 1024u);
            const glm::vec3 pos = glm::vec3(transform.WorldTransform[3]);

            uint32_t cube = 0, depth = 0, fbo = 0;
            glGenTextures(1, &cube);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
            int mips = 1;
            for (uint32_t sz = res; sz > 1; sz >>= 1) ++mips;
            glTexStorage2D(GL_TEXTURE_CUBE_MAP, mips, GL_RGBA16F, (GLsizei)res, (GLsizei)res);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glGenRenderbuffers(1, &depth);
            glBindRenderbuffer(GL_RENDERBUFFER, depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)res, (GLsizei)res);
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);

            const glm::vec3 dirs[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
            const glm::vec3 ups[6] = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
            for (int face = 0; face < 6; ++face)
            {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube, 0);
                glViewport(0, 0, (GLsizei)res, (GLsizei)res);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                RenderCamera cam;
                cam.View = glm::lookAt(pos, pos + dirs[face], ups[face]);
                cam.Projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 500.0f);
                cam.Position = pos;
                cam.NearClip = 0.05f;
                cam.FarClip = 500.0f;
                cam.ViewportWidth = res;
                cam.ViewportHeight = res;
                RenderScene3D(cam, false);
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

            probe.Captured = EnvironmentMap::FromCubemap(cube, res);
            probe.NeedsCapture = false;
            probe.TimeSinceCapture = 0.0f;

            glDeleteFramebuffers(1, &fbo);
            glDeleteRenderbuffers(1, &depth);
            glDeleteTextures(1, &cube);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        m_CapturingProbes = false;
    }

    void Scene::CaptureSceneCubes()
    {
        auto view = m_Registry.view<TransformComponent, SceneCaptureCubeComponent>();
        if (view.begin() == view.end() || m_CapturingProbes)
            return;
        const float dt = TimeManager::GetUnscaledDeltaTime();
        for (auto e : view)
        {
            auto& capture = view.get<SceneCaptureCubeComponent>(e);
            capture.Timer += dt;
            const bool due = !capture.Captured || (capture.Interval > 0.0f && capture.Timer >= capture.Interval);
            if (!due || !IsEntityActive(e) || capture.Target.empty())
                continue;
            int size = 0;
            const uint32_t cube = ExtraTextures::Cube(capture.Target, &size);
            if (!cube || size <= 0)
                continue;
            capture.Captured = true;
            capture.Timer = 0.0f;
            m_CapturingProbes = true;
            GLint prevFBO = 0;
            GLint prevViewport[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
            glGetIntegerv(GL_VIEWPORT, prevViewport);
            uint32_t depth = 0, fbo = 0;
            glGenRenderbuffers(1, &depth);
            glBindRenderbuffer(GL_RENDERBUFFER, depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
            const glm::vec3 pos = glm::vec3(view.get<TransformComponent>(e).WorldTransform[3]);
            const glm::vec3 dirs[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
            const glm::vec3 ups[6] = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
            for (int face = 0; face < 6; ++face)
            {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube, 0);
                glViewport(0, 0, size, size);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                RenderCamera cam;
                cam.View = glm::lookAt(pos, pos + dirs[face], ups[face]);
                cam.Projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 500.0f);
                cam.Position = pos;
                cam.NearClip = 0.05f;
                cam.FarClip = 500.0f;
                cam.ViewportWidth = (uint32_t)size;
                cam.ViewportHeight = (uint32_t)size;
                RenderScene3D(cam, false);
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            glDeleteFramebuffers(1, &fbo);
            glDeleteRenderbuffers(1, &depth);
            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            m_CapturingProbes = false;
        }
    }

    void Scene::RenderSceneFromCamera(entt::entity cameraEntity, uint32_t width, uint32_t height)
    {
        if (!m_Registry.valid(cameraEntity) || !m_Registry.all_of<CameraComponent, TransformComponent>(cameraEntity))
            return;
        auto& cameraComponent = m_Registry.get<CameraComponent>(cameraEntity);
        cameraComponent.Camera.SetViewportSize(width, height);
        const RenderCamera renderCamera = RenderCamera::FromSceneCamera(cameraComponent.Camera,
            m_Registry.get<TransformComponent>(cameraEntity).WorldTransform, width, height);
        RenderSceneSecondary(renderCamera, cameraEntity);
    }

    void Scene::RenderSceneSecondary(const RenderCamera& camera, entt::entity backgroundCamera)
    {
        const entt::entity previous = m_BackgroundCamera;
        m_BackgroundCamera = backgroundCamera;
        const bool previousSecondary = s_GISecondaryView;
        s_GISecondaryView = true;
        RenderScene3D(camera, false);
        s_GISecondaryView = previousSecondary;
        m_BackgroundCamera = previous;
    }

    void Scene::RenderPlanarReflection(const RenderCamera& camera)
    {
        Renderer3D::SetPlanarReflection(0, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(0.0f));
        entt::entity best = entt::null;
        float bestDist = 1e30f;
        for (auto e : m_Registry.view<TransformComponent, PlanarReflectionComponent>())
        {
            if (!IsEntityActive(e))
                continue;
            const float d = glm::distance(glm::vec3(m_Registry.get<TransformComponent>(e).WorldTransform[3]), camera.Position);
            if (d < bestDist) { bestDist = d; best = e; }
        }
        if (best == entt::null)
            return;
        auto& pr = m_Registry.get<PlanarReflectionComponent>(best);
        const glm::mat4 w = ComputeWorldMatrix(best);
        const glm::vec3 p0 = glm::vec3(w[3]), n = glm::normalize(glm::vec3(w[1]));
        RenderCamera mirrored;
        if (!PlanarReflections::BuildMirroredCamera(camera, p0, n, mirrored))
            return;
        const float pct = std::clamp(pr.ScreenPercentage, 10.0f, 100.0f) / 100.0f;
        const uint32_t rw = std::max(1u, (uint32_t)(camera.ViewportWidth * pct)), rh = std::max(1u, (uint32_t)(camera.ViewportHeight * pct));
        mirrored.ViewportWidth = rw;
        mirrored.ViewportHeight = rh;
        static GLuint s_Fbo = 0, s_Color = 0, s_Depth = 0;
        static uint32_t s_W = 0, s_H = 0;
        if (!s_Fbo || s_W != rw || s_H != rh)
        {
            if (s_Fbo) { glDeleteFramebuffers(1, &s_Fbo); glDeleteTextures(1, &s_Color); glDeleteRenderbuffers(1, &s_Depth); }
            glCreateTextures(GL_TEXTURE_2D, 1, &s_Color);
            glTextureStorage2D(s_Color, 1, GL_RGBA16F, (GLsizei)rw, (GLsizei)rh);
            glTextureParameteri(s_Color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(s_Color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(s_Color, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(s_Color, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glCreateRenderbuffers(1, &s_Depth);
            glNamedRenderbufferStorage(s_Depth, GL_DEPTH24_STENCIL8, (GLsizei)rw, (GLsizei)rh);
            glCreateFramebuffers(1, &s_Fbo);
            glNamedFramebufferTexture(s_Fbo, GL_COLOR_ATTACHMENT0, s_Color, 0);
            glNamedFramebufferRenderbuffer(s_Fbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_Depth);
            s_W = rw;
            s_H = rh;
        }
        GLint prevFbo = 0, prevViewport[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, s_Fbo);
        glViewport(0, 0, (GLsizei)rw, (GLsizei)rh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        // Aynalanmış görünüm el yönünü çevirir: ön yüzler saat yönünde
        glFrontFace(GL_CW);
        m_RenderingPlanar = true;
        RenderScene3D(mirrored, false);
        m_RenderingPlanar = false;
        glFrontFace(GL_CCW);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        Renderer3D::SetPlanarReflection(s_Color, glm::vec4(n, -glm::dot(n, p0)),
                                        glm::vec4(1.0f, std::max(pr.DistanceFromPlaneFadeEnd, 0.01f), pr.NormalDistortionStrength, std::max(pr.MaxRoughness, 0.01f)));
        pr.Renders++;
    }

    void Scene::RenderScene3D(const RenderCamera& camera, bool editorView)
    {
        CQ_PROFILE_SAMPLE("Render.3D", Rendering);
        CQ_PROFILE_GPU("Scene 3D");
        // Tree Creator: ayarı değişen ağaçlar yeniden üretilir, Mesh Renderer'a verilir (yüklenen sahnede ilk karede de)
        TreeCreator::Update(*this);
        // Texture Streaming: bu görünümün gerektirdiği doku mip seviyeleri. Editörün sahne görünümü her zaman; oyunda kamera
        // Streaming Controller'lıysa ya da Add All Cameras açıksa (Unity gibi)
        if (!m_CapturingProbes && !m_RenderingPlanar && TextureStreaming::Settings().Enabled)
        {
            float bias = 0.0f;
            bool counts = editorView;
            if (!editorView)
            {
                const auto* controller = m_BackgroundCamera != entt::null && m_Registry.valid(m_BackgroundCamera)
                                       ? m_Registry.try_get<StreamingControllerComponent>(m_BackgroundCamera) : nullptr;
                counts = TextureStreaming::Settings().AddAllCameras || controller;
                if (controller)
                    bias = controller->StreamingMipBias;
            }
            if (counts)
                TextureStreaming::RequestView(*this, camera, bias);
            if (!editorView)
                TextureStreaming::RequestPreloads(*this, camera.ViewportWidth, camera.ViewportHeight);
        }
        CustomRenderTextures::UpdateAll();   // Custom Render Texture'lar (karede bir kez)
        if (!m_CapturingProbes)
            CaptureReflectionProbes();
            CaptureSceneCubes();

        // Planar Reflection: ana görünümden önce aynalanmış sahne (kendi dokusuna)
        if (!m_RenderingPlanar && !m_CapturingProbes)
            RenderPlanarReflection(camera);

        SceneEnvironment env = BuildRenderEnvironment(true);
        if (editorView)
        {
            const auto& opts = m_EditorViewOptions;
            if (!opts.Skybox || m_EditorView2D) env.DrawSky = false;
            if (!opts.Fog) env.Settings.Fog.Enabled = false;
            if (!opts.Shadows) env.Settings.Shadows.Enabled = false;
        }
        else if (!m_CapturingProbes && m_BackgroundCamera != entt::null)
        {
            // Oyun görünümü: kameranın Background ayarı (yansıma probları her zaman gökyüzünü görür)
            if (!ResolveCameraBackground(Entity{ m_BackgroundCamera, this }).DrawSky)
                env.DrawSky = false;
            // Kameraya özel gökyüzü (Skybox bileşeni)
            if (auto* sky = m_Registry.try_get<CameraSkyboxComponent>(m_BackgroundCamera))
                if (auto cube = CameraSkybox::Resolve(*sky))
                {
                    env.BackgroundSkybox = cube;
                    env.BackgroundExposure = sky->Exposure;
                    env.BackgroundTint = sky->Tint;
                }
        }
        Renderer3D::BeginFrame(camera, env);

        uint32_t lightCount = 0;

        // Güneş (Sun Source)
        Entity sunSource = GetSunSourceEntity();
        if (sunSource && sunSource.HasComponent<DirectionalLightComponent>() && IsEntityActive(sunSource))
        {
            Renderer3D::SetDirectionalLight(sunSource.GetComponent<DirectionalLightComponent>());
            lightCount++;
        }
        else if (m_Registry.view<DirectionalLightComponent>().empty())
        {
            // Sahnede yönlü ışık yoksa yumuşak varsayılan ışık (eski davranış)
            DirectionalLightComponent defaultLight;
            defaultLight.Direction = glm::vec3(-0.5f, -1.0f, -0.3f);
            defaultLight.Color = glm::vec3(1.0f);
            defaultLight.Intensity = 0.5f;
            Renderer3D::SetDirectionalLight(defaultLight);
        }

        // Nokta ve spot ışıklar (dünya konumu)
        auto pointLightView = m_Registry.view<TransformComponent, PointLightComponent>();
        for (auto entity : pointLightView)
        {
            if (!IsEntityActive(entity))
                continue;
            auto [transform, pointLight] = pointLightView.get<TransformComponent, PointLightComponent>(entity);
            Renderer3D::AddPointLight(glm::vec3(transform.WorldTransform[3]), pointLight);
            if (auto* profile = m_Registry.try_get<LightProfileComponent>(entity))
            {
                glm::vec4 p0, p1;
                if (LightProfiles::Sample(profile->Profile, 180.0f, p0, p1))
                    Renderer3D::SetLastPointLightProfile(p0, p1);
            }
            lightCount++;
        }
        auto spotLightView = m_Registry.view<TransformComponent, SpotLightComponent>();
        for (auto entity : spotLightView)
        {
            if (!IsEntityActive(entity))
                continue;
            auto [transform, spotLight] = spotLightView.get<TransformComponent, SpotLightComponent>(entity);
            Renderer3D::AddSpotLight(glm::vec3(transform.WorldTransform[3]), spotLight);
            if (auto* profile = m_Registry.try_get<LightProfileComponent>(entity))
            {
                glm::vec4 p0, p1;
                if (LightProfiles::Sample(profile->Profile, std::max(spotLight.InnerConeAngle, spotLight.OuterConeAngle), p0, p1))
                    Renderer3D::SetLastSpotLightProfile(p0, p1);
            }
            lightCount++;
        }
        AreaLights::Submit(*this);
        lightCount += (uint32_t)m_Registry.view<AreaLightComponent>().size();
        Renderer::GetStats().LightCount = lightCount;

        // Yansıma probları (yakalanmış olanlar)
        if (!m_CapturingProbes)
        {
            auto probeView = m_Registry.view<TransformComponent, ReflectionProbeComponent>();
            for (auto entity : probeView)
            {
                if (!IsEntityActive(entity))
                    continue;
                auto [transform, probe] = probeView.get<TransformComponent, ReflectionProbeComponent>(entity);
                if (!probe.Captured)
                    continue;
                glm::vec3 pos = glm::vec3(transform.WorldTransform[3]);
                glm::vec3 boxMin = pos + probe.BoxOffset;
                glm::vec3 boxMax = boxMin + probe.BoxSize * 2.0f;
                Renderer3D::AddReflectionProbe(pos, probe.Captured->GetPrefilteredID(), (uint32_t)probe.Captured->GetPrefilteredMaxMip() + 1,
                                               boxMin, boxMax, probe.Intensity, probe.BlendDistance);
            }
        }

        // LOD grupları: bu kamera için gizlenecek seviyeler
        GameplaySystems::UpdateLOD(*this, camera, m_LODHidden);
        // Occlusion Culling: kameranın hücresinden görünmeyen statik nesneler (oyunda; düzenleyicide Visualize açıkken)
        m_OcclusionHidden.clear();
        if (!m_CapturingProbes && (!editorView || m_OcclusionVisualize))
            OcclusionCulling::ComputeHidden(*this, camera.Position, m_OcclusionHidden);
        // Cull Distance Volume: uzaktaki küçük statik nesneler (Unreal: düzenleyicide de)
        if (!m_CapturingProbes)
            WorldActors::ComputeCullHidden(*this, camera.Position, m_OcclusionHidden);

        // Blend shape fark dokuları (gölge, renk ve hareket vektörü geçişleri aynı şekli kullanır)
        BlendShapes::Update(*this);
        // Virtual Geometry (Nanite benzeri): bu görünümün nesneleri GPU güdümlü partilere (gölge ve opak çizim GPU'da seçer)
        {
            const uint32_t vgView = m_CapturingProbes ? 3u : m_RenderingPlanar ? 4u : s_GISecondaryView ? 2u : (editorView ? 0u : 1u);
            ClusterRenderer::Gather(*this, Renderer3D::GetCamera(), vgView, [this](uint32_t e) {
                const auto id = (entt::entity)e;
                return IsEntityActive(id) && !m_LODHidden.count(e) && !m_OcclusionHidden.count(e);
            });
        }
        // Gölgeler -> gökyüzü
        {
            CQ_PROFILE_SAMPLE("Render.Shadows", Rendering);
            CQ_PROFILE_GPU("Shadows");
            Renderer3D::RenderShadows(this);
        }
        // Ray Traced GI (Lumen benzeri): ışıklar verildikten sonra, opak çizimden önce problar güncellenir ve bağlanır. Editörün
        // sahne görünümü ve oyun görünümü ayrı hacim; ikincil kameralar (Render Texture) oyun hacmini izlemeden kullanır.
        // Yansıma probu / planar yakalamaları kendi ortam ışığıyla çizilir
        const bool giView = !m_CapturingProbes && !m_RenderingPlanar;
        if (giView)
            RayTracedGI::Update(*this, camera, env.Settings.Post, s_GISecondaryView ? 1u : (editorView ? 0u : 1u), !s_GISecondaryView);
        // Fog Volume: bu görünümün hacimsel sis geçişi (son işlem) yerel yoğunlukları okur
        if (giView)
            FogVolumes::Collect(*this);
        {
            CQ_PROFILE_SAMPLE("Render.Sky", Rendering);
            CQ_PROFILE_GPU("Sky");
            Renderer3D::DrawEnvironment();
            // Video Player "Camera Far Plane": gökyüzünün yerine, geometrinin arkasında
            if (!editorView && !m_CapturingProbes)
                VideoPlayers::RenderCameraPlanes(*this, true);
        }

        // Opak geometri: meshler ve modeller kuyruğa alınıp yakından uzağa çizilir (FlushOpaque), araziler sonra; saydamlar
        // EndScene'de uzaktan yakına sıralanır
        Profiling::FrameProfiler::BeginSample("Render.Opaque", Profiling::Category::Rendering);
        Profiling::FrameProfiler::BeginGPUSample("Opaque");

        // Meshler
        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            if (!IsEntityActive(entity) || m_LODHidden.count((uint32_t)entity) || m_OcclusionHidden.count((uint32_t)entity))
                continue;
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
            if (meshRenderer.IsModel())
                continue;   // model ağı aşağıda (Modeller)
            Renderer3D::SetDrawCastsShadows(meshRenderer.CastShadows);
            // Statik nesne lightmap'ten, hareketli nesne ışık problarından (yoksa gökyüzünden) ortam ışığı alır
            {
                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                Renderer3D::SetDrawLightmapped(tag && tag->IsStatic);
            }
            // TAA hareket takibi bu varlığın kimliğiyle (başka nesnelerin belirip kaybolması bu nesneyi "hareketli" yapmaz)
            Renderer3D::SetDrawIdentity(MotionIdentity(m_Registry, entity));

            // Unity gibi: renk, doku, ışıma ve Shader Graph malzemenin içinde; malzemesiz nesne varsayılan malzemeyle çizilir
            std::shared_ptr<Material> material = meshRenderer.MaterialInstance;
            if (!material)
            {
                static std::shared_ptr<Material> s_Fallback;
                material = MaterialAsset::DefaultGrid();
                if (!material)
                {
                    if (!s_Fallback)
                        s_Fallback = std::make_shared<Material>();
                    material = s_Fallback;
                }
            }

            Renderer3D::SetReceiveShadows(meshRenderer.ReceiveShadows);
            // Mesh Paint: örneğe özel köşe renkleri
            const bool painted = m_Registry.all_of<VertexPaintComponent>(entity);
            if (painted)
                Renderer3D::SetVertexColorBuffers(VertexPaint::Prepare(Entity{ entity, this }));
            if (meshRenderer.CustomMesh)
                Renderer3D::DrawMesh(meshRenderer.CustomMesh, transform.WorldTransform, material, false);
            else
            {
                switch (meshRenderer.Type)
                {
                    case MeshType::Sphere:   Renderer3D::DrawSphere(transform.WorldTransform, material); break;
                    case MeshType::Plane:    Renderer3D::DrawMesh(Renderer3D::GetPlaneMesh(), transform.WorldTransform, material, true); break;
                    case MeshType::Cylinder: Renderer3D::DrawCylinder(transform.WorldTransform, material); break;
                    default:                 Renderer3D::DrawCube(transform.WorldTransform, material); break;
                }
            }
            if (painted)
                Renderer3D::SetVertexColorBuffers(nullptr);
        }
        Renderer3D::SetDrawIdentity(0);
        Renderer3D::SetReceiveShadows(true);

        // 3D karo haritaları (altıgen/kutu prizmalar)
        GridSystem::DrawTilemaps3D(*this);

        // Modeller
        auto modelView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : modelView)
        {
            if (!IsEntityActive(entity) || m_LODHidden.count((uint32_t)entity) || m_OcclusionHidden.count((uint32_t)entity))
                continue;
            if (ClusterRenderer::Handles((uint32_t)entity))
                continue;   // Virtual Geometry: GPU güdümlü partide (aşağıda)
            auto [transform, modelComp] = modelView.get<TransformComponent, MeshRendererComponent>(entity);
            if (!modelComp.IsModel())
                continue;
            Renderer3D::SetDrawCastsShadows(modelComp.CastShadows);
            if (!modelComp.ModelData)
                continue;
            {
                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                Renderer3D::SetDrawLightmapped(tag && tag->IsStatic);
            }
            Renderer3D::SetDrawIdentity(MotionIdentity(m_Registry, entity));

            Entity ren{ entity, this };
            // Örnek rengi (Model.Tint): yalnız bu nesnenin çizimleri için; döngü sonunda 1'e döner
            Renderer3D::SetTint(modelComp.Tint);
            if (modelComp.ModelData->IsSkinned && modelComp.ModelData->SkeletonData)
            {
                const std::vector<glm::mat4>* bones = nullptr;
                if (const auto* lp = m_Registry.try_get<LeaderPoseComponent>(entity); lp && !lp->BoneMatrices.empty())
                    bones = &lp->BoneMatrices;
                else if (ren.HasComponent<AnimatorComponent>())
                    bones = &ren.GetComponent<AnimatorComponent>().BoneMatricesGPU;
                else if (ren.HasComponent<AnimationComponent>())
                    bones = &ren.GetComponent<AnimationComponent>().BoneMatricesGPU;
                const std::vector<uint32_t>* morph = BlendShapes::GetTextures(*this, (uint32_t)entity);
                if (bones && bones->size() == modelComp.ModelData->SkeletonData->GetBoneCount())
                {
                    Renderer3D::DrawSkinnedModel(transform.WorldTransform, modelComp.ModelData, *bones, morph);
                }
                else
                {
                    static thread_local std::vector<glm::mat4> s_IdPalette;
                    s_IdPalette.assign(modelComp.ModelData->SkeletonData->GetBoneCount(), glm::mat4(1.f));
                    Renderer3D::DrawSkinnedModel(transform.WorldTransform, modelComp.ModelData, s_IdPalette, morph);
                }
            }
            else
            {
                const bool painted = m_Registry.all_of<VertexPaintComponent>(entity);
                // Virtual Geometry (Nanite benzeri): küme hiyerarşisi hazırsa ekran hatasına göre seçilen kümeler çizilir
                if (auto* vg = m_Registry.try_get<VirtualGeometryComponent>(entity); vg && vg->Enabled && !painted)
                {
                    const auto& model = *modelComp.ModelData;
                    std::vector<std::shared_ptr<ClusterMesh>> clusterMeshes;
                    bool ready = !model.Meshes.empty();
                    for (const auto& m : model.Meshes)
                    {
                        auto cm = m ? VirtualGeometry::Acquire(m, vg->ClusterSize) : nullptr;
                        if (!cm || !cm->IsUploaded()) { ready = false; break; }
                        clusterMeshes.push_back(cm);
                    }
                    vg->Ready = ready;
                    if (ready)
                    {
                        int drawn = 0, clustersDrawn = 0, clusters = 0, levels = 0, total = 0;
                        for (size_t i = 0; i < clusterMeshes.size(); ++i)
                        {
                            uint32_t count = 0;
                            auto material = i < model.Materials.size() ? model.Materials[i] : nullptr;
                            drawn += (int)Renderer3D::DrawClusterMesh(*clusterMeshes[i], transform.WorldTransform, material, vg->ErrorThreshold,
                                                                      vg->VisualizeClusters, &count);
                            clustersDrawn += (int)count;
                            clusters += (int)clusterMeshes[i]->GetClusters().size();
                            levels = std::max(levels, clusterMeshes[i]->GetLevelCount());
                            total += (int)clusterMeshes[i]->GetLeafTriangleCount();
                        }
                        vg->TrianglesDrawn = drawn;
                        vg->ClustersDrawn = clustersDrawn;
                        vg->Clusters = clusters;
                        vg->Levels = levels;
                        vg->TrianglesTotal = total;
                        Renderer3D::SetTint(glm::vec3(1.0f));
                        continue;
                    }
                }
                if (painted)
                    Renderer3D::SetVertexColorBuffers(VertexPaint::Prepare(ren));
                // Tree Creator ağacı: Wind Zone rüzgârıyla salınır (görüntü, gölge ve hareket vektörü birlikte)
                glm::vec4 treeWind(0.0f);
                glm::vec2 treeWindParams(0.0f);
                const bool windy = TreeCreator::WindFor(*this, entity, treeWind, treeWindParams);
                if (windy)
                    Renderer3D::SetDrawWind(treeWind, treeWindParams);
                Renderer3D::DrawModel(transform.WorldTransform, modelComp.ModelData);
                if (windy)
                    Renderer3D::SetDrawWind(glm::vec4(0.0f), glm::vec2(0.0f));
                if (painted)
                    Renderer3D::SetVertexColorBuffers(nullptr);
            }
            Renderer3D::SetTint(glm::vec3(1.0f));
        }

        Renderer3D::SetDrawIdentity(0);
        // Sonraki çizimler (kuyruk, MultiMesh, arazi, parçacık) varsayılan: gölge düşürür
        Renderer3D::SetDrawCastsShadows(true);
        Renderer3D::SetDrawLightmapped(true);
        // Kuyruktaki opak meshler / modeller yakından uzağa; MultiMesh ve araziler (büyük, çoğunlukla arkada) sonra çizilir
        // Virtual Geometry partileri, 1. geçiş: önceki karede görünen kümeler (görüş + LOD GPU'da)
        ClusterRenderer::DrawOpaque(Renderer3D::GetCamera());
        Renderer3D::FlushOpaque();

        // MultiMesh: GPU örneklemeli kümeler
        MultiMesh::Render(*this);

        // Terrain'ler
        auto terrainView = m_Registry.view<TransformComponent, TerrainComponent>();
        for (auto entity : terrainView)
        {
            if (!IsEntityActive(entity))
                continue;
            auto [transform, terrain] = terrainView.get<TransformComponent, TerrainComponent>(entity);
            TerrainSystem::Render(terrain, transform.WorldTransform);
        }
        // Virtual Geometry, 2. geçiş: opak derinlikten HZB; önceki karede örtülü olup artık görünen kümeler aynı karede çizilir
        ClusterRenderer::DrawDisoccluded(Renderer3D::GetCamera());

        Profiling::FrameProfiler::EndGPUSample();
        Profiling::FrameProfiler::EndSample();
        // Çıkartmalar: opak yüzeylerin üstüne, saydamlardan önce
        {
            CQ_PROFILE_SAMPLE("Render.Decals", Rendering);
            Decals::Render(*this);
        }
        // Billboard'lar (alfa kesmeli, derinlik yazar): saydamlardan önce
        Billboards::RenderOpaque(*this, camera);
        {
            CQ_PROFILE_SAMPLE("Render.Transparent", Rendering);
            CQ_PROFILE_GPU("Transparent");
            Renderer3D::EndScene();
        }
        // Su yüzeyleri (saydam, sahne derinliğiyle kıyı yumuşaması)
        WaterBodies::Render(*this, camera.Position);

        // UI: dünya uzayı canvas'ları (editörde ekran üstü canvas'lar da sahnede gösterilir)
        {
            CQ_PROFILE_SAMPLE("UI.RenderWorldSpace", UI);
            UISystem::RenderWorldSpace(*this, camera.Projection * camera.View, false, m_UIScreenSize);
        }

        // Parçacıklar (saydam, opak geometri ve derinlikten sonra)
        {
            CQ_PROFILE_SAMPLE("Particles.Render", Particles);
            CQ_PROFILE_GPU("Particles");
            auto particleView = m_Registry.view<TransformComponent, ParticleSystemComponent>();
            for (auto entity : particleView)
            {
                if (!IsEntityActive(entity))
                    continue;
                auto [transform, ps] = particleView.get<TransformComponent, ParticleSystemComponent>(entity);
                ParticleSystem::Render(ps, transform.WorldTransform);
            }
            VisualEffects::Render(*this);
            // Çizgi ve iz çiziciler (saydam)
            GameplaySystems::RenderRibbons(*this);
        }
        // Halolar ve lens parlamaları (toplamalı; parlama örtülmesi GPU sorgusuyla)
        {
            CQ_PROFILE_SAMPLE("Render.Flares", Rendering);
            Billboards::RenderEffects(*this, camera);
        }
        // Video Player "Camera Near Plane": sahnenin önünde
        if (!editorView && !m_CapturingProbes)
            VideoPlayers::RenderCameraPlanes(*this, false);
        // Bu görünümün GI bağlantısı kaldırılır (önizleme / küçük resim çizimleri problardan etkilenmesin)
        if (giView)
            RayTracedGI::EndView();
        ClusterRenderer::EndView();
        (void)editorView;
    }

    void Scene::UpdateParticles(float dt, bool editorPreviewOnly)
    {
        auto view = m_Registry.view<TransformComponent, ParticleSystemComponent>();
        std::vector<std::pair<entt::entity, int>> stopped;   // Stop Action (döngü bitince uygulanır)
        for (auto entity : view)
        {
            auto [transform, ps] = view.get<TransformComponent, ParticleSystemComponent>(entity);
            if (!IsEntityActive(entity))
                continue;
            if (editorPreviewOnly && !ps.EditorPreview)
            {
                // Önizleme kapandıysa editörde kalan parçacıkları temizle
                if (!ps.RT->Particles.empty() || ps.RT->Playing)
                    ParticleSystem::Stop(ps, true);
                ps.RT->Started = false;
                continue;
            }
            if (!editorPreviewOnly && IsProcessDisabled(entity))
                continue;   // Visible On Screen Enabler: ekran dışında benzetim durur
            ParticleSystem::Update(ps, transform.WorldTransform, dt);
            if (!editorPreviewOnly && ps.StopAction != 0 && ParticleSystem::IsFinished(ps))
                stopped.emplace_back(entity, ps.StopAction);
        }
        for (auto [entity, action] : stopped)
        {
            if (!m_Registry.valid(entity))
                continue;
            Entity e{ entity, this };
            if (action == 2)
                DestroyEntity(e);
            else
            {
                if (!e.HasComponent<ActiveComponent>())
                    e.AddComponent<ActiveComponent>();
                e.GetComponent<ActiveComponent>().Active = false;
                e.GetComponent<ParticleSystemComponent>().RT->EverActive = false;
            }
        }
        // Visual Effect (VFX Graph): GPU simülasyonu, düzenleyicide de oynar
        VisualEffects::Update(*this, dt);
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        // Resize cameras
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent = view.get<CameraComponent>(entity);
            if (!cameraComponent.FixedAspectRatio)
                cameraComponent.Camera.SetViewportSize(width, height);
        }
    }

    Entity Scene::GetPrimaryCameraEntity()
    {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            const auto& camera = view.get<CameraComponent>(entity);
            // Render hedefi kameraları ekrana değil dokuya çizer
            if (camera.Primary && !m_Registry.all_of<RenderTargetComponent>(entity))
                return Entity{ entity, this };
        }
        return {};
    }

    Entity Scene::GetSunSourceEntity()
    {
        // Sun Source UUID varsa onu bul
        if (m_SunSourceUUID != 0)
        {
            auto view = m_Registry.view<IDComponent, DirectionalLightComponent>();
            for (auto entity : view)
            {
                const auto& id = view.get<IDComponent>(entity);
                if (id.ID == m_SunSourceUUID)
                    return Entity{ entity, this };
            }
        }
        
        // Yoksa ilk directional light'ı döndür
        auto view = m_Registry.view<DirectionalLightComponent>();
        if (!view.empty())
            return Entity{ *view.begin(), this };
        
        return {};
    }

    Entity Scene::FindEntityByTag(const std::string& tag)
    {
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view)
        {
            const auto& tagComp = view.get<TagComponent>(entity);
            if (tagComp.GameTag == tag)
                return Entity{ entity, this };
        }
        return {};
    }

    std::vector<Entity> Scene::FindEntitiesWithTag(const std::string& tag)
    {
        std::vector<Entity> entities;
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view)
        {
            const auto& tagComp = view.get<TagComponent>(entity);
            if (tagComp.GameTag == tag)
                entities.push_back(Entity{ entity, this });
        }
        return entities;
    }

    Entity Scene::FindEntityByUUID(uint64_t uuid)
    {
        auto view = m_Registry.view<IDComponent>();
        for (auto entity : view)
        {
            const auto& id = view.get<IDComponent>(entity);
            if (id.ID == uuid)
                return Entity{ entity, this };
        }
        return {};
    }

    const std::vector<std::string>& Scene::GetAvailableTags()
    {
        return s_AvailableTags;
    }

    size_t Scene::GetBuiltinTagCount() { return 8; }

    void Scene::SetCustomTags(const std::vector<std::string>& custom)
    {
        s_AvailableTags.resize(GetBuiltinTagCount());
        for (const auto& t : custom)
            if (!t.empty() && std::find(s_AvailableTags.begin(), s_AvailableTags.end(), t) == s_AvailableTags.end())
                s_AvailableTags.push_back(t);
    }

    int32_t Scene::AddLayer(const std::string& name)
    {
        int32_t newID = GetNextLayerID();
        m_Layers.push_back({ newID, name });
        return newID;
    }

    void Scene::RemoveLayer(int32_t layerID)
    {
        if (layerID == 0) return; // Default layer silinemez
        
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
            [layerID](const LayerInfo& layer) { return layer.ID == layerID; });
        
        if (it != m_Layers.end())
            m_Layers.erase(it);
    }

    void Scene::RenameLayer(int32_t layerID, const std::string& newName)
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
            [layerID](const LayerInfo& layer) { return layer.ID == layerID; });
        
        if (it != m_Layers.end())
            it->Name = newName;
    }

    std::string Scene::GetLayerName(int32_t layerID) const
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
            [layerID](const LayerInfo& layer) { return layer.ID == layerID; });
        
        if (it != m_Layers.end())
            return it->Name;
        
        return "Unknown Layer";
    }

    int32_t Scene::GetNextLayerID() const
    {
        if (m_Layers.empty())
            return 0;
        
        int32_t maxID = 0;
        for (const auto& layer : m_Layers)
        {
            if (layer.ID > maxID)
                maxID = layer.ID;
        }
        return maxID + 1;
    }
    
    void Scene::SetFlareElementCount(int count)
    {
        if (count < 1) count = 1;
        if (count > 10) count = 10; // Max 10 element
        
        int oldCount = (int)m_FlareElements.size();
        m_FlareElements.resize(count);
        
        // Yeni eklenen elementlere default değerler
        for (int i = oldCount; i < count; i++)
        {
            float offset = 0.3f + (float)i * 0.3f;
            if (offset > 1.0f) offset = 1.0f;
            
            m_FlareElements[i] = { 
                glm::vec3(1.0f), 
                0.08f - (float)i * 0.01f, 
                offset, 
                1.0f - (float)i * 0.2f 
            };
        }
    }
    
    void Scene::SetFlareElement(int index, const FlareElement& element)
    {
        if (index >= 0 && index < (int)m_FlareElements.size())
        {
            m_FlareElements[index] = element;
        }
    }
    
    Scene::FlareElement Scene::GetFlareElement(int index) const
    {
        if (index >= 0 && index < (int)m_FlareElements.size())
        {
            return m_FlareElements[index];
        }
        return FlareElement();
    }

    template<typename T>
    void Scene::OnComponentAdded([[maybe_unused]] Entity entity, [[maybe_unused]] T& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<IDComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] IDComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<TransformComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] TransformComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<TagComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] TagComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<ActiveComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ActiveComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<CameraComponent>([[maybe_unused]] Entity entity, CameraComponent& component)
    {
        if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
            component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<SpriteRendererComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] SpriteRendererComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<MeshRendererComponent>(Entity entity, MeshRendererComponent& component)
    {
        // Varsayılan malzeme (Unreal WorldGridMaterial gibi): dama dokusu motorla gelen salt okunur
        // "Packages/Core/Materials/Default Grid" malzemesinin içinde. Betik / Inspector renk ya da doku verirse
        // MeshRendererMaterial::Edit nesneye özel "(Instance)" kopyası çıkarır.
        component.MaterialPath = MaterialAsset::DefaultGridPath();
        component.MaterialInstance = MaterialAsset::DefaultGrid();
        if (!component.MaterialInstance)
            component.MaterialPath.clear();
    }

    template<>
    CQ_API void Scene::OnComponentAdded<RigidBody2DComponent>(Entity entity, [[maybe_unused]] RigidBody2DComponent& component)
    {
        if (m_PhysicsWorld2D)
            m_PhysicsWorld2D->MarkDirty(entity);
    }

    namespace
    {
        // Sprite / görüntü yerel sınırları (2D çarpıştırıcı sığdırma)
        bool LocalSpriteBounds(Entity e, glm::vec2& mn, glm::vec2& mx)
        {
            if (e.HasComponent<SpriteRendererComponent>() && Renderer2D::GetSpriteLocalBounds(e.GetComponent<SpriteRendererComponent>(), mn, mx))
                return true;
            if (e.HasComponent<SpriteRendererComponent>())
            {
                mn = glm::vec2(-0.5f);
                mx = glm::vec2(0.5f);
                return true;
            }
            return false;
        }
    }

    template<>
    CQ_API void Scene::OnComponentAdded<BoxCollider2DComponent>(Entity entity, [[maybe_unused]] BoxCollider2DComponent& component)
    {
        glm::vec2 mn, mx;
        if (LocalSpriteBounds(entity, mn, mx))
        {
            component.Size = glm::max(mx - mn, glm::vec2(0.01f));
            component.Offset = (mn + mx) * 0.5f;
        }
        if (m_PhysicsWorld2D)
            m_PhysicsWorld2D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<CircleCollider2DComponent>(Entity entity, [[maybe_unused]] CircleCollider2DComponent& component)
    {
        glm::vec2 mn, mx;
        if (LocalSpriteBounds(entity, mn, mx))
        {
            component.Radius = std::max(0.01f, std::max(mx.x - mn.x, mx.y - mn.y) * 0.5f);
            component.Offset = (mn + mx) * 0.5f;
        }
        if (m_PhysicsWorld2D)
            m_PhysicsWorld2D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<RigidbodyComponent>(Entity entity, [[maybe_unused]] RigidbodyComponent& component)
    {
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->MarkDirty(entity);
    }

#define CQ_PHYSICS_ADDED(T) template<> CQ_API void Scene::OnComponentAdded<T>(Entity entity, [[maybe_unused]] T& component) \
    { if (m_PhysicsWorld3D) m_PhysicsWorld3D->MarkDirty(entity); if (m_PhysicsWorld2D) m_PhysicsWorld2D->MarkDirty(entity); }
    CQ_PHYSICS_ADDED(CharacterControllerComponent) CQ_PHYSICS_ADDED(FixedJointComponent) CQ_PHYSICS_ADDED(HingeJointComponent)
    CQ_PHYSICS_ADDED(SliderJointComponent) CQ_PHYSICS_ADDED(SpringJointComponent) CQ_PHYSICS_ADDED(CharacterJointComponent)
    CQ_PHYSICS_ADDED(BallJointComponent) CQ_PHYSICS_ADDED(ConstantForceComponent)
    CQ_PHYSICS_ADDED(CapsuleCollider2DComponent) CQ_PHYSICS_ADDED(PolygonCollider2DComponent) CQ_PHYSICS_ADDED(EdgeCollider2DComponent)
    CQ_PHYSICS_ADDED(DistanceJoint2DComponent) CQ_PHYSICS_ADDED(SpringJoint2DComponent) CQ_PHYSICS_ADDED(HingeJoint2DComponent)
    CQ_PHYSICS_ADDED(SliderJoint2DComponent) CQ_PHYSICS_ADDED(WheelJoint2DComponent) CQ_PHYSICS_ADDED(FixedJoint2DComponent)
    CQ_PHYSICS_ADDED(PlatformEffector2DComponent) CQ_PHYSICS_ADDED(AreaEffector2DComponent) CQ_PHYSICS_ADDED(PointEffector2DComponent) CQ_PHYSICS_ADDED(CompositeCollider2DComponent) CQ_PHYSICS_ADDED(ClothComponent)
    CQ_PHYSICS_ADDED(SurfaceEffector2DComponent) CQ_PHYSICS_ADDED(BuoyancyEffector2DComponent)
    CQ_PHYSICS_ADDED(TilemapCollider2DComponent) CQ_PHYSICS_ADDED(WheelColliderComponent) CQ_PHYSICS_ADDED(ConfigurableJointComponent)
    CQ_PHYSICS_ADDED(FrictionJoint2DComponent) CQ_PHYSICS_ADDED(RelativeJoint2DComponent) CQ_PHYSICS_ADDED(TargetJoint2DComponent) CQ_PHYSICS_ADDED(ConstantForce2DComponent)
#define CQ_PLAIN_ADDED(T) template<> CQ_API void Scene::OnComponentAdded<T>([[maybe_unused]] Entity entity, [[maybe_unused]] T& component) {}
    CQ_PLAIN_ADDED(LineRendererComponent) CQ_PLAIN_ADDED(TrailRendererComponent) CQ_PLAIN_ADDED(LODGroupComponent)
    CQ_PLAIN_ADDED(LookAtConstraintComponent) CQ_PLAIN_ADDED(AimConstraintComponent) CQ_PLAIN_ADDED(PositionConstraintComponent)
    CQ_PLAIN_ADDED(RotationConstraintComponent) CQ_PLAIN_ADDED(ScaleConstraintComponent) CQ_PLAIN_ADDED(ParentConstraintComponent)
    CQ_PLAIN_ADDED(WindZoneComponent) CQ_PLAIN_ADDED(CameraFollowComponent) CQ_PLAIN_ADDED(RotatorComponent) CQ_PLAIN_ADDED(ParticleForceFieldComponent)
    CQ_PLAIN_ADDED(SplineContainerComponent) CQ_PLAIN_ADDED(SplineAnimateComponent) CQ_PLAIN_ADDED(SplineExtrudeComponent) CQ_PLAIN_ADDED(SplineInstantiateComponent)
    CQ_PLAIN_ADDED(VirtualCameraComponent) CQ_PLAIN_ADDED(CameraBrainComponent) CQ_PLAIN_ADDED(ImpulseSourceComponent) CQ_PLAIN_ADDED(PostProcessVolumeComponent) CQ_PLAIN_ADDED(DecalProjectorComponent)
    CQ_PLAIN_ADDED(Light2DComponent) CQ_PLAIN_ADDED(ShadowCaster2DComponent) CQ_PLAIN_ADDED(SpriteMaskComponent) CQ_PLAIN_ADDED(PixelPerfectCameraComponent)
    CQ_PLAIN_ADDED(BillboardRendererComponent) CQ_PLAIN_ADDED(HaloComponent) CQ_PLAIN_ADDED(LensFlareComponent) CQ_PLAIN_ADDED(CameraSkyboxComponent)
    CQ_PLAIN_ADDED(AreaLightComponent) CQ_PLAIN_ADDED(PlayerInputComponent) CQ_PLAIN_ADDED(PlayerInputManagerComponent)
    CQ_PLAIN_ADDED(NavMeshModifierComponent) CQ_PLAIN_ADDED(NavMeshModifierVolumeComponent) CQ_PLAIN_ADDED(NavMeshLinkComponent)
    CQ_PLAIN_ADDED(VideoPlayerComponent)
    CQ_PLAIN_ADDED(GameplayTagsComponent)
    CQ_PLAIN_ADDED(AbilitySystemComponent)
    CQ_PLAIN_ADDED(PlayerStartComponent)
    CQ_PLAIN_ADDED(GameModeComponent)
    CQ_PLAIN_ADDED(SpringArmComponent)
    CQ_PLAIN_ADDED(BoneAttachmentComponent)
    CQ_PLAIN_ADDED(StreamedLevelComponent)
    CQ_PLAIN_ADDED(AnimationPlayerComponent)
    CQ_PLAIN_ADDED(LevelStreamingVolumeComponent)
    CQ_PLAIN_ADDED(AutoLODComponent)
    CQ_PLAIN_ADDED(SoundOcclusionComponent)
    CQ_PLAIN_ADDED(SoundConcurrencyComponent)
    CQ_PLAIN_ADDED(VertexPaintComponent)
    CQ_PLAIN_ADDED(FractureComponent)
    CQ_PLAIN_ADDED(RagdollComponent)
    CQ_PLAIN_ADDED(VirtualGeometryComponent)
    CQ_PLAIN_ADDED(CharacterCustomizerComponent)
    CQ_PLAIN_ADDED(SignalConnectionsComponent)
    CQ_PLAIN_ADDED(CSGShapeComponent)
    CQ_PLAIN_ADDED(MultiMeshComponent)
    CQ_PLAIN_ADDED(RemoteTransformComponent)
    CQ_PLAIN_ADDED(RayCastComponent)
    CQ_PLAIN_ADDED(VisibleOnScreenNotifierComponent)
    CQ_PLAIN_ADDED(Parallax2DComponent)
    CQ_PLAIN_ADDED(TimerComponent)
    CQ_PLAIN_ADDED(UISpinBoxComponent) CQ_PLAIN_ADDED(UIColorPickerComponent) CQ_PLAIN_ADDED(UIColorPickerButtonComponent) CQ_PLAIN_ADDED(UIMenuButtonComponent) CQ_PLAIN_ADDED(UIPopupMenuComponent) CQ_PLAIN_ADDED(UIMenuBarComponent) CQ_PLAIN_ADDED(UITabContainerComponent) CQ_PLAIN_ADDED(UITabBarComponent) CQ_PLAIN_ADDED(UISplitContainerComponent) CQ_PLAIN_ADDED(UIItemListComponent) CQ_PLAIN_ADDED(UITreeComponent) CQ_PLAIN_ADDED(UILinkButtonComponent) CQ_PLAIN_ADDED(UIProgressBarComponent) CQ_PLAIN_ADDED(UIDialogComponent) CQ_PLAIN_ADDED(UIFileDialogComponent) CQ_PLAIN_ADDED(UITextEditComponent) CQ_PLAIN_ADDED(UIGraphEditComponent) CQ_PLAIN_ADDED(UIGraphNodeComponent)
    CQ_PLAIN_ADDED(ResourcePreloaderComponent)
    CQ_PLAIN_ADDED(ShaderGlobalsOverrideComponent)
    CQ_PLAIN_ADDED(ShapeCastComponent)
    CQ_PLAIN_ADDED(VisibleOnScreenEnablerComponent)
    CQ_PLAIN_ADDED(CharacterBody2DComponent)
    CQ_PLAIN_ADDED(FogVolumeComponent)
    CQ_PLAIN_ADDED(NavigationRegion2DComponent)
    CQ_PLAIN_ADDED(NavigationAgent2DComponent)
    CQ_PLAIN_ADDED(NavigationObstacle2DComponent)
    CQ_PLAIN_ADDED(NavigationLink2DComponent)
    CQ_PLAIN_ADDED(TileObjectComponent)
    CQ_PLAIN_ADDED(RenderTargetComponent)
    CQ_PLAIN_ADDED(FullScreenPassComponent)
    CQ_PLAIN_ADDED(SignalReceiverComponent)
    CQ_PLAIN_ADDED(Bone2DComponent)
    CQ_PLAIN_ADDED(IK2DComponent)
    CQ_PLAIN_ADDED(RigComponent) CQ_PLAIN_ADDED(TwoBoneIKConstraintComponent) CQ_PLAIN_ADDED(MultiAimConstraintComponent) CQ_PLAIN_ADDED(ChainIKConstraintComponent) CQ_PLAIN_ADDED(DampedTransformComponent)
    CQ_PLAIN_ADDED(ProjectileMovementComponent) CQ_PLAIN_ADDED(SortingGroupComponent) CQ_PLAIN_ADDED(PrefabInstanceComponent)
    CQ_PLAIN_ADDED(GridComponent) CQ_PLAIN_ADDED(TilemapComponent)
    CQ_PLAIN_ADDED(NetworkManagerComponent) CQ_PLAIN_ADDED(NetworkObjectComponent) CQ_PLAIN_ADDED(NetworkTransformComponent) CQ_PLAIN_ADDED(NetworkRigidbodyComponent) CQ_PLAIN_ADDED(NetworkAnimatorComponent)
    CQ_PLAIN_ADDED(PlayableDirectorComponent)
#undef CQ_PLAIN_ADDED
#undef CQ_PHYSICS_ADDED

    namespace
    {
        // Varlığın ağının yerel sınır kutusu (ilkel ağlar veya model); çarpıştırıcıları eklenirken sığdırmak için
        bool LocalMeshBounds(Entity e, glm::vec3& mn, glm::vec3& mx)
        {
            mn = glm::vec3(FLT_MAX);
            mx = glm::vec3(-FLT_MAX);
            auto addMesh = [&](const Mesh* mesh) {
                if (!mesh) return;
                for (const auto& v : mesh->GetVertices())
                {
                    mn = glm::min(mn, v.Position);
                    mx = glm::max(mx, v.Position);
                }
            };
            if (e.HasComponent<MeshRendererComponent>() && e.GetComponent<MeshRendererComponent>().ModelData)
            {
                for (auto& m : e.GetComponent<MeshRendererComponent>().ModelData->Meshes)
                    addMesh(m.get());
            }
            else if (e.HasComponent<MeshRendererComponent>() && !e.GetComponent<MeshRendererComponent>().IsModel())
            {
                std::shared_ptr<Mesh> mesh = e.GetComponent<MeshRendererComponent>().CustomMesh;
                if (!mesh)
                switch (e.GetComponent<MeshRendererComponent>().Type)
                {
                    case MeshType::Cube: mesh = Renderer3D::GetCubeMesh(); break;
                    case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                    case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                    case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                }
                if (mesh)
                    addMesh(mesh.get());
                else
                {
                    // Oluşturucu yoksa bilinen ilkel boyutları (küp/küre/silindir 2 birim, düzlem 2x2)
                    bool plane = e.GetComponent<MeshRendererComponent>().Type == MeshType::Plane;
                    mn = glm::vec3(-1.0f, plane ? 0.0f : -1.0f, -1.0f);
                    mx = glm::vec3(1.0f, plane ? 0.0f : 1.0f, 1.0f);
                }
            }
            return mn.x <= mx.x;
        }
    }

    void Scene::OnComponentRemovedAny(Entity entity)
    {
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->MarkDirty(entity);
        if (m_PhysicsWorld2D)
            m_PhysicsWorld2D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, [[maybe_unused]] BoxColliderComponent& component)
    {
        // Unity gibi: eklenince ağa sığdır (serileştirme/kopyalama sonra kendi değerlerini yazar)
        glm::vec3 mn, mx;
        if (LocalMeshBounds(entity, mn, mx))
        {
            component.Size = glm::max(mx - mn, glm::vec3(0.01f));
            component.Offset = (mn + mx) * 0.5f;
        }
        // Oyun sırasında: gövde (ve ata Rigidbody bileşiği) bir sonraki fizik adımında yeniden kurulur
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, [[maybe_unused]] SphereColliderComponent& component)
    {
        // Unity gibi: eklenince ağa sığdır (serileştirme/kopyalama sonra kendi değerlerini yazar)
        glm::vec3 mn, mx;
        if (LocalMeshBounds(entity, mn, mx))
        {
            glm::vec3 half = (mx - mn) * 0.5f;
            component.Radius = std::max(0.01f, std::max(half.x, std::max(half.y, half.z)));
            component.Offset = (mn + mx) * 0.5f;
        }
        // Oyun sırasında: gövde (ve ata Rigidbody bileşiği) bir sonraki fizik adımında yeniden kurulur
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<CapsuleColliderComponent>(Entity entity, [[maybe_unused]] CapsuleColliderComponent& component)
    {
        // Unity gibi: eklenince ağa sığdır (serileştirme/kopyalama sonra kendi değerlerini yazar)
        glm::vec3 mn, mx;
        if (LocalMeshBounds(entity, mn, mx))
        {
            glm::vec3 ext = mx - mn;
            component.Direction = 1;
            component.Radius = std::max(0.01f, std::max(ext.x, ext.z) * 0.5f);
            component.Height = std::max(component.Radius * 2.0f, ext.y);
            component.Offset = (mn + mx) * 0.5f;
        }
        // Oyun sırasında: gövde (ve ata Rigidbody bileşiği) bir sonraki fizik adımında yeniden kurulur
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<MeshColliderComponent>(Entity entity, [[maybe_unused]] MeshColliderComponent& component)
    {
        // Oyun sırasında: gövde (ve ata Rigidbody bileşiği) bir sonraki fizik adımında yeniden kurulur
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->MarkDirty(entity);
    }

    template<>
    CQ_API void Scene::OnComponentAdded<TextRendererComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] TextRendererComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<LayerComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] LayerComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<ButtonComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ButtonComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<CanvasComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] CanvasComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<CanvasScalerComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] CanvasScalerComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<GraphicRaycasterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] GraphicRaycasterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AnimatorComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AnimatorComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AnimationComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AnimationComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<DirectionalLightComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] DirectionalLightComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<PointLightComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] PointLightComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<SpotLightComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] SpotLightComponent& component)
    {
    }

    // Hierarchy sistem implementasyonu
    glm::mat4 Scene::ComputeWorldMatrix(entt::entity entity) const
    {
        // Yerel dönüşüm zincirinden taze dünya matrisi (önbellekteki WorldTransform bir sonraki
        // UpdateWorldTransforms'a kadar bayat olabilir: yeni oluşturulan / yeni taşınan varlıklar)
        std::vector<entt::entity> chain;
        for (entt::entity e = entity; e != entt::null && chain.size() < 256;)
        {
            if (!m_Registry.valid(e) || !m_Registry.all_of<TransformComponent>(e))
                break;
            // UI öğelerinin dünya dönüşümünü UI sistemi hesaplar
            if (m_Registry.all_of<RectTransformComponent>(e))
                return m_Registry.get<TransformComponent>(entity).WorldTransform;
            chain.push_back(e);
            auto it = m_EntityParent.find(e);
            e = it != m_EntityParent.end() ? it->second : entt::null;
        }
        glm::mat4 m(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            m = m * m_Registry.get<TransformComponent>(*it).GetTransform();
        return m;
    }

    void Scene::SetEntityWorldPose(Entity entity, const glm::vec3& position, const glm::quat* rotation)
    {
        if (!entity || !entity.HasComponent<TransformComponent>())
            return;
        auto& tc = entity.GetComponent<TransformComponent>();
        glm::mat4 parentWorld(1.0f);
        if (Entity parent = GetEntityParent(entity))
            parentWorld = ComputeWorldMatrix((entt::entity)parent);
        tc.Position = glm::vec3(glm::inverse(parentWorld) * glm::vec4(position, 1.0f));
        if (rotation)
        {
            glm::vec3 pp, pr, ps;
            Math::DecomposeTransform(parentWorld, pp, pr, ps);
            tc.Rotation = glm::eulerAngles(glm::inverse(glm::quat(pr)) * *rotation);
        }
    }

    void Scene::SetEntityParent(Entity entity, Entity parent, bool keepWorldTransform)
    {
        entt::entity entityHandle = entity;
        entt::entity parentHandle = parent;

        // Eski parent'tan çıkar (Bu işlem entity'nin Local Transform'unu Dünya Transform'una eşitler)
        RemoveEntityParent(entity);

        // Yeni parent'a ekle
        m_EntityParent[entityHandle] = parentHandle;
        m_EntityChildren[parentHandle].push_back(entityHandle);

        // Yeni parent varsa, entity'nin dünyadaki konumunu korumak için yeni parent'a göre Local hesapla
        if (keepWorldTransform && entity.HasComponent<TransformComponent>() && parent.HasComponent<TransformComponent>())
        {
            auto& tc = entity.GetComponent<TransformComponent>();
            glm::mat4 parentWorld = ComputeWorldMatrix(parentHandle);
            glm::mat4 worldTransform = tc.GetTransform(); // RemoveEntityParent sonrası bu değer zaten world pozisyonu
            
            glm::mat4 newLocal = glm::inverse(parentWorld) * worldTransform;
            
            glm::vec3 pos, rot, scale;
            Math::DecomposeTransform(newLocal, pos, rot, scale);
            tc.Position = pos;
            tc.Rotation = rot;
            tc.Scale = scale;
        }
    }

    void Scene::RemoveEntityParent(Entity entity)
    {
        entt::entity entityHandle = entity;

        auto it = m_EntityParent.find(entityHandle);
        if (it != m_EntityParent.end())
        {
            entt::entity parentHandle = it->second;
            const glm::mat4 world = entity.HasComponent<TransformComponent>() ? ComputeWorldMatrix(entityHandle) : glm::mat4(1.0f);

            // Parent'ın child listesinden çıkar
            auto& children = m_EntityChildren[parentHandle];
            children.erase(std::remove(children.begin(), children.end(), entityHandle), children.end());

            // Parent ilişkisini sil
            m_EntityParent.erase(it);

            // Dünyadaki konumunu koru (Keep World Transform)
            if (entity.HasComponent<TransformComponent>())
            {
                auto& tc = entity.GetComponent<TransformComponent>();
                glm::vec3 pos, rot, scale;
                Math::DecomposeTransform(world, pos, rot, scale);
                tc.Position = pos;
                tc.Rotation = rot;
                tc.Scale = scale;
            }
        }
    }

    Entity Scene::GetEntityParent(Entity entity) const
    {
        entt::entity entityHandle = entity;
        auto it = m_EntityParent.find(entityHandle);
        if (it != m_EntityParent.end())
            return Entity{ it->second, const_cast<Scene*>(this) };
        
        return Entity{}; // Null entity
    }

    bool Scene::SetPersistent(Entity entity, bool persistent)
    {
        if (!entity.IsValid() || entity.GetScene() != this)
            return false;
        const entt::entity h = entity;
        if (persistent && GetEntityParent(entity))
        {
            // Unity: DontDestroyOnLoad yalnız kök nesnelerde çalışır
            CQ_CORE_WARN("DontDestroyOnLoad only works for root objects ({0} has a parent)", entity.GetComponent<TagComponent>().Tag);
            return false;
        }
        if (persistent)
            m_Persistent.insert(h);
        else
            m_Persistent.erase(h);
        return true;
    }

    bool Scene::IsPersistent(entt::entity entity) const
    {
        return m_Persistent.count(entity) > 0;
    }

    int Scene::AdoptPersistent(Scene& previous)
    {
        if (&previous == this || previous.m_Persistent.empty())
            return 0;
        // Kökler ve tüm altları (ön sıra: ebeveyn önce)
        std::vector<entt::entity> order;
        std::vector<entt::entity> stack;
        for (entt::entity root : previous.m_Persistent)
            if (previous.m_Registry.valid(root))
                stack.push_back(root);
        while (!stack.empty())
        {
            const entt::entity e = stack.back();
            stack.pop_back();
            order.push_back(e);
            if (auto it = previous.m_EntityChildren.find(e); it != previous.m_EntityChildren.end())
                for (auto c = it->second.rbegin(); c != it->second.rend(); ++c)
                    if (previous.m_Registry.valid(*c))
                        stack.push_back(*c);
        }
        int moved = 0;
        for (entt::entity src : order)
        {
            // Aynı tanıtıcı: betik örnekleri kendi nesnelerini (self) bu sayıyla bilir. Sahne henüz boş olduğundan kullanılmıyor
            const entt::entity dst = m_Registry.create(src);
            if (dst != src)
            {
                CQ_CORE_ERROR("AdoptPersistent: entity id {0} is taken in the new scene", (uint32_t)src);
                m_Registry.destroy(dst);
                continue;
            }
            Entity from{ src, &previous }, to{ dst, this };
            to.AddComponent<IDComponent>(from.GetComponent<IDComponent>().ID);
            to.AddComponent<TransformComponent>();
            to.AddComponent<TagComponent>(from.GetComponent<TagComponent>().Tag);
            CopyEntityComponents(from, to, true);
            // Çalışma zamanı bağları taşınır (kopya yeni başlatmasın): betik örnekleri ve çalan ses
            // (önceki sahnedeki bağlar FinishAdoption'da çözülür: yükleme başarısız olursa önceki sahne olduğu gibi kalır)
            if (from.HasComponent<ConquerorScriptComponent>())
                m_Registry.emplace_or_replace<ConquerorScriptComponent>(dst).Scripts = from.GetComponent<ConquerorScriptComponent>().Scripts;
            if (from.HasComponent<AudioSourceComponent>() && to.HasComponent<AudioSourceComponent>())
                to.GetComponent<AudioSourceComponent>().RuntimeSound = from.GetComponent<AudioSourceComponent>().RuntimeSound;
            m_Adopted.push_back(src);
            if (auto it = previous.m_EntityParent.find(src); it != previous.m_EntityParent.end() && !previous.m_Persistent.count(src))
            {
                m_EntityParent[dst] = it->second;
                m_EntityChildren[it->second].push_back(dst);
            }
            if (previous.m_ProcessDisabled.count((uint32_t)src))
                m_ProcessDisabled.insert((uint32_t)dst);
            ++moved;
        }
        for (entt::entity root : previous.m_Persistent)
            if (m_Registry.valid(root))
                m_Persistent.insert(root);
        UpdateWorldTransforms();
        return moved;
    }

    void Scene::FinishAdoption(Scene& previous)
    {
        // Taşınan nesnelerin önceki sahnedeki kopyaları: betik örnekleri ve ses artık yeni sahnenin, durdurulunca yok edilmesin
        for (entt::entity e : m_Adopted)
        {
            if (!previous.m_Registry.valid(e))
                continue;
            if (auto* sc = previous.m_Registry.try_get<ConquerorScriptComponent>(e))
                for (auto& s : sc->Scripts)
                {
                    s.Instance = nullptr;
                    s.Runtime = nullptr;
                    s.bLoaded = false;
                }
            if (auto* a = previous.m_Registry.try_get<AudioSourceComponent>(e))
                a->RuntimeSound = nullptr;
        }
        m_Adopted.clear();
        ResolvePersistentDuplicates();
    }

    void Scene::ResolvePersistentDuplicates()
    {
        if (m_Persistent.empty())
            return;
        // Taşınan nesnenin UUID'si yeni sahnede de varsa (aynı sahne yeniden yüklendi) sahneden gelen kopya yeni bir UUID alır
        std::unordered_set<uint64_t> kept;
        std::vector<entt::entity> stack(m_Persistent.begin(), m_Persistent.end());
        std::unordered_set<entt::entity> persistentTree;
        while (!stack.empty())
        {
            const entt::entity e = stack.back();
            stack.pop_back();
            if (!m_Registry.valid(e) || !persistentTree.insert(e).second)
                continue;
            kept.insert((uint64_t)m_Registry.get<IDComponent>(e).ID);
            if (auto it = m_EntityChildren.find(e); it != m_EntityChildren.end())
                stack.insert(stack.end(), it->second.begin(), it->second.end());
        }
        static std::mt19937_64 rng{ std::random_device{}() };
        for (auto [e, id] : m_Registry.view<IDComponent>().each())
            if (!persistentTree.count(e) && kept.count((uint64_t)id.ID))
                id.ID = rng();
    }

    void Scene::SetProcessDisabled(uint32_t entity, bool disabled)
    {
        if (disabled)
            m_ProcessDisabled.insert(entity);
        else
            m_ProcessDisabled.erase(entity);
    }

    bool Scene::IsProcessDisabled(entt::entity entity) const
    {
        if (m_ProcessDisabled.empty())
            return false;
        entt::entity current = entity;
        for (int depth = 0; depth < 64; ++depth)
        {
            if (m_ProcessDisabled.count((uint32_t)current))
                return true;
            auto it = m_EntityParent.find(current);
            if (it == m_EntityParent.end())
                return false;
            current = it->second;
        }
        return false;
    }

    bool Scene::IsEntityActive(entt::entity entity) const
    {
        // Zincir yukarı yürünür: üstlerden biri kapalıysa çocuk da kapalıdır.
        // (Döngü güvenliği için derinlik sınırlı.)
        entt::entity current = entity;
        for (int depth = 0; depth < 64; ++depth)
        {
            if (!m_Registry.valid(current))
                return false;
            auto active = m_Registry.try_get<ActiveComponent>(current);
            if (active && !active->Active)
                return false;
            auto it = m_EntityParent.find(current);
            if (it == m_EntityParent.end())
                return true;
            current = it->second;
        }
        return true;
    }

    std::vector<Entity> Scene::GetEntityChildren(Entity entity) const
    {
        std::vector<Entity> result;
        entt::entity entityHandle = entity;
        
        auto it = m_EntityChildren.find(entityHandle);
        if (it != m_EntityChildren.end())
        {
            for (auto childHandle : it->second)
            {
                result.push_back(Entity{ childHandle, const_cast<Scene*>(this) });
            }
        }
        
        return result;
    }

    bool Scene::HasChildren(Entity entity) const
    {
        entt::entity entityHandle = entity;
        auto it = m_EntityChildren.find(entityHandle);
        return it != m_EntityChildren.end() && !it->second.empty();
    }

    glm::vec3 Scene::LightForward(const glm::mat4& world)
    {
        glm::vec3 f = glm::mat3(world) * glm::vec3(0.0f, 0.0f, -1.0f);
        return glm::dot(f, f) > 1e-12f ? glm::normalize(f) : glm::vec3(0.0f, -1.0f, 0.0f);
    }

    void Scene::UpdateTransformHierarchy(Entity entity, const glm::mat4& parentTransform)
    {
        if (!entity.HasComponent<TransformComponent>())
            return;

        auto& transform = entity.GetComponent<TransformComponent>();

        // UI öğelerinin (RectTransform) dünya matrisi Transform'dan değil UI yerleşiminden gelir
        // (UISystem::UpdateEditorTransforms). Burada ezilirse, örneğin Game panelinin editördeki önizlemesi
        // her kare sıfırlıyordu ve gizmo ile sürüklenen öğeler fareden kat kat hızlı kaçıyordu.
        if (m_Registry.all_of<RectTransformComponent>(entity) && !m_Registry.all_of<CanvasComponent>(entity))
            return;
        
        // Local transform'u hesapla
        glm::mat4 localTransform = transform.GetTransform();
        
        // World transform = parent * local
        transform.WorldTransform = parentTransform * localTransform;

        // Işık yönü nesnenin dönüşünden gelir (Unity gibi: ışık yerel -Z'ye bakar)
        if (auto* dl = m_Registry.try_get<DirectionalLightComponent>(entity))
            dl->Direction = LightForward(transform.WorldTransform);
        if (auto* sl = m_Registry.try_get<SpotLightComponent>(entity))
            sl->Direction = LightForward(transform.WorldTransform);
        
        // Child'lar için recursive çağır
        auto children = GetEntityChildren(entity);
        for (auto child : children)
        {
            UpdateTransformHierarchy(child, transform.WorldTransform);
        }
    }

    void Scene::UpdateButtonInput(EditorCamera& camera, Timestep ts, const glm::vec2* viewportBounds)
    {
        // Mouse pozisyonunu al
        glm::vec2 mousePos = Input::GetMousePosition();

        // Viewport bounds kontrolü
        if (viewportBounds)
        {
            // Mouse viewport içinde mi?
            if (mousePos.x < viewportBounds[0].x || mousePos.x > viewportBounds[1].x ||
                mousePos.y < viewportBounds[0].y || mousePos.y > viewportBounds[1].y)
            {
                // Mouse viewport dışında, butonları deaktif et
                auto buttonView = m_Registry.view<ButtonComponent>();
                for (auto entity : buttonView)
                {
                    auto& button = buttonView.get<ButtonComponent>(entity);
                    button.IsHovered = false;
                    button.IsPressed = false;
                    button.CurrentState = button.Interactable ? ButtonComponent::State::Normal : ButtonComponent::State::Disabled;
                }
                return;
            }
            
        }
        
        // Klavye navigation input
        static bool wasUpPressed = false;
        static bool wasDownPressed = false;
        static bool wasLeftPressed = false;
        static bool wasRightPressed = false;
        static bool wasEnterPressed = false;
        static bool wasSpacePressed = false;
        
        bool upPressed = Input::IsKeyPressed(Key::Up) || Input::IsKeyPressed(Key::W);
        bool downPressed = Input::IsKeyPressed(Key::Down) || Input::IsKeyPressed(Key::S);
        bool leftPressed = Input::IsKeyPressed(Key::Left) || Input::IsKeyPressed(Key::A);
        bool rightPressed = Input::IsKeyPressed(Key::Right) || Input::IsKeyPressed(Key::D);
        bool enterPressed = Input::IsKeyPressed(Key::Enter);
        bool spacePressed = Input::IsKeyPressed(Key::Space);
        
        bool upClicked = upPressed && !wasUpPressed;
        bool downClicked = downPressed && !wasDownPressed;
        bool leftClicked = leftPressed && !wasLeftPressed;
        bool rightClicked = rightPressed && !wasRightPressed;
        bool enterClicked = enterPressed && !wasEnterPressed;
        bool spaceClicked = spacePressed && !wasSpacePressed;
        
        wasUpPressed = upPressed;
        wasDownPressed = downPressed;
        wasLeftPressed = leftPressed;
        wasRightPressed = rightPressed;
        wasEnterPressed = enterPressed;
        wasSpacePressed = spacePressed;
        
        // Seçili buton varsa Enter/Space ile tetikle
        if ((enterClicked || spaceClicked) && m_SelectedButton != entt::null)
        {
            if (m_Registry.valid(m_SelectedButton) && m_Registry.all_of<ButtonComponent>(m_SelectedButton))
            {
                auto& selectedBtn = m_Registry.get<ButtonComponent>(m_SelectedButton);
                if (selectedBtn.Interactable && selectedBtn.OnClick)
                {
                    selectedBtn.OnClick();
                }
            }
        }
        
        // Navigation (arrow keys ile buton değiştir)
        if (upClicked || downClicked || leftClicked || rightClicked)
        {
            Entity nextButton = FindNextButton(upClicked, downClicked, leftClicked, rightClicked);
            if (nextButton)
            {
                m_SelectedButton = nextButton;
            }
        }
        
        // Screen space'den world space'e dönüştür (Ray casting ile)
        glm::mat4 viewProj = camera.GetViewProjection();
        glm::mat4 invViewProj = glm::inverse(viewProj);
        
        // NDC koordinatlarına dönüştür
        glm::vec2 ndc;
        ndc.x = (2.0f * mousePos.x) / m_ViewportWidth - 1.0f;
        ndc.y = 1.0f - (2.0f * mousePos.y) / m_ViewportHeight; // Y ters
        
        // Ray'in başlangıç ve bitiş noktalarını hesapla (near ve far plane)
        glm::vec4 rayStartNDC = glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f); // Near plane
        glm::vec4 rayEndNDC = glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);    // Far plane
        
        glm::vec4 rayStartWorld = invViewProj * rayStartNDC;
        glm::vec4 rayEndWorld = invViewProj * rayEndNDC;
        
        rayStartWorld /= rayStartWorld.w;
        rayEndWorld /= rayEndWorld.w;
        
        // Ray direction
        glm::vec3 rayDir = glm::normalize(glm::vec3(rayEndWorld - rayStartWorld));
        glm::vec3 rayOrigin = glm::vec3(rayStartWorld);
        
        // Ray'in z=0 plane ile kesişimini bul
        // Ray: P = rayOrigin + t * rayDir
        // Plane: z = 0
        // Çözüm: t = -rayOrigin.z / rayDir.z
        float t = -rayOrigin.z / rayDir.z;
        glm::vec3 intersectionPoint = rayOrigin + t * rayDir;
        
        glm::vec2 mouseWorldPos = glm::vec2(intersectionPoint.x, intersectionPoint.y);
        
        // Mouse button durumu
        bool mousePressed = Input::IsMouseButtonPressed(Mouse::ButtonLeft);
        static bool wasPressed = false;
        bool mouseClicked = mousePressed && !wasPressed;
        wasPressed = mousePressed;
        
        // Tüm butonları tara
        auto buttonView = m_Registry.view<TransformComponent, ButtonComponent, SpriteRendererComponent>();
        for (auto entity : buttonView)
        {
            auto [transform, button, image] = buttonView.get<TransformComponent, ButtonComponent, SpriteRendererComponent>(entity);
            
            if (!button.Interactable)
            {
                button.CurrentState = ButtonComponent::State::Disabled;
                button.IsHovered = false;
                button.IsPressed = false;
                
                // Disabled için hedef renk
                glm::vec4 targetBackgroundColor;
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundDisabledColor * button.ColorMultiplier;
                }
                else
                {
                    targetBackgroundColor = image.Color * 0.5f * button.ColorMultiplier;
                    targetBackgroundColor.a = image.Color.a * 0.5f;
                }
                
                // Smooth transition
                float lerpFactor = glm::min(1.0f, button.TransitionSpeed * ts);
                button.CurrentBackgroundColor = glm::mix(button.CurrentBackgroundColor, targetBackgroundColor, lerpFactor);
                
                continue;
            }
            
            // Highlighted state (en yüksek öncelik, interactable olsa bile)
            if (button.IsHighlighted)
            {
                button.CurrentState = ButtonComponent::State::Highlighted;
                
                glm::vec4 targetBackgroundColor;
                glm::vec4 targetTextColor;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundHighlightedColor * button.ColorMultiplier;
                    targetTextColor = button.TextHighlightedColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * 1.5f * button.ColorMultiplier;
                    targetBackgroundColor.a = image.Color.a;
                    targetTextColor = glm::vec4(1.0f, 1.0f, 0.4f, 1.0f);
                }
                
                // Smooth transition
                float lerpFactor = glm::min(1.0f, button.TransitionSpeed * ts);
                button.CurrentBackgroundColor = glm::mix(button.CurrentBackgroundColor, targetBackgroundColor, lerpFactor);
                button.CurrentTextColor = glm::mix(button.CurrentTextColor, targetTextColor, lerpFactor);
                
                continue;
            }
            



            // AABB bounds hesapla (Sprite Renderer dokusunun boyutundan)
            if (!image.Texture)
                continue;
            
            // Rendering ile aynı hesaplama (piksel -> dünya birimi)
            float halfWidth = (float)image.Texture->GetWidth() / 200.0f;
            float halfHeight = (float)image.Texture->GetHeight() / 200.0f;


            
            // Vertex pozisyonlarını rendering ile aynı şekilde hesapla
            glm::vec4 vertexPositions[4] = {
                { -halfWidth, -halfHeight, 0.0f, 1.0f },
                {  halfWidth, -halfHeight, 0.0f, 1.0f },
                {  halfWidth,  halfHeight, 0.0f, 1.0f },
                { -halfWidth,  halfHeight, 0.0f, 1.0f }
            };
            
            // Transform'u uygula (rendering ile aynı)
            glm::vec4 transformedVerts[4];
            for (int i = 0; i < 4; i++)
            {
                transformedVerts[i] = transform.WorldTransform * vertexPositions[i];
            }


            // Offset düzeltmesi: Sol-üste kaydır
            float offsetMultiplierX = -3.32f; // X için çarpan (sola/sağa)
            float offsetMultiplierY = 1.72f; // Y için çarpan (yukarı/aşağı)
            glm::vec2 offset = glm::vec2(-halfWidth * offsetMultiplierX, -halfHeight * offsetMultiplierY);
            for (int i = 0; i < 4; i++)
            {
                transformedVerts[i].x += offset.x;
                transformedVerts[i].y += offset.y;
            }
            
            // AABB bounds (transformed vertex'lerden min/max bul)
            glm::vec2 min = glm::vec2(transformedVerts[0].x, transformedVerts[0].y);
            glm::vec2 max = min;
            
            for (int i = 1; i < 4; i++)
            {
                min.x = glm::min(min.x, transformedVerts[i].x);
                min.y = glm::min(min.y, transformedVerts[i].y);
                max.x = glm::max(max.x, transformedVerts[i].x);
                max.y = glm::max(max.y, transformedVerts[i].y);
            }
            
            // Mouse bounds içinde mi?
            bool isInside = mouseWorldPos.x >= min.x && mouseWorldPos.x <= max.x &&
                           mouseWorldPos.y >= min.y && mouseWorldPos.y <= max.y;
            
            // GraphicRaycaster kontrolü
            if (isInside)
            {
                Entity canvasEntity = Entity{ entity, this };
                while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                {
                    canvasEntity = GetEntityParent(canvasEntity);
                }
                
                if (canvasEntity && canvasEntity.HasComponent<GraphicRaycasterComponent>())
                {
                    auto& raycaster = canvasEntity.GetComponent<GraphicRaycasterComponent>();
                    
                    // Blocking Objects kontrolü
                    if (raycaster.IgnoreReversedGraphics)
                    {
                        glm::vec3 scale = glm::vec3(transform.WorldTransform[0][0], 
                                                    transform.WorldTransform[1][1], 
                                                    transform.WorldTransform[2][2]);
                        
                        if (scale.x < 0.0f || scale.y < 0.0f)
                        {
                            isInside = false;
                        }
                    }
                    
                    if (isInside && raycaster.Blocking != GraphicRaycasterComponent::BlockingObjects::None)
                    {
                        // Mouse tıklandığında blocking durumunu sıfırla
                        if (mouseClicked)
                        {
                            button.IsBlockedByDepth = false;
                        }
                        
                        // Butonun depth'ini hesapla (TÜM MODLAR İÇİN)
                        glm::vec4 buttonWorldPos = transform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                        glm::vec4 buttonClipPos = viewProj * buttonWorldPos;
                        float buttonDepth = (buttonClipPos.z / buttonClipPos.w) * 0.5f + 0.5f;
                        
                        CQ_CORE_INFO("--- Blocking Check ---");
                        CQ_CORE_INFO("Blocking Mode: {0}", (int)raycaster.Blocking);
                        CQ_CORE_INFO("BlockingMask: 0x{0:X}", raycaster.BlockingMask);
                        CQ_CORE_INFO("buttonDepth: {0}", buttonDepth);
                        
                        // TÜM mesh'lerin depth'ini hesapla ve logla
                        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent, LayerComponent>();
                        int meshIdx = 0;
                        for (auto meshEntity : meshView)
                        {
                            auto [meshTransform, meshRenderer, meshLayer] = 
                                meshView.get<TransformComponent, MeshRendererComponent, LayerComponent>(meshEntity);
                            
                            glm::vec3 meshPos = glm::vec3(meshTransform.WorldTransform[3]);
                            glm::vec3 meshScale = glm::vec3(
                                glm::length(glm::vec3(meshTransform.WorldTransform[0])),
                                glm::length(glm::vec3(meshTransform.WorldTransform[1])),
                                glm::length(glm::vec3(meshTransform.WorldTransform[2]))
                            );
                            
                            glm::vec4 meshWorldPos = meshTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                            glm::vec4 meshClipPos = viewProj * meshWorldPos;
                            float meshDepth = (meshClipPos.z / meshClipPos.w) * 0.5f + 0.5f;
                            
                            CQ_CORE_INFO("Mesh #{0}:", meshIdx);
                            CQ_CORE_INFO("  Pos: ({0}, {1}, {2})", meshPos.x, meshPos.y, meshPos.z);
                            CQ_CORE_INFO("  Scale: ({0}, {1}, {2})", meshScale.x, meshScale.y, meshScale.z);
                            CQ_CORE_INFO("  Layer: {0}", meshLayer.Layer);
                            CQ_CORE_INFO("  Depth: {0}", meshDepth);
                            CQ_CORE_INFO("  In Mask: {0}", (raycaster.BlockingMask & (1 << meshLayer.Layer)) != 0);
                            
                            meshIdx++;
                        }
                        
                        if (raycaster.Blocking == GraphicRaycasterComponent::BlockingObjects::All)
                        {
                            // Depth buffer oku (HER FRAME)
                            float mouseDepth;
                            int screenX = (int)mousePos.x;
                            int screenY = (int)(m_ViewportHeight - mousePos.y);
                            glReadPixels(screenX, screenY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mouseDepth);
                            
                            // Butonun depth'ini hesapla
                            glm::vec4 buttonWorldPos = transform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                            glm::vec4 buttonClipPos = viewProj * buttonWorldPos;
                            float buttonDepth = (buttonClipPos.z / buttonClipPos.w) * 0.5f + 0.5f;
                            
                            const float epsilon = 0.0000001f; // Maksimum hassasiyet
                            if (mouseDepth < buttonDepth - epsilon)
                            {
                                // Önde bir şey var, hem 2D hem 3D objeleri kontrol et
                                bool shouldBlock = false;
                                
                                // 2D sprite kontrolü
                                auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent, LayerComponent>();
                                for (auto spriteEntity : spriteView)
                                {
                                    auto [spriteTransform, spriteRenderer, spriteLayer] = 
                                        spriteView.get<TransformComponent, SpriteRendererComponent, LayerComponent>(spriteEntity);
                                    
                                    // Layer mask kontrolü
                                    if (!(raycaster.BlockingMask & (1 << spriteLayer.Layer)))
                                        continue;
                                    
                                    if (!spriteRenderer.Texture)
                                        continue;
                                    
                                    // AABB bounds hesapla
                                    float spriteHalfWidth = (float)spriteRenderer.Texture->GetWidth() / 200.0f;
                                    float spriteHalfHeight = (float)spriteRenderer.Texture->GetHeight() / 200.0f;
                                    
                                    glm::vec4 spriteVertexPositions[4] = {
                                        { -spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                        {  spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                        {  spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f },
                                        { -spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f }
                                    };
                                    
                                    glm::vec4 spriteTransformedVerts[4];
                                    for (int i = 0; i < 4; i++)
                                    {
                                        spriteTransformedVerts[i] = spriteTransform.WorldTransform * spriteVertexPositions[i];
                                    }
                                    
                                    glm::vec2 spriteMin = glm::vec2(spriteTransformedVerts[0].x, spriteTransformedVerts[0].y);
                                    glm::vec2 spriteMax = spriteMin;
                                    
                                    for (int i = 1; i < 4; i++)
                                    {
                                        spriteMin.x = glm::min(spriteMin.x, spriteTransformedVerts[i].x);
                                        spriteMin.y = glm::min(spriteMin.y, spriteTransformedVerts[i].y);
                                        spriteMax.x = glm::max(spriteMax.x, spriteTransformedVerts[i].x);
                                        spriteMax.y = glm::max(spriteMax.y, spriteTransformedVerts[i].y);
                                    }
                                    
                                    bool spriteContainsMouse = mouseWorldPos.x >= spriteMin.x && mouseWorldPos.x <= spriteMax.x &&
                                                               mouseWorldPos.y >= spriteMin.y && mouseWorldPos.y <= spriteMax.y;
                                    
                                    if (!spriteContainsMouse)
                                        continue;
                                    
                                    // Depth kontrolü
                                    glm::vec4 spriteWorldPos = spriteTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                    glm::vec4 spriteClipPos = viewProj * spriteWorldPos;
                                    float spriteDepth = (spriteClipPos.z / spriteClipPos.w) * 0.5f + 0.5f;
                                    
                                    if (spriteDepth < buttonDepth - epsilon)
                                    {
                                        shouldBlock = true;
                                        break;
                                    }
                                }
                                
                                // 3D mesh kontrolü (sprite bulunamadıysa)
                                if (!shouldBlock)
                                {
                                    auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent, LayerComponent>();
                                    for (auto meshEntity : meshView)
                                    {
                                        auto [meshTransform, meshRenderer, meshLayer] = 
                                            meshView.get<TransformComponent, MeshRendererComponent, LayerComponent>(meshEntity);
                                        
                                        // Layer mask kontrolü
                                        if (!(raycaster.BlockingMask & (1 << meshLayer.Layer)))
                                            continue;
                                        
                                        // AABB bounds hesapla
                                        float meshHalfSize = 0.5f;
                                        
                                        glm::vec4 meshVertexPositions[8] = {
                                            { -meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                            {  meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                            {  meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                            { -meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                            { -meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                            {  meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                            {  meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f },
                                            { -meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f }
                                        };
                                        
                                        glm::vec4 meshTransformedVerts[8];
                                        for (int i = 0; i < 8; i++)
                                        {
                                            meshTransformedVerts[i] = meshTransform.WorldTransform * meshVertexPositions[i];
                                        }
                                        
                                        glm::vec3 meshMin = glm::vec3(meshTransformedVerts[0].x, meshTransformedVerts[0].y, meshTransformedVerts[0].z);
                                        glm::vec3 meshMax = meshMin;
                                        
                                        for (int i = 1; i < 8; i++)
                                        {
                                            meshMin.x = glm::min(meshMin.x, meshTransformedVerts[i].x);
                                            meshMin.y = glm::min(meshMin.y, meshTransformedVerts[i].y);
                                            meshMin.z = glm::min(meshMin.z, meshTransformedVerts[i].z);
                                            meshMax.x = glm::max(meshMax.x, meshTransformedVerts[i].x);
                                            meshMax.y = glm::max(meshMax.y, meshTransformedVerts[i].y);
                                            meshMax.z = glm::max(meshMax.z, meshTransformedVerts[i].z);
                                        }
                                        
                                        bool meshContainsMouse = mouseWorldPos.x >= meshMin.x && mouseWorldPos.x <= meshMax.x &&
                                                                 mouseWorldPos.y >= meshMin.y && mouseWorldPos.y <= meshMax.y;
                                        
                                        if (!meshContainsMouse)
                                            continue;
                                        
                                        // Ray-AABB intersection
                                        float tMin = (meshMin.z - rayOrigin.z) / rayDir.z;
                                        float tMax = (meshMax.z - rayOrigin.z) / rayDir.z;
                                        
                                        if (tMin > tMax) std::swap(tMin, tMax);
                                        
                                        if (tMax < 0.0f || tMin > 1000.0f)
                                            continue;
                                        
                                        // Depth kontrolü
                                        glm::vec4 meshWorldPos = meshTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                        glm::vec4 meshClipPos = viewProj * meshWorldPos;
                                        float meshDepth = (meshClipPos.z / meshClipPos.w) * 0.5f + 0.5f;
                                        
                                        if (meshDepth < buttonDepth - epsilon)
                                        {
                                            shouldBlock = true;
                                            break;
                                        }
                                    }
                                }
                                
                                if (shouldBlock)
                                {
                                    button.IsBlockedByDepth = true;
                                    isInside = false;
                                }
                            }
                        }
                        else
                        {
                            // Depth buffer oku (HER FRAME)
                            float mouseDepth;
                            int screenX = (int)mousePos.x;
                            int screenY = (int)(m_ViewportHeight - mousePos.y);
                            glReadPixels(screenX, screenY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mouseDepth);
                            
                            // Butonun depth'ini hesapla
                            glm::vec4 buttonWorldPos = transform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                            glm::vec4 buttonClipPos = viewProj * buttonWorldPos;
                            float buttonDepth = (buttonClipPos.z / buttonClipPos.w) * 0.5f + 0.5f;
                            
                            if (mouseClicked)
                            {
                                CQ_CORE_INFO("Blocking check - mouseDepth: {0}, buttonDepth: {1}", mouseDepth, buttonDepth);
                            }
                            
                            const float epsilon = 0.0000001f; // Maksimum hassasiyet
                            if (mouseDepth < buttonDepth - epsilon)
                            {
                                // Önde bir şey var, şimdi mouse pozisyonundaki objeleri kontrol et
                                bool shouldBlock = false;
                                
                                if (raycaster.Blocking == GraphicRaycasterComponent::BlockingObjects::TwoD)
                                {
                                    // 2D sprite kontrolü - SADECE mouse pozisyonundaki sprite'lar
                                    auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent, LayerComponent>();
                                    for (auto spriteEntity : spriteView)
                                    {
                                        auto [spriteTransform, spriteRenderer, spriteLayer] = 
                                            spriteView.get<TransformComponent, SpriteRendererComponent, LayerComponent>(spriteEntity);
                                        
                                        // Layer mask kontrolü - bu sprite'ın layer'ı mask'te var mı?
                                        if (!(raycaster.BlockingMask & (1 << spriteLayer.Layer)))
                                            continue; // Bu layer mask'te yok, atla
                                        
                                        // Sprite'ın texture'ı var mı?
                                        if (!spriteRenderer.Texture)
                                            continue;
                                        
                                        // Sprite'ın AABB bounds'unu hesapla (rendering ile aynı mantık)
                                        float spriteHalfWidth = (float)spriteRenderer.Texture->GetWidth() / 200.0f;
                                        float spriteHalfHeight = (float)spriteRenderer.Texture->GetHeight() / 200.0f;
                                        
                                        // Vertex pozisyonları (local space)
                                        glm::vec4 spriteVertexPositions[4] = {
                                            { -spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                            {  spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                            {  spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f },
                                            { -spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f }
                                        };
                                        
                                        // Transform uygula (world space'e çevir)
                                        glm::vec4 spriteTransformedVerts[4];
                                        for (int i = 0; i < 4; i++)
                                        {
                                            spriteTransformedVerts[i] = spriteTransform.WorldTransform * spriteVertexPositions[i];
                                        }
                                        
                                        // AABB bounds hesapla (min/max)
                                        glm::vec2 spriteMin = glm::vec2(spriteTransformedVerts[0].x, spriteTransformedVerts[0].y);
                                        glm::vec2 spriteMax = spriteMin;
                                        
                                        for (int i = 1; i < 4; i++)
                                        {
                                            spriteMin.x = glm::min(spriteMin.x, spriteTransformedVerts[i].x);
                                            spriteMin.y = glm::min(spriteMin.y, spriteTransformedVerts[i].y);
                                            spriteMax.x = glm::max(spriteMax.x, spriteTransformedVerts[i].x);
                                            spriteMax.y = glm::max(spriteMax.y, spriteTransformedVerts[i].y);
                                        }
                                        
                                        // Mouse sprite bounds içinde mi?
                                        bool spriteContainsMouse = mouseWorldPos.x >= spriteMin.x && mouseWorldPos.x <= spriteMax.x &&
                                                                   mouseWorldPos.y >= spriteMin.y && mouseWorldPos.y <= spriteMax.y;
                                        
                                        if (!spriteContainsMouse)
                                            continue; // Mouse bu sprite'ın üzerinde değil
                                        
                                        // Sprite'ın depth'ini hesapla
                                        glm::vec4 spriteWorldPos = spriteTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                        glm::vec4 spriteClipPos = viewProj * spriteWorldPos;
                                        float spriteDepth = (spriteClipPos.z / spriteClipPos.w) * 0.5f + 0.5f;
                                        
                                        // Sprite butonun önünde mi? (depth karşılaştırması)
                                        if (spriteDepth < buttonDepth - epsilon)
                                        {
                                            shouldBlock = true;
                                            break;
                                        }
                                    }
                                }
                                else if (raycaster.Blocking == GraphicRaycasterComponent::BlockingObjects::ThreeD)
                                {
                                    CQ_CORE_INFO("ThreeD mode - Checking meshes...");
                                    
                                    // Butonun AABB bounds'unu hesapla
                                    float buttonHalfWidth = (float)image.Texture->GetWidth() / 200.0f;
                                    float buttonHalfHeight = (float)image.Texture->GetHeight() / 200.0f;
                                    
                                    glm::vec4 buttonVertexPositions[4] = {
                                        { -buttonHalfWidth, -buttonHalfHeight, 0.0f, 1.0f },
                                        {  buttonHalfWidth, -buttonHalfHeight, 0.0f, 1.0f },
                                        {  buttonHalfWidth,  buttonHalfHeight, 0.0f, 1.0f },
                                        { -buttonHalfWidth,  buttonHalfHeight, 0.0f, 1.0f }
                                    };
                                    
                                    glm::vec4 buttonTransformedVerts[4];
                                    for (int i = 0; i < 4; i++)
                                    {
                                        buttonTransformedVerts[i] = transform.WorldTransform * buttonVertexPositions[i];
                                    }
                                    
                                    // Buton AABB
                                    glm::vec2 buttonMin = glm::vec2(buttonTransformedVerts[0].x, buttonTransformedVerts[0].y);
                                    glm::vec2 buttonMax = buttonMin;
                                    
                                    for (int i = 1; i < 4; i++)
                                    {
                                        buttonMin.x = glm::min(buttonMin.x, buttonTransformedVerts[i].x);
                                        buttonMin.y = glm::min(buttonMin.y, buttonTransformedVerts[i].y);
                                        buttonMax.x = glm::max(buttonMax.x, buttonTransformedVerts[i].x);
                                        buttonMax.y = glm::max(buttonMax.y, buttonTransformedVerts[i].y);
                                    }
                                    
                                    CQ_CORE_INFO("  Button AABB: ({0}, {1}) to ({2}, {3})", buttonMin.x, buttonMin.y, buttonMax.x, buttonMax.y);
                                    
                                    // 3D mesh kontrolü
                                    auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent, LayerComponent>();
                                    for (auto meshEntity : meshView)
                                    {
                                        auto [meshTransform, meshRenderer, meshLayer] = 
                                            meshView.get<TransformComponent, MeshRendererComponent, LayerComponent>(meshEntity);
                                        
                                        // Layer mask kontrolü
                                        if (!(raycaster.BlockingMask & (1 << meshLayer.Layer)))
                                        {
                                            CQ_CORE_INFO("  Mesh layer {0} NOT in mask", meshLayer.Layer);
                                            continue;
                                        }
                                        
                                        CQ_CORE_INFO("  Mesh layer {0} in mask, checking depth...", meshLayer.Layer);
                                        
                                        // Mesh'in depth'ini hesapla
                                        glm::vec4 meshWorldPos = meshTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                        glm::vec4 meshClipPos = viewProj * meshWorldPos;
                                        float meshDepth = (meshClipPos.z / meshClipPos.w) * 0.5f + 0.5f;
                                        
                                        CQ_CORE_INFO("  meshDepth: {0}, buttonDepth: {1}", meshDepth, buttonDepth);
                                        
                                        // Mesh butonun önünde mi?
                                        if (meshDepth < buttonDepth - epsilon)
                                        {
                                            CQ_CORE_INFO("  Mesh is IN FRONT of button, checking overlap...");
                                            
                                            // Mesh AABB hesapla
                                            float meshHalfSize = 0.5f;
                                            
                                            glm::vec4 meshVertexPositions[8] = {
                                                { -meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                                {  meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                                {  meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                                { -meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                                { -meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                                {  meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                                {  meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f },
                                                { -meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f }
                                            };
                                            
                                            glm::vec4 meshTransformedVerts[8];
                                            for (int i = 0; i < 8; i++)
                                            {
                                                meshTransformedVerts[i] = meshTransform.WorldTransform * meshVertexPositions[i];
                                            }
                                            
                                            // Mesh AABB
                                            glm::vec2 meshMin = glm::vec2(meshTransformedVerts[0].x, meshTransformedVerts[0].y);
                                            glm::vec2 meshMax = meshMin;
                                            
                                            for (int i = 1; i < 8; i++)
                                            {
                                                meshMin.x = glm::min(meshMin.x, meshTransformedVerts[i].x);
                                                meshMin.y = glm::min(meshMin.y, meshTransformedVerts[i].y);
                                                meshMax.x = glm::max(meshMax.x, meshTransformedVerts[i].x);
                                                meshMax.y = glm::max(meshMax.y, meshTransformedVerts[i].y);
                                            }
                                            
                                            CQ_CORE_INFO("  Mesh AABB: ({0}, {1}) to ({2}, {3})", meshMin.x, meshMin.y, meshMax.x, meshMax.y);
                                            
                                            // AABB overlap kontrolü (2D)
                                            bool overlaps = !(buttonMax.x < meshMin.x || buttonMin.x > meshMax.x ||
                                                             buttonMax.y < meshMin.y || buttonMin.y > meshMax.y);
                                            
                                            CQ_CORE_INFO("  Button and Mesh overlap: {0}", overlaps);
                                            
                                            if (overlaps)
                                            {
                                                // Mouse mesh'in içinde mi kontrol et
                                                bool mouseInMesh = mouseWorldPos.x >= meshMin.x && mouseWorldPos.x <= meshMax.x &&
                                                                   mouseWorldPos.y >= meshMin.y && mouseWorldPos.y <= meshMax.y;
                                                
                                                CQ_CORE_INFO("  Mouse in mesh: {0}", mouseInMesh);
                                                
                                                if (mouseInMesh)
                                                {
                                                    CQ_CORE_INFO("  BLOCKING! Mouse is in overlapping area");
                                                    shouldBlock = true;
                                                    break;
                                                }
                                                else
                                                {
                                                    CQ_CORE_INFO("  Overlap exists but mouse NOT in mesh area, not blocking");
                                                }
                                            }
                                            else
                                            {
                                                CQ_CORE_INFO("  Mesh is in front but NOT overlapping, not blocking");
                                            }
                                        }
                                        else
                                        {
                                            CQ_CORE_INFO("  Mesh is BEHIND button, not blocking");
                                        }
                                    }
                                }
                                
                                if (shouldBlock)
                                {
                                    button.IsBlockedByDepth = true;
                                    isInside = false;
                                }
                            }
                        }
                    }
                }
            }
            
            // State güncelle
            button.IsHovered = isInside && !button.IsBlockedByDepth;
            
            // Blocking kontrolünden sonra isInside'ı güncelle
            if (button.IsBlockedByDepth)
            {
                isInside = false;
            }
            
            // Debug log: Butona tıklandığında detaylı bilgi
            if (mouseClicked && isInside)
            {
                // Buton bilgileri
                glm::vec3 buttonPos = glm::vec3(transform.WorldTransform[3]);
                glm::vec3 buttonScale = glm::vec3(
                    glm::length(glm::vec3(transform.WorldTransform[0])),
                    glm::length(glm::vec3(transform.WorldTransform[1])),
                    glm::length(glm::vec3(transform.WorldTransform[2]))
                );
                
                CQ_CORE_INFO("=== BUTTON CLICK DEBUG ===");
                CQ_CORE_INFO("isInside: {0}", isInside);
                CQ_CORE_INFO("Mouse World Pos: ({0}, {1})", mouseWorldPos.x, mouseWorldPos.y);
                CQ_CORE_INFO("Button Pos: ({0}, {1}, {2})", buttonPos.x, buttonPos.y, buttonPos.z);
                CQ_CORE_INFO("Button Scale: ({0}, {1}, {2})", buttonScale.x, buttonScale.y, buttonScale.z);
                
                // Cube bilgileri (eğer varsa)
                auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
                for (auto meshEntity : meshView)
                {
                    auto& meshTransform = meshView.get<TransformComponent>(meshEntity);
                    
                    glm::vec3 cubePos = glm::vec3(meshTransform.WorldTransform[3]);
                    glm::vec3 cubeScale = glm::vec3(
                        glm::length(glm::vec3(meshTransform.WorldTransform[0])),
                        glm::length(glm::vec3(meshTransform.WorldTransform[1])),
                        glm::length(glm::vec3(meshTransform.WorldTransform[2]))
                    );
                    
                    CQ_CORE_INFO("Cube Pos: ({0}, {1}, {2})", cubePos.x, cubePos.y, cubePos.z);
                    CQ_CORE_INFO("Cube Scale: ({0}, {1}, {2})", cubeScale.x, cubeScale.y, cubeScale.z);
                }
                CQ_CORE_INFO("=========================");
            }
            
            // Blocked ise pressed olamaz
            if (button.IsBlockedByDepth && button.IsPressed)
            {
                button.IsPressed = false;
            }
            
            // Klavye ile seçili buton mu?
            bool isSelected = (entity == m_SelectedButton);
            
            // Hedef renkleri belirle
            glm::vec4 targetBackgroundColor;
            glm::vec4 targetTextColor;
            
            // Blocked ise normal state
            if (button.IsBlockedByDepth)
            {
                button.CurrentState = ButtonComponent::State::Normal;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundNormalColor * button.ColorMultiplier;
                    targetTextColor = button.TextNormalColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * button.ColorMultiplier;
                    targetTextColor = glm::vec4(1.0f);
                }
            }
            // Klavye ile seçili ise ve mouse hover yoksa selected göster
            else if (isSelected && !button.IsHighlighted && !isInside)
            {
                button.CurrentState = ButtonComponent::State::Hover;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundHoverColor * button.ColorMultiplier;
                    targetTextColor = button.TextHoverColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * 1.2f * button.ColorMultiplier;
                    targetBackgroundColor.a = image.Color.a;
                    targetTextColor = glm::vec4(1.0f, 1.0f, 0.8f, 1.0f);
                }
            }
            else if (isInside)
            {
                if (mousePressed)
                {
                    button.IsPressed = true;
                    button.CurrentState = ButtonComponent::State::Pressed;
                    
                    if (button.ShowColorDetails)
                    {
                        targetBackgroundColor = button.BackgroundPressedColor * button.ColorMultiplier;
                        targetTextColor = button.TextPressedColor;
                    }
                    else
                    {
                        targetBackgroundColor = image.Color * 0.7f * button.ColorMultiplier;
                        targetBackgroundColor.a = image.Color.a;
                        targetTextColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
                    }
                }
                else
                {
                    if (button.IsPressed && mouseClicked == false) // Mouse bırakıldı
                    {
                        // OnClick callback tetikle
                        if (button.OnClick)
                            button.OnClick();
                    }
                    
                    button.IsPressed = false;
                    button.CurrentState = ButtonComponent::State::Hover;
                    
                    if (button.ShowColorDetails)
                    {
                        targetBackgroundColor = button.BackgroundHoverColor * button.ColorMultiplier;
                        targetTextColor = button.TextHoverColor;
                    }
                    else
                    {
                        targetBackgroundColor = image.Color * 1.2f * button.ColorMultiplier;
                        targetBackgroundColor.a = image.Color.a;
                        targetTextColor = glm::vec4(1.0f, 1.0f, 0.9f, 1.0f);
                    }
                }
            }
            else
            {
                button.IsPressed = false;
                button.CurrentState = ButtonComponent::State::Normal;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundNormalColor * button.ColorMultiplier;
                    targetTextColor = button.TextNormalColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * button.ColorMultiplier;
                    targetTextColor = glm::vec4(1.0f);
                }
            }
            
            // Smooth transition
            float lerpFactor = glm::min(1.0f, button.TransitionSpeed * ts);
            button.CurrentBackgroundColor = glm::mix(button.CurrentBackgroundColor, targetBackgroundColor, lerpFactor);
            button.CurrentTextColor = glm::mix(button.CurrentTextColor, targetTextColor, lerpFactor);
        }
    }

    bool Scene::IsCanvasOrCanvasChild(Entity entity) const
    {
        if (!entity)
            return false;
        
        // Kendisi Canvas mı?
        if (m_Registry.all_of<CanvasComponent>((entt::entity)entity))
            return true;
        
        // Parent'ı Canvas mı? (recursive)
        Entity parent = GetEntityParent(entity);
        if (parent)
            return IsCanvasOrCanvasChild(parent);
        
        return false;
    }

    Entity Scene::FindNextButton(bool up, bool down, bool left, bool right)
    {
        // Seçili buton yoksa ilk interactable butonu seç
        if (m_SelectedButton == entt::null || !m_Registry.valid(m_SelectedButton))
        {
            auto buttonView = m_Registry.view<ButtonComponent, TransformComponent>();
            for (auto entity : buttonView)
            {
                auto& button = buttonView.get<ButtonComponent>(entity);
                if (button.Interactable)
                {
                    return Entity{ entity, this };
                }
            }
            return Entity{};
        }
        
        auto& currentButton = m_Registry.get<ButtonComponent>(m_SelectedButton);
        auto& currentTransform = m_Registry.get<TransformComponent>(m_SelectedButton);
        
        // Explicit mode: Manuel olarak belirlenmiş butonlara git
        if (currentButton.Navigation == ButtonComponent::NavigationMode::Explicit)
        {
            entt::entity targetEntity = entt::null;
            
            // Her yön için ayrı kontrol (else if yerine if kullan)
            if (up) targetEntity = currentButton.NavigateUp;
            if (down) targetEntity = currentButton.NavigateDown;
            if (left) targetEntity = currentButton.NavigateLeft;
            if (right) targetEntity = currentButton.NavigateRight;
            
            // Hedef entity geçerli mi kontrol et
            if (targetEntity != entt::null && m_Registry.valid(targetEntity))
            {
                // Hedef buton interactable mı kontrol et
                if (m_Registry.all_of<ButtonComponent>(targetEntity))
                {
                    auto& targetButton = m_Registry.get<ButtonComponent>(targetEntity);
                    if (targetButton.Interactable)
                    {
                        return Entity{ targetEntity, this };
                    }
                }
            }
            
            // Hedef bulunamadı veya geçersiz, mevcut butonda kal
            return Entity{};
        }
        
        // None mode: Navigation kapalı
        if (currentButton.Navigation == ButtonComponent::NavigationMode::None)
        {
            return Entity{};
        }
        
        // Horizontal mode: Sadece yatay
        if (currentButton.Navigation == ButtonComponent::NavigationMode::Horizontal)
        {
            if (up || down) return Entity{}; // Dikey hareket yasak
            if (!left && !right) return Entity{};
        }
        
        // Vertical mode: Sadece dikey
        if (currentButton.Navigation == ButtonComponent::NavigationMode::Vertical)
        {
            if (left || right) return Entity{}; // Yatay hareket yasak
            if (!up && !down) return Entity{};
        }
        
        // Automatic mode: En yakın butonu bul
        glm::vec3 currentPos = glm::vec3(currentTransform.WorldTransform[3]);
        
        Entity bestCandidate;
        float bestDistance = FLT_MAX;
        
        auto buttonView = m_Registry.view<ButtonComponent, TransformComponent>();
        for (auto entity : buttonView)
        {
            if (entity == m_SelectedButton) continue; // Kendini atla
            
            auto& button = buttonView.get<ButtonComponent>(entity);
            if (!button.Interactable) continue;
            
            auto& transform = buttonView.get<TransformComponent>(entity);
            glm::vec3 pos = glm::vec3(transform.WorldTransform[3]);
            
            glm::vec3 dir = pos - currentPos;
            float distance = glm::length(dir);
            
            if (distance < 0.01f) continue; // Çok yakın
            
            dir = glm::normalize(dir);
            
            // Yön kontrolü
            bool validDirection = false;
            if (up && dir.y > 0.5f) validDirection = true;
            if (down && dir.y < -0.5f) validDirection = true;
            if (left && dir.x < -0.5f) validDirection = true;
            if (right && dir.x > 0.5f) validDirection = true;
            
            if (validDirection && distance < bestDistance)
            {
                bestDistance = distance;
                bestCandidate = Entity{ entity, this };
            }
        }
        
        return bestCandidate;
    }

    template<>
    CQ_API void Scene::OnComponentAdded<NativeScriptComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NativeScriptComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<ConquerorScriptComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ConquerorScriptComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioSourceComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioSourceComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioListenerComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioListenerComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioChorusFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioChorusFilterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioDistortionFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioDistortionFilterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioEchoFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioEchoFilterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioHighPassFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioHighPassFilterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioLowPassFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioLowPassFilterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioReverbFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioReverbFilterComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioReverbZoneComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioReverbZoneComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioGainComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioGainComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<AudioPanComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioPanComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<NavMeshAgentComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NavMeshAgentComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<NavMeshObstacleComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NavMeshObstacleComponent& component)
    {
    }

    template<>
    CQ_API void Scene::OnComponentAdded<NavMeshSurfaceComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NavMeshSurfaceComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<ReflectionProbeComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ReflectionProbeComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AdaptiveProbeVolumeComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AdaptiveProbeVolumeComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<TerrainComponent>([[maybe_unused]] Entity entity, TerrainComponent& component)
    {
        TerrainSystem::EnsureData(component);
    }

#define CQ_UI_ADDED(T) template<> void Scene::OnComponentAdded<T>([[maybe_unused]] Entity entity, [[maybe_unused]] T& component) {}
    CQ_UI_ADDED(RectTransformComponent) CQ_UI_ADDED(UIImageComponent) CQ_UI_ADDED(UITextComponent) CQ_UI_ADDED(UIButtonComponent)
    CQ_UI_ADDED(UIToggleComponent) CQ_UI_ADDED(UISliderComponent) CQ_UI_ADDED(UIInputFieldComponent) CQ_UI_ADDED(UIDropdownComponent)
    CQ_UI_ADDED(UIScrollRectComponent) CQ_UI_ADDED(UILayoutGroupComponent)
    CQ_UI_ADDED(UICanvasGroupComponent) CQ_UI_ADDED(UIRectMask2DComponent) CQ_UI_ADDED(UIMaskComponent) CQ_UI_ADDED(UIRawImageComponent) CQ_UI_ADDED(UIContentSizeFitterComponent)
    CQ_UI_ADDED(UIAspectRatioFitterComponent) CQ_UI_ADDED(UILayoutElementComponent) CQ_UI_ADDED(UIScrollbarComponent) CQ_UI_ADDED(UIEventTriggerComponent) CQ_UI_ADDED(UIEventSystemComponent) CQ_UI_ADDED(UIShadowComponent) CQ_UI_ADDED(UIOutlineComponent) CQ_UI_ADDED(UIPositionAsUV1Component)
#undef CQ_UI_ADDED

    template<>
    void Scene::OnComponentAdded<ParticleSystemComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ParticleSystemComponent& component)
    {
    }

    void Scene::UpdateAudioSystem([[maybe_unused]] Timestep ts)
    {
        // Biten ateşle-unut sesleri (AudioPlayOneShot) temizle
        AudioOneShots::Update();
        if (!AudioEngine::IsInitialized())
            return;

        // Mikser: proje ayarları + kare güncellemesi (ölçer, ducking, snapshot)
        auto& mixer = AudioMixer::Get();
        mixer.EnsureProjectLoaded(Project::GetActiveProjectDirectory());
        mixer.Update();
        const uint32_t routingGeneration = mixer.GetRoutingGeneration();

        // Audio Listener: dünya konumu ve yönelimi
        auto listenerView = m_Registry.view<TransformComponent, AudioListenerComponent>();
        for (auto entity : listenerView)
        {
            auto [transform, listener] = listenerView.get<TransformComponent, AudioListenerComponent>(entity);
            if (listener.IsActive && IsEntityActive(entity))
            {
                const glm::mat4& w = transform.WorldTransform;
                AudioEngine::Get().SetListenerPosition(glm::vec3(w[3]));
                glm::vec3 forward = -glm::vec3(w[2]);
                glm::vec3 up = glm::vec3(w[1]);
                if (glm::dot(forward, forward) > 1e-8f && glm::dot(up, up) > 1e-8f)
                    AudioEngine::Get().SetListenerDirection(glm::normalize(forward), glm::normalize(up));
                break; // Sadece ilk aktif listener'ı kullan
            }
        }
        
        // Ses örtme (occlusion) ve eşzamanlılık sınırları (oyun modunda)
        if (m_IsRuntimeScene)
            AudioOcclusion::Update(*this, ts.GetSeconds(), TimeManager::GetTime());

        // Audio Source'ları güncelle
        // Silinen / klibi değişen kaynakların müzik çaları
        InteractiveMusic::Prune(*this);
        auto audioView = m_Registry.view<TransformComponent, AudioSourceComponent>();
        for (auto entityID : audioView)
        {
            Entity entity{ entityID, this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& audioSource = entity.GetComponent<AudioSourceComponent>();

            // Etkileşimli müzik (Godot AudioStreamPlaylist / Synchronized / Interactive): kendi çaları, mikser grubuna bağlı
            if (InteractiveMusic::IsAsset(audioSource.FilePath))
            {
                if (audioSource.RuntimeSound)
                {
                    ReleaseAudioRuntime(m_Registry, entityID);
                    audioSource.LoadedPath.clear();
                }
                const auto* occlusion = m_Registry.try_get<SoundOcclusionComponent>(entityID);
                const auto* soundSettings = m_Registry.try_get<SoundSettingsComponent>(entityID);
                const float gain = audioSource.Volume * (occlusion ? occlusion->Gain : 1.0f) * (soundSettings ? soundSettings->Gain : 1.0f);
                InteractiveMusic::UpdateSource(*this, (uint32_t)entityID, audioSource, glm::vec3(transform.WorldTransform[3]), gain,
                                               m_IsRuntimeScene, routingGeneration);
                continue;
            }

            // Audio Random Container: istenince (Play / otomatik tetik) yeni klip seçilir
            const bool container = AudioRandomContainers::IsAsset(audioSource.FilePath);
            if (container && audioSource.ContainerRequest && audioSource.RuntimeSound)
            {
                ReleaseAudioRuntime(m_Registry, entityID);
                audioSource.LoadedPath.clear();
            }
            // Clip çalışma sırasında değiştiyse (Unity audioSource.clip = ...) eski sesi bırak, yenisi yüklensin
            if (audioSource.RuntimeSound && audioSource.FilePath != audioSource.LoadedPath)
            {
                const bool wasPlaying = audioSource.IsPlaying;
                ReleaseAudioRuntime(m_Registry, entityID);
                audioSource.PlayOnAwake = audioSource.PlayOnAwake || wasPlaying;
            }
            // RuntimeSound yoksa ve dosya yolu varsa yükle
            if (!audioSource.RuntimeSound && !audioSource.FilePath.empty())
            {
                std::string loadPath = audioSource.FilePath;
                bool playNow = false;
                if (container)
                {
                    AudioRandomContainers::Pick pick;
                    if (!AudioRandomContainers::Next(audioSource.FilePath, (uint64_t)entityID, pick))
                        continue;
                    loadPath = pick.Clip;
                    audioSource.ContainerClip = pick.Clip;
                    audioSource.ContainerVolume = pick.VolumeScale;
                    audioSource.ContainerPitch = pick.PitchScale;
                    if (pick.Loop)
                        audioSource.Loop = true;
                    playNow = audioSource.ContainerRequest;
                    audioSource.ContainerRequest = false;
                }
                if (loadPath.size() > 3 && loadPath[0] == 'C' && loadPath[1] == 'Q' && loadPath[2] == ':')
                {
                    std::string relative = loadPath.substr(3);
                    auto engineDir = std::filesystem::current_path();
                    loadPath = (engineDir / std::filesystem::path(relative)).string();
                }
                else if (std::filesystem::path(loadPath).is_relative())
                {
                    static std::unordered_map<std::string, std::string> s_AudioPathCache;
                    auto it = s_AudioPathCache.find(loadPath);
                    if (it != s_AudioPathCache.end())
                    {
                        loadPath = it->second;
                    }
                    else
                    {
                        auto projectDir = Project::GetActiveProjectDirectory();
                        if (!projectDir.empty())
                        {
                            const std::string key = loadPath;
                            loadPath = (projectDir / loadPath).string();
                            s_AudioPathCache[key] = loadPath;
                        }
                    }
                }

                AudioSource* source = new AudioSource();
                if (source->LoadFromFile(loadPath))
                {
                    audioSource.RuntimeSound = source;
                    audioSource.LoadedPath = audioSource.FilePath;
                    audioSource.NeedsReconnection = true; // İlk yüklemede bağla
                    
                    // Play On Awake yalnızca oyun modunda (düzenleyicide proje açılınca ses çalmaz; Unity gibi)
                    if ((audioSource.PlayOnAwake || playNow) && m_IsRuntimeScene)
                    {
                        source->Play();
                        audioSource.IsPlaying = true;
                        if (container)
                        {
                            ++audioSource.ContainerPlays;
                            audioSource.ContainerTimer = 0.0f;
                            AudioRandomContainerAsset asset;
                            if (AudioRandomContainers::Load(audioSource.FilePath, asset))
                                audioSource.ContainerInterval = AudioRandomContainers::NextInterval(asset);
                        }
                    }
                }
                else
                {
                    delete source;
                }
            }
            
            if (!audioSource.RuntimeSound) continue;
            
            AudioSource* source = (AudioSource*)audioSource.RuntimeSound;
            
            // Temel Ayarlar (Sound Occlusion çarpanı dahil)
            const auto* occlusion = m_Registry.try_get<SoundOcclusionComponent>(entityID);
            const auto* soundSettings = m_Registry.try_get<SoundSettingsComponent>(entityID);
            source->SetVolume(audioSource.Volume * (container ? audioSource.ContainerVolume : 1.0f) * (occlusion ? occlusion->Gain : 1.0f) * (soundSettings ? soundSettings->Gain : 1.0f));
            source->SetPitch(audioSource.Pitch * (container ? audioSource.ContainerPitch : 1.0f) * (soundSettings ? soundSettings->Pitch : 1.0f));
            // Audio Random Container: Automatic tetikleme (Unity Pulse) ya da Loop'ta bitince sıradaki klip
            if (container && m_IsRuntimeScene && audioSource.ContainerPlays > 0)
            {
                AudioRandomContainerAsset asset;
                if (AudioRandomContainers::Load(audioSource.FilePath, asset))
                {
                    audioSource.ContainerTimer += ts.GetSeconds();
                    const bool more = asset.Count == 0 || audioSource.ContainerPlays < asset.Count;
                    if (asset.Trigger == 1 && more && audioSource.ContainerTimer >= audioSource.ContainerInterval)
                        audioSource.ContainerRequest = true;
                    else if (asset.Trigger == 0 && audioSource.Loop && !source->IsPlaying() && audioSource.IsPlaying)
                        audioSource.ContainerRequest = true;
                }
            }
            source->SetPan(audioSource.Pan);
            source->SetLooping(audioSource.Loop);
            source->SetMute(audioSource.Mute);
            if (source->IsSpatialized() != audioSource.Is3D)
            {
                source->SetSpatialization(audioSource.Is3D);
                audioSource.NeedsReconnection = true;
            }
            
            // Mikser grubu ya da yapısı değiştiyse yeniden bağla
            if (audioSource.RoutingGeneration != routingGeneration)
            {
                audioSource.RoutingGeneration = routingGeneration;
                audioSource.NeedsReconnection = true;
            }

            if (audioSource.Is3D)
            {
                source->SetPosition(glm::vec3(transform.WorldTransform[3]));
                source->SetMinDistance(audioSource.MinDistance);
                source->SetMaxDistance(audioSource.MaxDistance);
                source->SetDopplerFactor(audioSource.DopplerLevel);
                source->SetAttenuationModel((AudioAttenuationModel)audioSource.Attenuation);
                source->SetRolloff(audioSource.RolloffFactor);
            }
            
            audioSource.IsPlaying = source->IsPlaying();
            
            // Eğer graph oluşturulmamışsa (Editor'de değilsek veya Editor açılmamışsa) varsayılan bir GraphLinks oluştur
            if (audioSource.GraphNodes.empty())
            {
                if (audioSource.NeedsReconnection)
                {
                    // Kaynak -> mikser grubu (yoksa Master)
                    source->DetachAllOutputs();
                    mixer.Connect(source, audioSource.OutputGroup);
                    audioSource.NeedsReconnection = false;
                }
                continue;
            }

            // Node ID'den filtre nesnesini bulan/yaratan lambda
            auto getNodeInstance = [&](int nodeID) -> AudioNode* {
                AudioGraphNodeType type = AudioGraphNodeType::Master;
                bool found = false;
                for (auto& n : audioSource.GraphNodes) {
                    if (n.ID == nodeID) { type = n.Type; found = true; break; }
                }
                if (!found) return nullptr;
                
                if (type == AudioGraphNodeType::Source) return source;
                if (type == AudioGraphNodeType::Master) return nullptr;

                #define HANDLE_FILTER(NodeType, ComponentType, AudioClass, ...) \
                if (type == NodeType && entity.HasComponent<ComponentType>()) { \
                    auto& comp = entity.GetComponent<ComponentType>(); \
                    if (!comp.RuntimeFilter) comp.RuntimeFilter = new AudioClass(__VA_ARGS__); \
                    else ((AudioClass*)comp.RuntimeFilter)->SetParams(__VA_ARGS__); \
                    return (AudioNode*)comp.RuntimeFilter; \
                }

                HANDLE_FILTER(AudioGraphNodeType::LowPass, AudioLowPassFilterComponent, AudioLowPassFilter, comp.CutoffFrequency, 8);
                HANDLE_FILTER(AudioGraphNodeType::HighPass, AudioHighPassFilterComponent, AudioHighPassFilter, comp.CutoffFrequency, 8);
                HANDLE_FILTER(AudioGraphNodeType::Echo, AudioEchoFilterComponent, AudioEchoFilter, comp.Delay, comp.Decay, comp.WetMix, comp.DryMix);
                HANDLE_FILTER(AudioGraphNodeType::Reverb, AudioReverbFilterComponent, AudioReverbFilter, 1.0f, comp.DecayTime, 0.5f, 1.0f);
                HANDLE_FILTER(AudioGraphNodeType::Chorus, AudioChorusFilterComponent, AudioChorusFilter, comp.Delay, comp.Depth, comp.Rate, comp.WetMix, comp.DryMix);
                HANDLE_FILTER(AudioGraphNodeType::Distortion, AudioDistortionFilterComponent, AudioDistortionFilter, comp.Level);
                HANDLE_FILTER(AudioGraphNodeType::Gain, AudioGainComponent, AudioGainNode, comp.Gain);
                HANDLE_FILTER(AudioGraphNodeType::Pan, AudioPanComponent, AudioPanNode, comp.Pan);
                #undef HANDLE_FILTER

                return nullptr;
            };

            // Her node için parametreleri güncelle (Reconnection gerekmese bile slider'lar etki etsin diye)
            for (auto& n : audioSource.GraphNodes) {
                if (n.Type != AudioGraphNodeType::Source && n.Type != AudioGraphNodeType::Master) {
                    getNodeInstance(n.ID); 
                }
            }

            // Bağlantıları güncelle
            if (audioSource.NeedsReconnection)
            {
                // Tüm bağlantıları kes
                source->DetachAllOutputs();
                for (auto& n : audioSource.GraphNodes) {
                    if (n.Type != AudioGraphNodeType::Source && n.Type != AudioGraphNodeType::Master) {
                        AudioNode* node = getNodeInstance(n.ID);
                        if (node) node->DetachAllOutputs();
                    }
                }

                // Yeni bağlantıları kur
                for (auto& link : audioSource.GraphLinks)
                {
                    bool fromBypassed = false;
                    for (auto& n : audioSource.GraphNodes) {
                        if (n.ID == link.FromNodeID && n.Bypassed) { fromBypassed = true; break; }
                    }
                    if (fromBypassed) continue; // Bypassed bir node çıkış yapamaz
                    
                    AudioNode* fromNode = getNodeInstance(link.FromNodeID);
                    if (!fromNode) continue;
                    
                    int targetID = link.ToNodeID;
                    // Eğer hedef bypassed ise, sonrakine atla
                    while (true) {
                        bool targetBypassed = false;
                        for (auto& n : audioSource.GraphNodes) {
                            if (n.ID == targetID && n.Bypassed) { targetBypassed = true; break; }
                        }
                        if (!targetBypassed) break;
                        
                        int nextID = -1;
                        for (auto& l : audioSource.GraphLinks) {
                            if (l.FromNodeID == targetID) { nextID = l.ToNodeID; break; }
                        }
                        if (nextID == -1) break;
                        targetID = nextID;
                    }
                    
                    bool isTargetMaster = false;
                    for (auto& n : audioSource.GraphNodes) {
                        if (n.ID == targetID && n.Type == AudioGraphNodeType::Master) {
                            isTargetMaster = true; break;
                        }
                    }
                    
                    if (isTargetMaster)
                    {
                        mixer.Connect(fromNode, audioSource.OutputGroup);
                    }
                    else
                    {
                        AudioNode* toNode = getNodeInstance(targetID);
                        if (toNode) fromNode->ConnectTo(toNode);
                    }
                }
                
                audioSource.NeedsReconnection = false;
            }
        }
    }

    void Scene::UpdateAnimators(Timestep ts)
    {
        const float dt = ts.GetSeconds();

        // Animation Rigging kısıtlarını sahiplerine göre topla
        RigConstraints::BeginFrame(*this, dt);

        // Basit klip oynatıcı (AnimatorComponent)
        auto view = m_Registry.view<AnimatorComponent, MeshRendererComponent>();
        for (auto entity : view)
        {
            if (IsProcessDisabled(entity))
                continue;   // Visible On Screen Enabler: ekran dışında poz donar
            auto& anim = view.get<AnimatorComponent>(entity);
            auto& mc = view.get<MeshRendererComponent>(entity);
            if (!mc.ModelData || !mc.ModelData->IsSkinned || !mc.ModelData->SkeletonData || mc.ModelData->Animations.empty())
                continue;

            // Ragdoll etkinse poz fizik gövdelerinden gelir
            if (Ragdoll::ApplyPose(*this, (uint32_t)entity, anim.BoneMatricesGPU))
                continue;
            const int clipCount = static_cast<int>(mc.ModelData->Animations.size());
            const int idx = std::clamp(anim.ActiveClipIndex, 0, clipCount - 1);
            const auto& clipPtr = mc.ModelData->Animations[static_cast<size_t>(idx)];
            if (!clipPtr)
                continue;

            if (anim.Playing)
                anim.CurrentTime += dt * anim.Speed;

            static thread_local Pose s_Pose;
            Animator::SamplePose(*mc.ModelData->SkeletonData, *clipPtr, anim.CurrentTime, true, s_Pose);
            // Zaman çizelgesi klip geçişi
            if (anim.BlendWeight > 0.0f && anim.BlendClipIndex >= 0 && anim.BlendClipIndex < clipCount &&
                mc.ModelData->Animations[(size_t)anim.BlendClipIndex])
            {
                static thread_local Pose s_BlendPose;
                Animator::SamplePose(*mc.ModelData->SkeletonData, *mc.ModelData->Animations[(size_t)anim.BlendClipIndex], anim.BlendClipTime,
                                     true, s_BlendPose);
                Animator::BlendPoses(s_Pose, s_BlendPose, anim.BlendWeight, s_Pose);
            }
            AnimationAssets::Apply(*this, (uint32_t)entity, *mc.ModelData->SkeletonData, s_Pose, (float)ts);
            if (RigConstraints::HasConstraints((uint32_t)entity))
                RigConstraints::Apply(*this, (uint32_t)entity, *mc.ModelData->SkeletonData, s_Pose);
            if (auto* customizer = m_Registry.try_get<CharacterCustomizerComponent>(entity))
                CharacterCustomizer::ApplyToPose(*customizer, *mc.ModelData->SkeletonData, s_Pose);
            Animator::PoseToSkinning(*mc.ModelData->SkeletonData, s_Pose, anim.BoneMatricesGPU);
            anim.FinalPose = s_Pose;
        }

        // Durum makinesi (AnimationComponent + AnimationController) + montajlar
        std::vector<std::pair<entt::entity, std::vector<AnimNotifyEvent>>> notifyBatches;
        auto animView = m_Registry.view<AnimationComponent, MeshRendererComponent>();
        for (auto entity : animView)
        {
            auto& ac = animView.get<AnimationComponent>(entity);
            auto& mc = animView.get<MeshRendererComponent>(entity);
            // Ragdoll etkinse poz fizik gövdelerinden gelir (animasyon durur)
            if (Ragdoll::ApplyPose(*this, (uint32_t)entity, ac.BoneMatricesGPU))
                continue;
            if (!ac.Controller && !ac.Montage.Active)
                continue;
            if (!ac.Playing && ac.PlayOnAwake)
                ac.Playing = true;
            if (!mc.ModelData || !mc.ModelData->IsSkinned || !mc.ModelData->SkeletonData)
                continue;
            const bool hasRig = RigConstraints::HasConstraints((uint32_t)entity);
            auto* customizer = m_Registry.try_get<CharacterCustomizerComponent>(entity);
            if (hasRig || customizer)
            {
                // Poz sonrası: Animation Rigging kısıtları, ardından Character Customizer oranları
                const Skeleton& skeleton = *mc.ModelData->SkeletonData;
                const float postDt = ac.Playing ? dt : 0.0f;
                const std::function<void(Pose&)> post = [this, entity, &skeleton, hasRig, customizer, postDt](Pose& pose) {
                    AnimationAssets::Apply(*this, (uint32_t)entity, skeleton, pose, postDt);
                    if (hasRig)
                        RigConstraints::Apply(*this, (uint32_t)entity, skeleton, pose);
                    if (customizer)
                        CharacterCustomizer::ApplyToPose(*customizer, skeleton, pose);
                };
                AnimationStateMachine::Update(ac, *mc.ModelData, ac.Playing ? dt : 0.0f, &post);
            }
            else
                AnimationStateMachine::Update(ac, *mc.ModelData, ac.Playing ? dt : 0.0f);
            if (!ac.PendingNotifies.empty())
            {
                notifyBatches.emplace_back(entity, std::move(ac.PendingNotifies));
                ac.PendingNotifies.clear();
            }
        }

        // Anim Notify / montaj olayları: varlığın betiklerine, varlıkta betik yoksa betikli ilk ataya (karakter kökü).
        // "Sound:<yol>" adlı notify'lar sesi varlığın konumunda çalar.
        for (auto& [entity, events] : notifyBatches)
        {
            if (!m_Registry.valid(entity))
                continue;
            entt::entity target = entity;
            for (Entity e{ entity, this }; e; e = e.GetParent())
                if (e.HasComponent<ConquerorScriptComponent>())
                {
                    target = (entt::entity)e;
                    break;
                }
            const glm::vec3 position = glm::vec3(m_Registry.get<TransformComponent>(entity).WorldTransform[3]);
            for (const auto& ev : events)
            {
                if (ev.Kind == AnimNotifyEventKind::Notify && ev.Name.rfind("Sound:", 0) == 0)
                {
                    AudioOneShots::PlayAt(SceneSerializer::ResolveAssetPath(ev.Name.substr(6)), position);
                    continue;
                }
                const char* method = "OnAnimNotify";
                switch (ev.Kind)
                {
                    case AnimNotifyEventKind::Notify: method = "OnAnimNotify"; break;
                    case AnimNotifyEventKind::Begin: method = "OnAnimNotifyBegin"; break;
                    case AnimNotifyEventKind::End: method = "OnAnimNotifyEnd"; break;
                    case AnimNotifyEventKind::MontageBlendingOut: method = "OnMontageBlendingOut"; break;
                    case AnimNotifyEventKind::MontageEnded: method = "OnMontageEnded"; break;
                }
                const bool montage = ev.Kind == AnimNotifyEventKind::MontageBlendingOut || ev.Kind == AnimNotifyEventKind::MontageEnded;
                CQS::Value args[2] = { CQS::Value::MakeString(ev.Name), CQS::Value::MakeBool(ev.Interrupted) };
                InvokeScriptMethod((uint32_t)target, method, args, montage ? 2 : 1);
                if (!m_Registry.valid(target))
                    break;
            }
        }

        // Kemiğe bağlı varlıklar (silahlar, eşyalar) yeni pozu izler
        GameFramework::UpdateBoneAttachments(*this);
    }

    // Editör önizlemesi: durum makinesi yalnızca çizim pozunu (kemik matrisleri) üretir. Betik olayları / ses, kök hareketi,
    // Animation Rigging kısıtları (hedefleri oyunda atanır) ve kemiğe bağlı nesnelerin taşınması yapılmaz: sahne verisi
    // değişmez, kaydedilen dosya etkilenmez.
    void Scene::UpdateEditorAnimationPreview(float dt)
    {
        auto view = m_Registry.view<AnimationComponent, MeshRendererComponent>();
        for (auto entity : view)
        {
            auto& ac = view.get<AnimationComponent>(entity);
            auto& mc = view.get<MeshRendererComponent>(entity);
            if (!ac.Controller || !mc.ModelData || !mc.ModelData->IsSkinned || !mc.ModelData->SkeletonData)
                continue;
            AnimationStateMachine::Update(ac, *mc.ModelData, dt);
            ac.PendingNotifies.clear();
        }
    }

    void Scene::StopAllAudio()
    {
        auto view = m_Registry.view<AudioSourceComponent>();
        for (auto entityID : view)
        {
            auto& audioSource = view.get<AudioSourceComponent>(entityID);
            if (audioSource.RuntimeSound)
            {
                AudioSource* source = (AudioSource*)audioSource.RuntimeSound;
                source->Stop();
                audioSource.IsPlaying = false;
            }
        }
    }
}
