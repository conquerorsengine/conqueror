#include "PhysicsBackend3D.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/DynamicLibrary.h"

// Yalnızca bildirimler için (işlev işaretçisi türleri); kitaplık çalışma zamanında yüklenir
#include <mujoco/mujoco.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

// MuJoCo arka ucu: sahne MJCF (XML) modeline çevrilir, MuJoCo ile simüle edilir.
// Model yapısal değişikliklerde (gövde/eklem ekleme-silme) yeniden derlenir ve durum aktarılır.
// Kısıtlar (Unity/Jolt ile fark): tetikleyiciler mj_geomDistance ile; menteşe = iki 'connect';
// kaydırma eklemi desteklenmez; statik üçgen ağları dışbükey kabuk olarak çarpışır (arazi hfield kullanır).
namespace Conqueror::Physics3D
{
    namespace
    {
        struct MuJoCoApi
        {
            DynamicLibrary Lib;
            bool Loaded = false;
            bool Tried = false;
            std::string Error;
            std::string Version;

            decltype(&mj_version) version = nullptr;
            decltype(&mj_versionString) versionString = nullptr;
            decltype(&mj_defaultVFS) defaultVFS = nullptr;
            decltype(&mj_addBufferVFS) addBufferVFS = nullptr;
            decltype(&mj_deleteVFS) deleteVFS = nullptr;
            decltype(&mj_loadXML) loadXML = nullptr;
            decltype(&mj_deleteModel) deleteModel = nullptr;
            decltype(&mj_makeData) makeData = nullptr;
            decltype(&mj_deleteData) deleteData = nullptr;
            decltype(&mj_step) step = nullptr;
            decltype(&mj_forward) forward = nullptr;
            decltype(&mj_kinematics) kinematics = nullptr;
            decltype(&mj_ray) ray = nullptr;
            decltype(&mj_geomDistance) geomDistance = nullptr;
            decltype(&mj_name2id) name2id = nullptr;
            void (**userError)(const char*) = nullptr;
            void (**userWarning)(const char*) = nullptr;

            std::vector<std::filesystem::path> Candidates(const std::string& overridePath) const
            {
                std::vector<std::filesystem::path> c;
                const std::string file = DynamicLibrary::PlatformFileName("mujoco");
                auto addDir = [&](const std::filesystem::path& dir) {
                    if (dir.empty()) return;
                    c.push_back(dir / file);
                    c.push_back(dir / "lib" / file);
                    c.push_back(dir / "bin" / file);
                };
                if (!overridePath.empty())
                {
                    std::filesystem::path p(overridePath);
                    std::error_code ec;
                    if (std::filesystem::is_directory(p, ec)) addDir(p);
                    else c.push_back(p);
                }
                for (const char* env : { "MUJOCO_DIR", "MUJOCO_PATH", "MUJOCO_HOME" })
                    if (const char* v = std::getenv(env)) addDir(v);
                addDir(DynamicLibrary::ExecutableDirectory() / "mujoco");
                c.push_back(DynamicLibrary::ExecutableDirectory() / file);
#ifndef _WIN32
                addDir("/opt/mujoco");
                std::error_code ec;
                for (const char* root : { "/opt" })
                    if (std::filesystem::is_directory(root, ec))
                        for (auto& e : std::filesystem::directory_iterator(root, ec))
                            if (e.path().filename().string().rfind("mujoco", 0) == 0)
                                addDir(e.path());
                if (const char* home = std::getenv("HOME"))
                {
                    std::filesystem::path dot = std::filesystem::path(home) / ".mujoco";
                    if (std::filesystem::is_directory(dot, ec))
                        for (auto& e : std::filesystem::directory_iterator(dot, ec))
                            addDir(e.path());
                }
                addDir("/usr/local");
#else
                if (const char* pf = std::getenv("ProgramFiles")) addDir(std::filesystem::path(pf) / "MuJoCo");
#endif
                c.push_back(file); // sistem araması
                return c;
            }

            bool Load(const std::string& overridePath)
            {
                if (Loaded)
                    return true;
                if (!DynamicLibrary::OpenFirst(Lib, Candidates(overridePath), &Error))
                {
                    Error = "MuJoCo library not found (" + Error + "). Install MuJoCo " + std::string("3.17") +
                            " and set MUJOCO_DIR or Project Settings > Physics > MuJoCo Library.";
                    return false;
                }
#define CQ_MJ(sym, name) sym = Lib.Get<decltype(sym)>(name); if (!sym) { Error = std::string("Missing symbol ") + name; Lib.Close(); return false; }
                CQ_MJ(version, "mj_version");
                CQ_MJ(versionString, "mj_versionString");
                CQ_MJ(defaultVFS, "mj_defaultVFS");
                CQ_MJ(addBufferVFS, "mj_addBufferVFS");
                CQ_MJ(deleteVFS, "mj_deleteVFS");
                CQ_MJ(loadXML, "mj_loadXML");
                CQ_MJ(deleteModel, "mj_deleteModel");
                CQ_MJ(makeData, "mj_makeData");
                CQ_MJ(deleteData, "mj_deleteData");
                CQ_MJ(step, "mj_step");
                CQ_MJ(forward, "mj_forward");
                CQ_MJ(kinematics, "mj_kinematics");
                CQ_MJ(ray, "mj_ray");
                CQ_MJ(geomDistance, "mj_geomDistance");
                CQ_MJ(name2id, "mj_name2id");
#undef CQ_MJ
                userError = Lib.Get<void (**)(const char*)>("mju_user_error");
                userWarning = Lib.Get<void (**)(const char*)>("mju_user_warning");
                // Yapı düzenleri başlıklara bağlı: sürüm birebir aynı olmalı
                int v = version();
                if (v / 1000 != mjVERSION_HEADER / 1000)
                {
                    Error = std::string("MuJoCo version mismatch: found ") + versionString() + ", engine built for 3.17.x";
                    Lib.Close();
                    return false;
                }
                Version = versionString();
                // Varsayılan hata işleyicisi süreci sonlandırır: günlüğe yönlendir
                if (userError)
                    *userError = [](const char* msg) { CQ_CORE_ERROR("[MuJoCo] {0}", msg ? msg : "error"); };
                if (userWarning)
                    *userWarning = [](const char* msg) { CQ_CORE_WARN("[MuJoCo] {0}", msg ? msg : "warning"); };
                Loaded = true;
                return true;
            }
        };

        MuJoCoApi& Api()
        {
            static MuJoCoApi* s_Api = new MuJoCoApi(); // süreç sonuna kadar (kitaplık kapatılmaz)
            return *s_Api;
        }

        std::string F(double v)
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.9g", std::isfinite(v) ? v : 0.0);
            return buf;
        }
        std::string V3(const glm::vec3& v) { return F(v.x) + " " + F(v.y) + " " + F(v.z); }
        // MuJoCo dördey sırası: w x y z
        std::string Q(const glm::quat& q) { glm::quat n = glm::normalize(q); return F(n.w) + " " + F(n.x) + " " + F(n.y) + " " + F(n.z); }

        // MJCF contype/conaffinity işaretli int: 31 bit kullanılabilir (katman 31, 30 ile paylaşır)
        inline unsigned LayerBit(int layer) { return 1u << std::clamp(layer, 0, 30); }
        inline unsigned AffinityMask(const PhysicsSettings& s, int layer)
        {
            uint32_t row = s.CollisionMatrix[(size_t)std::clamp(layer, 0, 31)];
            unsigned mask = row & 0x3FFFFFFFu;
            if (row & 0xC0000000u) mask |= 0x40000000u; // 30 ve 31 -> bit 30
            return mask;
        }

        // MuJoCo kapsül/silindir ekseni yerel Z; motorda Y. Y'yi Z'ye hizalayan dönüş (X ekseni etrafında -90°)
        const glm::quat kYToZ = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));

        float ShapeVolume(const ShapeDesc& s)
        {
            switch (s.Type)
            {
                case ShapeType::Box: return 8.0f * std::fabs(s.HalfExtents.x * s.HalfExtents.y * s.HalfExtents.z);
                case ShapeType::Sphere: return 4.18879f * s.Radius * s.Radius * s.Radius;
                case ShapeType::Capsule: return 3.14159f * s.Radius * s.Radius * (2.0f * s.HalfHeight) + 4.18879f * s.Radius * s.Radius * s.Radius;
                case ShapeType::Cylinder: return 3.14159f * s.Radius * s.Radius * 2.0f * s.HalfHeight;
                default:
                {
                    glm::vec3 mn(1e9f), mx(-1e9f);
                    for (auto& v : s.Vertices) { mn = glm::min(mn, v); mx = glm::max(mx, v); }
                    glm::vec3 e = glm::max(mx - mn, glm::vec3(0.01f));
                    return e.x * e.y * e.z * 0.5f;
                }
            }
        }
    }

    bool ProbeMuJoCo(const std::string& overridePath, std::string* foundPath, std::string* version)
    {
        MuJoCoApi& api = Api();
        bool ok = api.Load(overridePath);
        if (ok)
        {
            if (foundPath) *foundPath = api.Lib.GetPath().string();
            if (version) *version = api.Version;
        }
        else if (foundPath)
            *foundPath = api.Error;
        return ok;
    }

    class MuJoCoBackend final : public Backend
    {
    public:
        ~MuJoCoBackend() override { Shutdown(); }

        const char* GetName() const override { return "MuJoCo"; }

        bool Init(const PhysicsSettings& settings, std::string* error) override
        {
            if (!Api().Load(settings.MuJoCoLibraryPath))
            {
                if (error) *error = Api().Error;
                return false;
            }
            m_Settings = settings;
            m_Initialized = true;
            m_Dirty = true;
            CQ_CORE_INFO("[Physics] MuJoCo {0} loaded from {1}", Api().Version, Api().Lib.GetPath().string());
            return true;
        }

        void Shutdown() override
        {
            FreeModel();
            m_Bodies.clear();
            m_Joints.clear();
            m_Characters.clear();
            m_Initialized = false;
        }

        void SetGravity(const glm::vec3& g) override
        {
            m_Settings.Gravity = g;
            if (m_Model)
                for (int i = 0; i < 3; ++i) m_Model->opt.gravity[i] = g[i];
        }

        void Step(float dt) override
        {
            if (!m_Initialized || dt <= 0.0f || !EnsureModel())
                return;
            const double h = std::max(1e-5, (double)m_Settings.FixedTimestep * std::max(0.01f, m_Settings.MuJoCoTimestepScale));
            int sub = std::max(1, (int)std::lround(dt / h));
            m_Model->opt.timestep = dt / sub;
            ApplyPendingForces();
            ApplyKinematicTargets();
            ApplyMotors();
            for (int i = 0; i < sub; ++i)
                Api().step(m_Model, m_Data);
            ClearAppliedForces();
            m_LastDt = dt;
            if (m_Data->warning[mjWARN_BADQACC].number > m_LastBadQacc)
            {
                m_LastBadQacc = m_Data->warning[mjWARN_BADQACC].number;
                CQ_CORE_WARN("[Physics] MuJoCo simulation became unstable and was reset (check masses/joints).");
            }
            CollectContacts();
        }

        // ── Gövdeler ──
        Handle CreateBody(const BodyDesc& desc) override
        {
            if (!m_Initialized || desc.Shapes.empty())
                return 0;
            Handle h = ++m_NextHandle;
            BodyRec& b = m_Bodies[h];
            b.Desc = desc;
            b.Pos = desc.Position;
            b.Rot = desc.Rotation;
            b.LinVel = desc.LinearVelocity;
            b.AngVel = desc.AngularVelocity;
            b.KinPos = desc.Position;
            b.KinRot = desc.Rotation;
            for (auto& s : desc.Shapes)
                if (s.Type == ShapeType::TriangleMesh && !m_WarnedMesh)
                {
                    m_WarnedMesh = true;
                    CQ_CORE_WARN("[Physics] MuJoCo collides triangle meshes as convex hulls; terrain uses height fields.");
                }
            m_Dirty = true;
            return h;
        }

        void DestroyBody(Handle h) override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end())
                return;
            SyncBodyState(it->second);
            // Bağlı eklemler
            for (auto jt = m_Joints.begin(); jt != m_Joints.end();)
            {
                if (jt->second.Desc.BodyA == h || jt->second.Desc.BodyB == h) jt = m_Joints.erase(jt);
                else ++jt;
            }
            // Bu gövdeyi içeren temas çiftleri: Exit
            uint32_t e = it->second.Desc.Entity;
            for (auto pt = m_ActivePairs.begin(); pt != m_ActivePairs.end();)
            {
                if (pt->second.A == e || pt->second.B == e)
                {
                    ContactEvent ev;
                    ev.Type = ContactEvent::Exit;
                    ev.EntityA = pt->second.A;
                    ev.EntityB = pt->second.B;
                    ev.Trigger = pt->second.Trigger;
                    m_Events.push_back(ev);
                    pt = m_ActivePairs.erase(pt);
                }
                else
                    ++pt;
            }
            m_Bodies.erase(it);
            m_Dirty = true;
        }

        void SetBodyTransform(Handle h, const glm::vec3& position, const glm::quat& rotation, bool resetVelocity) override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end()) return;
            BodyRec& b = it->second;
            b.Pos = position;
            b.Rot = rotation;
            b.KinPos = position;
            b.KinRot = rotation;
            if (resetVelocity) { b.LinVel = glm::vec3(0.0f); b.AngVel = glm::vec3(0.0f); }
            if (m_Model && !m_Dirty)
            {
                if (b.QposAdr >= 0)
                {
                    WriteFreeJoint(b, position, rotation);
                    if (resetVelocity)
                        for (int i = 0; i < 6; ++i) m_Data->qvel[b.DofAdr + i] = 0.0;
                }
                else if (b.Mocap >= 0)
                    WriteMocap(b.Mocap, position, rotation);
                else
                    m_Dirty = true; // statik gövde taşındı: yeniden derle
                Api().kinematics(m_Model, m_Data);
            }
            else if (b.Desc.Motion == MotionType::Static)
                m_Dirty = true;
        }

        void MoveKinematic(Handle h, const glm::vec3& position, const glm::quat& rotation, float) override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end()) return;
            it->second.KinPos = position;
            it->second.KinRot = rotation;
            it->second.HasKinTarget = true;
            if (it->second.Desc.Motion == MotionType::Static)
                SetBodyTransform(h, position, rotation, false);
        }

        void GetBodyTransform(Handle h, glm::vec3& position, glm::quat& rotation) const override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end()) return;
            const BodyRec& b = it->second;
            if (m_Model && !m_Dirty && b.MjBody >= 0)
            {
                const mjtNum* p = m_Data->xpos + 3 * b.MjBody;
                const mjtNum* q = m_Data->xquat + 4 * b.MjBody;
                position = glm::vec3((float)p[0], (float)p[1], (float)p[2]);
                rotation = glm::normalize(glm::quat((float)q[0], (float)q[1], (float)q[2], (float)q[3]));
            }
            else
            {
                position = b.Pos;
                rotation = b.Rot;
            }
        }

        bool IsBodySleeping(Handle) const override { return false; }
        void WakeBody(Handle) override {}

        void AddForce(Handle h, const glm::vec3& f) override { if (auto* b = DynamicBody(h)) b->Force += f; }
        void AddForceAtPoint(Handle h, const glm::vec3& f, const glm::vec3& p) override
        {
            if (auto* b = DynamicBody(h))
            {
                b->Force += f;
                b->Torque += glm::cross(p - ComOf(*b), f);
            }
        }
        void AddImpulse(Handle h, const glm::vec3& i) override
        {
            if (DynamicBody(h))
            {
                float m = GetMass(h);
                SetLinearVelocity(h, GetLinearVelocity(h) + (m > 0.0f ? i / m : glm::vec3(0.0f)));
            }
        }
        void AddImpulseAtPoint(Handle h, const glm::vec3& i, const glm::vec3& p) override
        {
            if (auto* b = DynamicBody(h))
            {
                AddImpulse(h, i);
                AddAngularImpulse(h, glm::cross(p - ComOf(*b), i));
            }
        }
        void AddTorque(Handle h, const glm::vec3& t) override { if (auto* b = DynamicBody(h)) b->Torque += t; }
        void AddAngularImpulse(Handle h, const glm::vec3& l) override
        {
            BodyRec* b = DynamicBody(h);
            if (!b) return;
            if (!EnsureModel() || b->MjBody < 0)
                return;
            // Δω = R I⁻¹ Rᵀ L (atalet çerçevesinde köşegen)
            const mjtNum* R = m_Data->ximat + 9 * b->MjBody;
            const mjtNum* I = m_Model->body_inertia + 3 * b->MjBody;
            double li[3];
            for (int r = 0; r < 3; ++r) li[r] = R[0 * 3 + r] * l.x + R[1 * 3 + r] * l.y + R[2 * 3 + r] * l.z;
            for (int r = 0; r < 3; ++r) li[r] /= std::max(1e-9, (double)I[r]);
            glm::vec3 dw((float)(R[0] * li[0] + R[1] * li[1] + R[2] * li[2]),
                         (float)(R[3] * li[0] + R[4] * li[1] + R[5] * li[2]),
                         (float)(R[6] * li[0] + R[7] * li[1] + R[8] * li[2]));
            SetAngularVelocity(h, GetAngularVelocity(h) + dw);
        }

        void SetLinearVelocity(Handle h, const glm::vec3& v) override
        {
            BodyRec* b = DynamicBody(h);
            if (!b) return;
            b->LinVel = v;
            if (m_Model && !m_Dirty && b->DofAdr >= 0)
                for (int i = 0; i < 3; ++i) m_Data->qvel[b->DofAdr + i] = v[i];
        }

        glm::vec3 GetLinearVelocity(Handle h) const override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end()) return glm::vec3(0.0f);
            const BodyRec& b = it->second;
            if (m_Model && !m_Dirty && b.DofAdr >= 0)
                return glm::vec3((float)m_Data->qvel[b.DofAdr], (float)m_Data->qvel[b.DofAdr + 1], (float)m_Data->qvel[b.DofAdr + 2]);
            return b.LinVel;
        }

        void SetAngularVelocity(Handle h, const glm::vec3& w) override
        {
            BodyRec* b = DynamicBody(h);
            if (!b) return;
            b->AngVel = w;
            if (m_Model && !m_Dirty && b->DofAdr >= 0)
            {
                // Serbest eklemin açısal hızı gövde çerçevesinde: ω_yerel = Rᵀ ω
                const mjtNum* R = m_Data->xmat + 9 * b->MjBody;
                for (int r = 0; r < 3; ++r)
                    m_Data->qvel[b->DofAdr + 3 + r] = R[0 * 3 + r] * w.x + R[1 * 3 + r] * w.y + R[2 * 3 + r] * w.z;
            }
        }

        glm::vec3 GetAngularVelocity(Handle h) const override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end()) return glm::vec3(0.0f);
            const BodyRec& b = it->second;
            if (m_Model && !m_Dirty && b.DofAdr >= 0)
            {
                const mjtNum* R = m_Data->xmat + 9 * b.MjBody;
                const mjtNum* w = m_Data->qvel + b.DofAdr + 3;
                return glm::vec3((float)(R[0] * w[0] + R[1] * w[1] + R[2] * w[2]),
                                 (float)(R[3] * w[0] + R[4] * w[1] + R[5] * w[2]),
                                 (float)(R[6] * w[0] + R[7] * w[1] + R[8] * w[2]));
            }
            return b.AngVel;
        }

        float GetMass(Handle h) const override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end() || it->second.Desc.Motion != MotionType::Dynamic) return 0.0f;
            if (m_Model && !m_Dirty && it->second.MjBody >= 0)
                return (float)m_Model->body_mass[it->second.MjBody];
            return it->second.Desc.Mass;
        }

        // ── Eklemler ──
        Handle CreateJoint(const JointDesc& d) override
        {
            if (!m_Initialized || !m_Bodies.count(d.BodyA))
                return 0;
            if (d.Type == JointType::Slider)
            {
                CQ_CORE_WARN("[Physics] Slider joints are not supported by the MuJoCo backend (use Jolt).");
                return 0;
            }
            if (d.Type == JointType::SixDOF)
            {
                // MuJoCo eşitlik kısıtlarıyla: öteleme kilitli olmalı; dönme tamamen kilitli (weld), tek eksen serbest
                // (menteşe) ya da tamamen serbest/sınırlı (top eklem, sınırlar uygulanmaz). Diğerleri için Jolt.
                const SixDOFDesc& x = d.SixDOF;
                if (x.Linear[0] != AxisMotion::Locked || x.Linear[1] != AxisMotion::Locked || x.Linear[2] != AxisMotion::Locked)
                {
                    CQ_CORE_WARN("[Physics] Configurable Joint with free/limited translation needs the Jolt backend.");
                    return 0;
                }
            }
            Handle h = ++m_NextHandle;
            JointRec& j = m_Joints[h];
            j.Desc = d;
            // Bağlantı noktalarını gövde yerel uzayında sakla (yeniden derlemede doğru kalsın)
            j.LocalAnchorA = ToLocal(d.BodyA, d.AnchorWorld);
            const glm::vec3 pointB = d.UseFrameB ? d.AnchorWorldB : (d.Type == JointType::Spring ? d.ConnectedAnchorWorld : d.AnchorWorld);
            j.LocalAnchorB = d.BodyB ? ToLocal(d.BodyB, pointB) : pointB;
            j.LocalAxisA = ToLocalDir(d.BodyA, d.AxisWorld);
            m_Dirty = true;
            return h;
        }

        void DestroyJoint(Handle h) override
        {
            if (m_Joints.erase(h))
                m_Dirty = true;
        }

        void GetJointReaction(Handle h, float& force, float& torque) const override
        {
            force = torque = 0.0f;
            auto it = m_Joints.find(h);
            if (it == m_Joints.end() || !m_Model || m_Dirty)
                return;
            const JointRec& j = it->second;
            double f2 = 0.0, t2 = 0.0;
            for (int i = 0; i < m_Data->nefc; ++i)
            {
                if (m_Data->efc_type[i] != mjCNSTR_EQUALITY)
                    continue;
                int id = m_Data->efc_id[i];
                for (size_t k = 0; k < j.Equalities.size(); ++k)
                    if (id == j.Equalities[k])
                    {
                        double v = m_Data->efc_force[i];
                        // weld: ilk 3 satır öteleme, sonraki 3 dönme
                        int row = i - FirstRowOf(id);
                        (row >= 3 ? t2 : f2) += v * v;
                    }
            }
            force = (float)std::sqrt(f2);
            torque = (float)std::sqrt(t2);
        }

        void SetJointMotor(Handle h, bool enabled, float targetVelocity, float maxForce) override
        {
            auto it = m_Joints.find(h);
            if (it == m_Joints.end()) return;
            it->second.Desc.UseMotor = enabled;
            it->second.Desc.MotorTargetVelocity = targetVelocity;
            it->second.Desc.MotorMaxForce = maxForce;
        }

        float GetJointPosition(Handle h) const override
        {
            auto it = m_Joints.find(h);
            if (it == m_Joints.end()) return 0.0f;
            const JointRec& j = it->second;
            // Menteşe açısı: A'ya göre B'nin eksen etrafındaki dönüşü
            glm::vec3 pa; glm::quat qa; glm::vec3 pb(0.0f); glm::quat qb(1, 0, 0, 0);
            GetBodyTransform(j.Desc.BodyA, pa, qa);
            if (j.Desc.BodyB) GetBodyTransform(j.Desc.BodyB, pb, qb);
            glm::quat rel = glm::inverse(qb) * qa * glm::inverse(j.InitialRelative);
            glm::vec3 axis = glm::normalize(j.LocalAxisA);
            float angle = 2.0f * std::atan2(glm::dot(glm::vec3(rel.x, rel.y, rel.z), axis), rel.w);
            return angle;
        }

        // ── Karakterler (mocap kapsül + mj_geomDistance ile çarp-kay) ──
        Handle CreateCharacter(const CharacterDesc& d) override
        {
            if (!m_Initialized) return 0;
            Handle h = ++m_NextHandle;
            CharRec& c = m_Characters[h];
            c.Desc = d;
            c.Pos = d.Position;
            c.State.Position = d.Position;
            m_Dirty = true;
            return h;
        }

        void DestroyCharacter(Handle h) override
        {
            if (m_Characters.erase(h))
                m_Dirty = true;
        }

        CharacterState MoveCharacter(Handle h, const glm::vec3& displacement, float dt) override
        {
            auto it = m_Characters.find(h);
            if (it == m_Characters.end() || !EnsureModel())
                return {};
            CharRec& c = it->second;
            glm::vec3 start = c.Pos;
            const float radius = std::max(0.01f, c.Desc.Radius);
            // Tünellemeyi önlemek için yarıçapın yarısından büyük adımlar bölünür
            int steps = std::clamp((int)std::ceil(glm::length(displacement) / (radius * 0.5f)), 1, 16);
            glm::vec3 step = displacement / (float)steps;
            uint32_t flags = CollidedNone;
            bool grounded = false;
            glm::vec3 groundNormal(0, 1, 0);
            uint32_t groundEntity = NoEntity;
            const float cosSlope = std::cos(c.Desc.SlopeLimit);
            for (int s = 0; s < steps; ++s)
            {
                glm::vec3 target = c.Pos + step;
                glm::vec3 resolved = ResolveCharacter(c, target, flags, grounded, groundNormal, groundEntity, cosSlope);
                // Basamak: yatay engelde yukarı kaldırıp tekrar dene
                if ((flags & CollidedSides) && c.Desc.StepOffset > 0.0f && glm::length(glm::vec2(step.x, step.z)) > 1e-5f)
                {
                    uint32_t f2 = CollidedNone; bool g2 = false; glm::vec3 n2(0, 1, 0); uint32_t ge2 = NoEntity;
                    glm::vec3 up = ResolveCharacter(c, c.Pos + glm::vec3(0, c.Desc.StepOffset, 0), f2, g2, n2, ge2, cosSlope);
                    glm::vec3 fwd = ResolveCharacter(c, up + glm::vec3(step.x, 0, step.z), f2 = CollidedNone, g2, n2, ge2, cosSlope);
                    glm::vec3 down = ResolveCharacter(c, fwd - glm::vec3(0, c.Desc.StepOffset, 0), f2, g2, n2, ge2, cosSlope);
                    float gainStepped = glm::length(glm::vec2(down.x - c.Pos.x, down.z - c.Pos.z));
                    float gainFlat = glm::length(glm::vec2(resolved.x - c.Pos.x, resolved.z - c.Pos.z));
                    if (g2 && gainStepped > gainFlat + 1e-4f)
                    {
                        resolved = down;
                        grounded = true;
                        groundNormal = n2;
                    }
                }
                c.Pos = resolved;
            }
            WriteCharacterGeom(c, c.Pos);
            c.State.Position = c.Pos;
            c.State.Velocity = (c.Pos - start) / std::max(dt, 1e-4f);
            c.State.Grounded = grounded;
            c.State.GroundNormal = groundNormal;
            c.State.GroundEntity = groundEntity;
            c.State.Flags = flags | (grounded ? CollidedBelow : 0u);
            if (c.Mocap >= 0)
                WriteMocap(c.Mocap, c.Pos, glm::quat(1, 0, 0, 0));
            return c.State;
        }

        void SetCharacterPosition(Handle h, const glm::vec3& position) override
        {
            auto it = m_Characters.find(h);
            if (it == m_Characters.end()) return;
            it->second.Pos = position;
            it->second.State.Position = position;
            if (m_Model && !m_Dirty && it->second.Mocap >= 0)
            {
                WriteMocap(it->second.Mocap, position, glm::quat(1, 0, 0, 0));
                WriteCharacterGeom(it->second, position);
            }
        }

        CharacterState GetCharacterState(Handle h) const override
        {
            auto it = m_Characters.find(h);
            return it == m_Characters.end() ? CharacterState{} : it->second.State;
        }

        // ── Sorgular ──
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RaycastHit& hit) const override
        {
            std::vector<RaycastHit> hits;
            CastRays(origin, direction, maxDistance, filter, hits, true);
            if (hits.empty()) return false;
            hit = hits.front();
            return true;
        }

        void RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, std::vector<RaycastHit>& hits) const override
        {
            CastRays(origin, direction, maxDistance, filter, hits, false);
        }

        bool SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RaycastHit& hit) const override
        {
            // Yaklaşık: merkez + 4 kenar ışını; en yakını
            if (glm::length(direction) < 1e-8f) return false;
            glm::vec3 d = glm::normalize(direction);
            glm::vec3 t = std::fabs(d.y) < 0.9f ? glm::normalize(glm::cross(d, glm::vec3(0, 1, 0))) : glm::normalize(glm::cross(d, glm::vec3(1, 0, 0)));
            glm::vec3 b = glm::cross(d, t);
            const glm::vec3 offsets[] = { glm::vec3(0.0f), t * radius, -t * radius, b * radius, -b * radius };
            bool any = false;
            for (auto& o : offsets)
            {
                RaycastHit h;
                if (Raycast(origin + o, d, maxDistance, filter, h) && (!any || h.Distance < hit.Distance))
                {
                    hit = h;
                    any = true;
                }
            }
            return any;
        }

        void OverlapSphere(const glm::vec3& center, float radius, const QueryFilter& filter, std::vector<uint32_t>& entities) const override
        {
            ShapeDesc s;
            s.Type = ShapeType::Sphere;
            s.Radius = radius;
            Overlap(s, center, glm::quat(1, 0, 0, 0), filter, entities);
        }

        void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const QueryFilter& filter, std::vector<uint32_t>& entities) const override
        {
            ShapeDesc s;
            s.Type = ShapeType::Box;
            s.HalfExtents = halfExtents;
            Overlap(s, center, rotation, filter, entities);
        }

        void DrainContactEvents(std::vector<ContactEvent>& events) override
        {
            events.insert(events.end(), m_Events.begin(), m_Events.end());
            m_Events.clear();
        }

        int GetBodyCount() const override { return (int)m_Bodies.size() + (int)m_Characters.size(); }
        int GetActiveBodyCount() const override
        {
            int n = 0;
            for (auto& [h, b] : m_Bodies) if (b.Desc.Motion != MotionType::Static) ++n;
            return n;
        }

    private:
        struct BodyRec
        {
            BodyDesc Desc;
            glm::vec3 Pos{ 0.0f }, LinVel{ 0.0f }, AngVel{ 0.0f };
            glm::quat Rot{ 1, 0, 0, 0 };
            glm::vec3 KinPos{ 0.0f };
            glm::quat KinRot{ 1, 0, 0, 0 };
            bool HasKinTarget = false;
            glm::vec3 Force{ 0.0f }, Torque{ 0.0f };
            int MjBody = -1, Mocap = -1, QposAdr = -1, DofAdr = -1;
            std::vector<int> Geoms;
            std::vector<int> TriggerGeoms;
        };

        struct JointRec
        {
            JointDesc Desc;
            glm::vec3 LocalAnchorA{ 0.0f }, LocalAnchorB{ 0.0f }, LocalAxisA{ 1, 0, 0 };
            glm::quat InitialRelative{ 1, 0, 0, 0 };
            std::vector<int> Equalities;
            int Tendon = -1;
        };

        struct CharRec
        {
            CharacterDesc Desc;
            glm::vec3 Pos{ 0.0f };
            CharacterState State;
            int MjBody = -1, Mocap = -1, Geom = -1;
        };

        struct GeomInfo
        {
            Handle Body = 0;          // 0: karakter / sorgu
            uint32_t Entity = NoEntity;
            int Layer = 0;
            bool Trigger = false;
            bool Character = false;
            bool Static = false;
        };

        struct PairState
        {
            uint32_t A = NoEntity, B = NoEntity;
            bool Trigger = false;
        };

        BodyRec* DynamicBody(Handle h)
        {
            auto it = m_Bodies.find(h);
            return it != m_Bodies.end() && it->second.Desc.Motion == MotionType::Dynamic ? &it->second : nullptr;
        }

        glm::vec3 ComOf(const BodyRec& b) const
        {
            if (m_Model && !m_Dirty && b.MjBody >= 0)
            {
                const mjtNum* c = m_Data->xipos + 3 * b.MjBody;
                return glm::vec3((float)c[0], (float)c[1], (float)c[2]);
            }
            return b.Pos;
        }

        glm::vec3 ToLocal(Handle body, const glm::vec3& world) const
        {
            glm::vec3 p; glm::quat q;
            GetBodyTransform(body, p, q);
            return glm::inverse(q) * (world - p);
        }
        glm::vec3 ToLocalDir(Handle body, const glm::vec3& dir) const
        {
            glm::vec3 p; glm::quat q;
            GetBodyTransform(body, p, q);
            return glm::inverse(q) * dir;
        }

        int FirstRowOf(int eqId) const
        {
            for (int i = 0; i < m_Data->nefc; ++i)
                if (m_Data->efc_type[i] == mjCNSTR_EQUALITY && m_Data->efc_id[i] == eqId)
                    return i;
            return 0;
        }

        void WriteFreeJoint(BodyRec& b, const glm::vec3& p, const glm::quat& q)
        {
            mjtNum* qp = m_Data->qpos + b.QposAdr;
            glm::quat n = glm::normalize(q);
            qp[0] = p.x; qp[1] = p.y; qp[2] = p.z;
            qp[3] = n.w; qp[4] = n.x; qp[5] = n.y; qp[6] = n.z;
        }

        void WriteMocap(int mocap, const glm::vec3& p, const glm::quat& q)
        {
            glm::quat n = glm::normalize(q);
            mjtNum* mp = m_Data->mocap_pos + 3 * mocap;
            mjtNum* mq = m_Data->mocap_quat + 4 * mocap;
            mp[0] = p.x; mp[1] = p.y; mp[2] = p.z;
            mq[0] = n.w; mq[1] = n.x; mq[2] = n.y; mq[3] = n.z;
        }

        // Karakter kapsülünün çarpışma pozunu doğrudan yaz (mj_kinematics çağırmadan)
        void WriteCharacterGeom(const CharRec& c, const glm::vec3& pos)
        {
            if (c.Geom < 0) return;
            glm::vec3 center = pos + c.Desc.Center;
            mjtNum* gp = m_Data->geom_xpos + 3 * c.Geom;
            gp[0] = center.x; gp[1] = center.y; gp[2] = center.z;
            // Kapsül ekseni dünya Y (yerel Z -> Y): satır-ana 3x3
            static const mjtNum kMat[9] = { 1, 0, 0, 0, 0, 1, 0, -1, 0 };
            std::memcpy(m_Data->geom_xmat + 9 * c.Geom, kMat, sizeof(kMat));
        }

        glm::vec3 ResolveCharacter(CharRec& c, glm::vec3 target, uint32_t& flags, bool& grounded, glm::vec3& groundNormal, uint32_t& groundEntity, float cosSlope)
        {
            const float skin = std::max(0.005f, c.Desc.SkinWidth);
            for (int iter = 0; iter < 6; ++iter)
            {
                WriteCharacterGeom(c, target);
                glm::vec3 push(0.0f);
                bool any = false;
                glm::vec3 cc = target + c.Desc.Center;
                float reach = c.Desc.Height * 0.5f + c.Desc.Radius + skin * 2.0f;
                for (int g = 0; g < (int)m_GeomInfo.size(); ++g)
                {
                    const GeomInfo& gi = m_GeomInfo[(size_t)g];
                    if (g == c.Geom || gi.Trigger || gi.Character || gi.Body == 0)
                        continue;
                    if (!m_Settings.LayersCollide(c.Desc.Layer, gi.Layer))
                        continue;
                    const mjtNum* gp = m_Data->geom_xpos + 3 * g;
                    float dx = (float)gp[0] - cc.x, dy = (float)gp[1] - cc.y, dz = (float)gp[2] - cc.z;
                    float rb = (float)m_Model->geom_rbound[g];
                    if (rb > 0.0f && std::sqrt(dx * dx + dy * dy + dz * dz) > rb + reach)
                        continue;
                    mjtNum fromto[6];
                    mjtNum dist = Api().geomDistance(m_Model, m_Data, c.Geom, g, skin * 2.0, fromto);
                    if (dist >= skin)
                        continue;
                    glm::vec3 p1((float)fromto[0], (float)fromto[1], (float)fromto[2]);
                    glm::vec3 p2((float)fromto[3], (float)fromto[4], (float)fromto[5]);
                    glm::vec3 n = p1 - p2;
                    if (dist < 0.0f) n = -n;
                    float len = glm::length(n);
                    n = len > 1e-6f ? n / len : glm::vec3(0, 1, 0);
                    push += n * (float)(skin - dist);
                    any = true;
                    if (n.y >= cosSlope)
                    {
                        grounded = true;
                        groundNormal = n;
                        groundEntity = gi.Entity;
                    }
                    else if (n.y < -0.5f)
                        flags |= CollidedAbove;
                    else
                        flags |= CollidedSides;
                }
                if (!any)
                    break;
                // Dik yüzeylerde kaymayı önlemek için zemin itmesini yalnız dikey uygula
                target += push;
            }
            return target;
        }

        void CastRays(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, std::vector<RaycastHit>& hits, bool firstOnly) const
        {
            hits.clear();
            if (glm::length(direction) < 1e-8f || !const_cast<MuJoCoBackend*>(this)->EnsureModel())
                return;
            glm::vec3 d = glm::normalize(direction);
            mjtNum vec[3] = { d.x, d.y, d.z };
            glm::vec3 from = origin;
            float travelled = 0.0f;
            std::set<int> excludedGeoms;
            for (int guard = 0; guard < 64 && travelled <= maxDistance; ++guard)
            {
                mjtNum pnt[3] = { from.x, from.y, from.z };
                int geom = -1;
                mjtNum normal[3] = { 0, 1, 0 };
                mjtNum dist = Api().ray(m_Model, m_Data, pnt, vec, nullptr, 1, -1, &geom, normal);
                if (dist < 0.0 || geom < 0 || travelled + (float)dist > maxDistance)
                    break;
                travelled += (float)dist;
                const GeomInfo& gi = m_GeomInfo[(size_t)geom];
                bool pass = gi.Body != 0 || gi.Character;
                if (pass && !((filter.LayerMask >> gi.Layer) & 1u)) pass = false;
                if (pass && gi.Trigger && !filter.HitTriggers) pass = false;
                if (pass && filter.IgnoreEntity != NoEntity && gi.Entity == filter.IgnoreEntity) pass = false;
                if (pass && filter.IgnoreBody && gi.Body == filter.IgnoreBody) pass = false;
                if (pass)
                {
                    RaycastHit h;
                    h.Entity = gi.Entity;
                    h.Distance = travelled;
                    h.Point = origin + d * travelled;
                    h.Normal = glm::vec3((float)normal[0], (float)normal[1], (float)normal[2]);
                    h.IsTrigger = gi.Trigger;
                    hits.push_back(h);
                    if (firstOnly)
                        return;
                }
                // Geom'un arkasından devam et
                const float advance = 1e-3f;
                travelled += advance;
                from = origin + d * travelled;
            }
        }

        void Overlap(const ShapeDesc& shape, const glm::vec3& center, const glm::quat& rotation, const QueryFilter& filter, std::vector<uint32_t>& entities) const
        {
            entities.clear();
            auto* self = const_cast<MuJoCoBackend*>(this);
            if (!self->EnsureModel() || m_QueryGeom < 0)
                return;
            // Gizli sorgu geom'unu şekle uyarla
            int q = m_QueryGeom;
            mjtNum* size = m_Model->geom_size + 3 * q;
            if (shape.Type == ShapeType::Sphere)
            {
                m_Model->geom_type[q] = mjGEOM_SPHERE;
                size[0] = std::max(0.001f, shape.Radius);
                m_Model->geom_rbound[q] = size[0];
            }
            else
            {
                m_Model->geom_type[q] = mjGEOM_BOX;
                glm::vec3 he = glm::max(glm::abs(shape.HalfExtents), glm::vec3(0.001f));
                size[0] = he.x; size[1] = he.y; size[2] = he.z;
                m_Model->geom_rbound[q] = glm::length(he);
            }
            mjtNum* gp = m_Data->geom_xpos + 3 * q;
            gp[0] = center.x; gp[1] = center.y; gp[2] = center.z;
            glm::mat3 R = glm::mat3_cast(glm::normalize(rotation));
            mjtNum* gm = m_Data->geom_xmat + 9 * q;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    gm[r * 3 + c] = R[c][r];
            std::set<uint32_t> unique;
            for (int g = 0; g < (int)m_GeomInfo.size(); ++g)
            {
                const GeomInfo& gi = m_GeomInfo[(size_t)g];
                if (g == q || (gi.Body == 0 && !gi.Character))
                    continue;
                if (!((filter.LayerMask >> gi.Layer) & 1u) || (gi.Trigger && !filter.HitTriggers) || gi.Entity == filter.IgnoreEntity
                    || (filter.IgnoreBody && gi.Body == filter.IgnoreBody))
                    continue;
                const mjtNum* p = m_Data->geom_xpos + 3 * g;
                float dist = glm::length(glm::vec3((float)p[0], (float)p[1], (float)p[2]) - center);
                if (dist > (float)(m_Model->geom_rbound[g] + m_Model->geom_rbound[q]) && m_Model->geom_rbound[g] > 0.0)
                    continue;
                if (Api().geomDistance(m_Model, m_Data, q, g, 0.0, nullptr) <= 0.0 && unique.insert(gi.Entity).second)
                    entities.push_back(gi.Entity);
            }
        }

        void ApplyPendingForces()
        {
            for (auto& [h, b] : m_Bodies)
            {
                if (b.MjBody < 0 || b.Desc.Motion != MotionType::Dynamic)
                    continue;
                mjtNum* f = m_Data->xfrc_applied + 6 * b.MjBody;
                f[0] = b.Force.x; f[1] = b.Force.y; f[2] = b.Force.z;
                f[3] = b.Torque.x; f[4] = b.Torque.y; f[5] = b.Torque.z;
                // Kilitli eksenler: hızları sıfırla (MuJoCo'da serbest eklemde DOF kilidi yok)
                if (b.Desc.Locks && b.DofAdr >= 0)
                {
                    mjtNum* v = m_Data->qvel + b.DofAdr;
                    if (b.Desc.Locks & LockPosX) v[0] = 0;
                    if (b.Desc.Locks & LockPosY) v[1] = 0;
                    if (b.Desc.Locks & LockPosZ) v[2] = 0;
                    if (b.Desc.Locks & (LockRotX | LockRotY | LockRotZ))
                    {
                        glm::vec3 w = GetAngularVelocity(h);
                        if (b.Desc.Locks & LockRotX) w.x = 0;
                        if (b.Desc.Locks & LockRotY) w.y = 0;
                        if (b.Desc.Locks & LockRotZ) w.z = 0;
                        SetAngularVelocity(h, w);
                    }
                }
            }
        }

        void ClearAppliedForces()
        {
            for (auto& [h, b] : m_Bodies)
            {
                b.Force = glm::vec3(0.0f);
                b.Torque = glm::vec3(0.0f);
                if (b.MjBody >= 0)
                    std::memset(m_Data->xfrc_applied + 6 * b.MjBody, 0, sizeof(mjtNum) * 6);
            }
        }

        void ApplyKinematicTargets()
        {
            for (auto& [h, b] : m_Bodies)
                if (b.Mocap >= 0 && b.HasKinTarget)
                {
                    WriteMocap(b.Mocap, b.KinPos, b.KinRot);
                    b.HasKinTarget = false;
                }
        }

        // Menteşe motoru: eksen etrafındaki göreli açısal hız hedefe torkla sürülür
        void ApplyMotors()
        {
            for (auto& [h, j] : m_Joints)
            {
                if (j.Desc.Type != JointType::Hinge || !j.Desc.UseMotor)
                    continue;
                BodyRec* a = DynamicBody(j.Desc.BodyA);
                BodyRec* b = j.Desc.BodyB ? DynamicBody(j.Desc.BodyB) : nullptr;
                if (!a || a->MjBody < 0) continue;
                glm::vec3 p; glm::quat q;
                GetBodyTransform(j.Desc.BodyA, p, q);
                glm::vec3 axis = glm::normalize(q * j.LocalAxisA);
                float rel = glm::dot(GetAngularVelocity(j.Desc.BodyA) - (b ? GetAngularVelocity(j.Desc.BodyB) : glm::vec3(0.0f)), axis);
                float err = j.Desc.MotorTargetVelocity - rel;
                float inertia = (float)(m_Model->body_inertia[3 * a->MjBody] + m_Model->body_inertia[3 * a->MjBody + 1] + m_Model->body_inertia[3 * a->MjBody + 2]) / 3.0f;
                float torque = std::clamp(err * inertia / std::max(1e-4f, m_LastDt), -j.Desc.MotorMaxForce, j.Desc.MotorMaxForce);
                a->Torque += axis * torque;
                if (b) b->Torque -= axis * torque;
            }
        }

        void SyncBodyState(BodyRec& b)
        {
            if (!m_Model || b.MjBody < 0)
                return;
            GetBodyTransform(m_HandleOf(b), b.Pos, b.Rot);
        }

        Handle m_HandleOf(const BodyRec& rec) const
        {
            for (auto& [h, b] : m_Bodies) if (&b == &rec) return h;
            return 0;
        }

        // ── Model ──
        void FreeModel()
        {
            if (m_Data) Api().deleteData(m_Data);
            if (m_Model) Api().deleteModel(m_Model);
            m_Data = nullptr;
            m_Model = nullptr;
            m_GeomInfo.clear();
            m_QueryGeom = -1;
        }

        bool EnsureModel()
        {
            if (!m_Initialized)
                return false;
            if (!m_Dirty && m_Model)
                return true;
            if (!m_Dirty && m_Failed)
                return false; // yapısal bir değişiklik olana kadar yeniden deneme

            // Mevcut durumu aktar
            if (m_Model && m_Data)
                for (auto& [h, b] : m_Bodies)
                {
                    if (b.MjBody < 0) continue;
                    GetBodyTransform(h, b.Pos, b.Rot);
                    if (b.DofAdr >= 0)
                    {
                        b.LinVel = GetLinearVelocity(h);
                        b.AngVel = GetAngularVelocity(h);
                    }
                    b.MjBody = b.Mocap = b.QposAdr = b.DofAdr = -1;
                }
            m_Dirty = false;
            FreeModel();
            std::string xml = BuildXml();
            mjVFS vfs;
            Api().defaultVFS(&vfs);
            Api().addBufferVFS(&vfs, "scene.xml", xml.data(), (int)xml.size());
            char error[1024] = {};
            m_Model = Api().loadXML("scene.xml", &vfs, error, sizeof(error));
            Api().deleteVFS(&vfs);
            if (!m_Model)
            {
                CQ_CORE_ERROR("[Physics] MuJoCo model compile failed: {0}", error);
                m_Failed = true;
                return false;
            }
            m_Failed = false;
            m_Data = Api().makeData(m_Model);
            ResolveIds();
            // Başlangıç durumları
            for (auto& [h, b] : m_Bodies)
            {
                if (b.QposAdr >= 0)
                {
                    WriteFreeJoint(b, b.Pos, b.Rot);
                    for (int i = 0; i < 3; ++i) m_Data->qvel[b.DofAdr + i] = b.LinVel[i];
                }
                if (b.Mocap >= 0)
                    WriteMocap(b.Mocap, b.KinPos, b.KinRot);
            }
            for (auto& [h, c] : m_Characters)
                if (c.Mocap >= 0)
                    WriteMocap(c.Mocap, c.Pos, glm::quat(1, 0, 0, 0));
            Api().forward(m_Model, m_Data);
            for (auto& [h, b] : m_Bodies)
                if (b.DofAdr >= 0)
                    SetAngularVelocity(h, b.AngVel);
            // Eklemlerin başlangıç göreli dönüşleri
            for (auto& [h, j] : m_Joints)
            {
                glm::vec3 pa; glm::quat qa; glm::vec3 pb(0.0f); glm::quat qb(1, 0, 0, 0);
                GetBodyTransform(j.Desc.BodyA, pa, qa);
                if (j.Desc.BodyB) GetBodyTransform(j.Desc.BodyB, pb, qb);
                if (j.InitialRelative == glm::quat(1, 0, 0, 0))
                    j.InitialRelative = glm::inverse(qb) * qa;
            }
            return true;
        }

        std::string GeomXml(const ShapeDesc& s, const std::string& name, const BodyDesc* body, float massShare, std::ostringstream& assets, int& assetCounter)
        {
            std::ostringstream g;
            g << "<geom name=\"" << name << "\"";
            glm::quat rot = s.Rotation;
            switch (s.Type)
            {
                case ShapeType::Box:
                    g << " type=\"box\" size=\"" << V3(glm::max(glm::abs(s.HalfExtents), glm::vec3(1e-4f))) << "\"";
                    break;
                case ShapeType::Sphere:
                    g << " type=\"sphere\" size=\"" << F(std::max(1e-4f, s.Radius)) << "\"";
                    break;
                case ShapeType::Capsule:
                    if (s.HalfHeight <= 1e-4f)
                        g << " type=\"sphere\" size=\"" << F(std::max(1e-4f, s.Radius)) << "\"";
                    else
                        g << " type=\"capsule\" size=\"" << F(std::max(1e-4f, s.Radius)) << " " << F(s.HalfHeight) << "\"";
                    rot = rot * kYToZ;
                    break;
                case ShapeType::Cylinder:
                    g << " type=\"cylinder\" size=\"" << F(std::max(1e-4f, s.Radius)) << " " << F(std::max(1e-4f, s.HalfHeight)) << "\"";
                    rot = rot * kYToZ;
                    break;
                case ShapeType::ConvexHull:
                case ShapeType::TriangleMesh:
                {
                    std::string mesh = "m" + std::to_string(assetCounter++);
                    assets << "<mesh name=\"" << mesh << "\" vertex=\"";
                    for (auto& v : s.Vertices) assets << V3(v) << " ";
                    assets << "\"/>\n";
                    g << " type=\"mesh\" mesh=\"" << mesh << "\"";
                    break;
                }
                case ShapeType::HeightField:
                {
                    std::string hf = "h" + std::to_string(assetCounter++);
                    const int rows = s.HFRows, cols = s.HFCols;
                    // MuJoCo hfield: yerel XY düzlemi, Z yükseklik; satırlar +Y. Yerel Y -> dünya -Z olduğundan satırlar ters çevrilir
                    assets << "<hfield name=\"" << hf << "\" nrow=\"" << rows << "\" ncol=\"" << cols << "\" size=\""
                           << F(s.HFSize.x * 0.5f) << " " << F(s.HFSize.z * 0.5f) << " " << F(std::max(1e-4f, s.HFSize.y)) << " " << F(0.1) << "\" elevation=\"";
                    for (int r = rows - 1; r >= 0; --r)
                        for (int c = 0; c < cols; ++c)
                            assets << F(std::clamp(s.HFHeights[(size_t)r * cols + c], 0.0f, 1.0f)) << " ";
                    assets << "\"/>\n";
                    g << " type=\"hfield\" hfield=\"" << hf << "\"";
                    // Köşe kökenli yerel uzaydan merkeze kaydır, Z yukarıyı Y yukarıya çevir
                    glm::vec3 centerOffset(s.HFSize.x * 0.5f, 0.0f, s.HFSize.z * 0.5f);
                    g << " pos=\"" << V3(s.Offset + s.Rotation * centerOffset) << "\" quat=\"" << Q(s.Rotation * glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0))) << "\"";
                    break;
                }
            }
            if (s.Type != ShapeType::HeightField)
                g << " pos=\"" << V3(s.Offset) << "\" quat=\"" << Q(rot) << "\"";
            int layer = body ? std::clamp(body->Layer, 0, 31) : 0;
            if (s.IsTrigger)
                g << " contype=\"0\" conaffinity=\"0\"";
            else
                g << " contype=\"" << LayerBit(layer) << "\" conaffinity=\"" << AffinityMask(m_Settings, layer) << "\"";
            if (body)
            {
                g << " friction=\"" << F(body->Friction) << " 0.005 0.0001\"";
                if (body->Motion == MotionType::Dynamic && !s.IsTrigger)
                    g << " mass=\"" << F(std::max(1e-5f, massShare)) << "\"";
                else if (body->Motion == MotionType::Dynamic)
                    g << " mass=\"0\" density=\"0\"";
                // Sekme: solref'in sönüm oranı ile
                if (body->Restitution > 0.01f)
                    g << " solref=\"0.02 " << F(std::max(0.05f, 1.0f - body->Restitution)) << "\"";
            }
            g << "/>";
            return g.str();
        }

        std::string BuildXml()
        {
            std::ostringstream assets, world, equality, tendon;
            int assetCounter = 0;
            const char* integrators[] = { "Euler", "RK4", "implicit", "implicitfast" };
            for (auto& [h, b] : m_Bodies)
            {
                const BodyDesc& d = b.Desc;
                const bool dynamic = d.Motion == MotionType::Dynamic;
                const bool kinematic = d.Motion == MotionType::Kinematic;
                world << "<body name=\"b" << h << "\" pos=\"" << V3(b.Pos) << "\" quat=\"" << Q(b.Rot) << "\"";
                if (kinematic) world << " mocap=\"true\"";
                // Gövde başına yerçekimi ölçeği: MuJoCo yerçekimi telafisi (1 = yerçekimsiz)
                if (dynamic && std::fabs(d.GravityFactor - 1.0f) > 1e-4f)
                    world << " gravcomp=\"" << F(1.0f - d.GravityFactor) << "\"";
                world << ">\n";
                if (dynamic)
                {
                    // Serbest eklem sönümü 6 DOF'a eşit uygulanır (viskoz katsayı ~ sürükleme * kütle)
                    float damping = std::max(0.0f, std::max(d.LinearDamping, d.AngularDamping)) * std::max(1e-4f, d.Mass);
                    world << "<joint name=\"j" << h << "\" type=\"free\" damping=\"" << F(damping) << "\"/>\n";
                }
                float totalVol = 0.0f;
                for (auto& s : d.Shapes) if (!s.IsTrigger) totalVol += ShapeVolume(s);
                int k = 0;
                for (auto& s : d.Shapes)
                {
                    float share = totalVol > 0.0f ? d.Mass * ShapeVolume(s) / totalVol : d.Mass;
                    world << GeomXml(s, "g" + std::to_string(h) + "_" + std::to_string(k++), &d, share, assets, assetCounter) << "\n";
                }
                // Eklem siteleri
                for (auto& [jh, j] : m_Joints)
                {
                    if (j.Desc.Type != JointType::Spring) continue;
                    if (j.Desc.BodyA == h) world << "<site name=\"sa" << jh << "\" pos=\"" << V3(j.LocalAnchorA) << "\"/>\n";
                    if (j.Desc.BodyB == h) world << "<site name=\"sb" << jh << "\" pos=\"" << V3(j.LocalAnchorB) << "\"/>\n";
                }
                world << "</body>\n";
            }
            // Dünyaya bağlı yay siteleri
            for (auto& [jh, j] : m_Joints)
                if (j.Desc.Type == JointType::Spring && !j.Desc.BodyB)
                    world << "<site name=\"sb" << jh << "\" pos=\"" << V3(j.LocalAnchorB) << "\"/>\n";
            // Karakterler: mocap kapsül (dinamik gövdeleri iter)
            for (auto& [h, c] : m_Characters)
            {
                float radius = std::max(0.01f, c.Desc.Radius);
                float half = std::max(0.0f, c.Desc.Height * 0.5f - radius);
                int layer = std::clamp(c.Desc.Layer, 0, 31);
                world << "<body name=\"c" << h << "\" mocap=\"true\" pos=\"" << V3(c.Pos) << "\">\n";
                world << "<geom name=\"cg" << h << "\" type=\"" << (half > 1e-4f ? "capsule" : "sphere") << "\" size=\"" << F(radius)
                      << (half > 1e-4f ? " " + F(half) : std::string()) << "\" pos=\"" << V3(c.Desc.Center) << "\" quat=\"" << Q(kYToZ)
                      << "\" contype=\"" << LayerBit(layer) << "\" conaffinity=\"" << AffinityMask(m_Settings, layer) << "\"/>\n";
                world << "</body>\n";
            }
            // Sorgu geom'u (çarpışmasız)
            world << "<body name=\"__query\" mocap=\"true\" pos=\"0 -100000 0\"><geom name=\"__qg\" type=\"box\" size=\"0.5 0.5 0.5\" contype=\"0\" conaffinity=\"0\"/></body>\n";

            for (auto& [jh, j] : m_Joints)
            {
                const JointDesc& d = j.Desc;
                std::string b1 = "b" + std::to_string(d.BodyA);
                std::string b2 = d.BodyB ? "b" + std::to_string(d.BodyB) : "world";
                std::string sol = " solref=\"0.005 1\"";
                switch (d.Type)
                {
                    case JointType::Fixed:
                        equality << "<weld name=\"q" << jh << "_0\" body1=\"" << b1 << "\" body2=\"" << b2 << "\"" << sol << "/>\n";
                        break;
                    case JointType::Ball:
                    case JointType::Character:
                        equality << "<connect name=\"q" << jh << "_0\" body1=\"" << b1 << "\" body2=\"" << b2 << "\" anchor=\"" << V3(j.LocalAnchorA) << "\"" << sol << "/>\n";
                        break;
                    case JointType::SixDOF:
                    {
                        const SixDOFDesc& x = d.SixDOF;
                        int locked = 0, freeAxis = -1;
                        for (int i = 0; i < 3; ++i)
                        {
                            if (x.Angular[i] == AxisMotion::Locked) ++locked;
                            else freeAxis = i;
                        }
                        if (locked == 3)
                            equality << "<weld name=\"q" << jh << "_0\" body1=\"" << b1 << "\" body2=\"" << b2 << "\"" << sol << "/>\n";
                        else if (locked == 2)
                        {
                            // Tek serbest dönme ekseni: iki bağlantı noktasıyla menteşe
                            glm::vec3 ax = glm::length(j.LocalAxisA) > 1e-6f ? glm::normalize(j.LocalAxisA) : glm::vec3(1, 0, 0);
                            if (freeAxis != 0)
                            {
                                glm::vec3 sec = ToLocalDir(d.BodyA, x.SecondaryAxisWorld);
                                sec = glm::length(sec) > 1e-6f ? glm::normalize(sec) : glm::vec3(0, 1, 0);
                                ax = freeAxis == 1 ? sec : glm::normalize(glm::cross(ax, sec));
                            }
                            equality << "<connect name=\"q" << jh << "_0\" body1=\"" << b1 << "\" body2=\"" << b2 << "\" anchor=\"" << V3(j.LocalAnchorA + ax * 0.25f) << "\"" << sol << "/>\n";
                            equality << "<connect name=\"q" << jh << "_1\" body1=\"" << b1 << "\" body2=\"" << b2 << "\" anchor=\"" << V3(j.LocalAnchorA - ax * 0.25f) << "\"" << sol << "/>\n";
                        }
                        else
                            equality << "<connect name=\"q" << jh << "_0\" body1=\"" << b1 << "\" body2=\"" << b2 << "\" anchor=\"" << V3(j.LocalAnchorA) << "\"" << sol << "/>\n";
                        break;
                    }
                    case JointType::Hinge:
                    {
                        glm::vec3 axis = glm::length(j.LocalAxisA) > 1e-6f ? glm::normalize(j.LocalAxisA) : glm::vec3(1, 0, 0);
                        equality << "<connect name=\"q" << jh << "_0\" body1=\"" << b1 << "\" body2=\"" << b2 << "\" anchor=\"" << V3(j.LocalAnchorA + axis * 0.25f) << "\"" << sol << "/>\n";
                        equality << "<connect name=\"q" << jh << "_1\" body1=\"" << b1 << "\" body2=\"" << b2 << "\" anchor=\"" << V3(j.LocalAnchorA - axis * 0.25f) << "\"" << sol << "/>\n";
                        break;
                    }
                    case JointType::Spring:
                    {
                        float rest = d.MaxDistance > 0.0f ? d.MaxDistance : (d.RestLength >= 0.0f ? d.RestLength : glm::distance(d.AnchorWorld, d.ConnectedAnchorWorld));
                        tendon << "<spatial name=\"t" << jh << "\" stiffness=\"" << F(std::max(0.0f, d.SpringStiffness)) << "\" damping=\""
                               << F(std::max(0.0f, d.SpringDamping)) << "\" springlength=\"" << F(rest) << "\"";
                        if (d.MaxDistance > 0.0f)
                            tendon << " limited=\"true\" range=\"" << F(std::max(0.0f, d.MinDistance)) << " " << F(d.MaxDistance) << "\"";
                        tendon << "><site site=\"sa" << jh << "\"/><site site=\"sb" << jh << "\"/></spatial>\n";
                        break;
                    }
                    default: break;
                }
            }

            std::ostringstream xml;
            xml << "<mujoco model=\"conqueror\">\n";
            xml << "<compiler angle=\"radian\" autolimits=\"true\" boundmass=\"1e-6\" boundinertia=\"1e-9\"/>\n";
            xml << "<option timestep=\"" << F(m_Settings.FixedTimestep) << "\" gravity=\"" << V3(m_Settings.Gravity) << "\" integrator=\""
                << integrators[std::clamp((int)m_Settings.Integrator, 0, 3)] << "\" iterations=\"" << std::max(1, m_Settings.MuJoCoIterations) << "\" cone=\"elliptic\"/>\n";
            xml << "<size memory=\"256M\"/>\n";
            xml << "<default><geom condim=\"3\" solimp=\"0.95 0.99 0.001\"/></default>\n";
            std::string a = assets.str();
            if (!a.empty()) xml << "<asset>\n" << a << "</asset>\n";
            xml << "<worldbody>\n" << world.str() << "</worldbody>\n";
            std::string eq = equality.str();
            if (!eq.empty()) xml << "<equality>\n" << eq << "</equality>\n";
            std::string tn = tendon.str();
            if (!tn.empty()) xml << "<tendon>\n" << tn << "</tendon>\n";
            xml << "</mujoco>\n";
            return xml.str();
        }

        void ResolveIds()
        {
            m_GeomInfo.assign((size_t)m_Model->ngeom, GeomInfo{});
            for (auto& [h, b] : m_Bodies)
            {
                std::string name = "b" + std::to_string(h);
                b.MjBody = Api().name2id(m_Model, mjOBJ_BODY, name.c_str());
                if (b.MjBody < 0) continue;
                b.Mocap = m_Model->body_mocapid[b.MjBody];
                int jnt = m_Model->body_jntadr[b.MjBody];
                b.QposAdr = jnt >= 0 ? m_Model->jnt_qposadr[jnt] : -1;
                b.DofAdr = jnt >= 0 ? m_Model->jnt_dofadr[jnt] : -1;
                b.Geoms.clear();
                b.TriggerGeoms.clear();
                for (size_t k = 0; k < b.Desc.Shapes.size(); ++k)
                {
                    std::string gname = "g" + std::to_string(h) + "_" + std::to_string(k);
                    int g = Api().name2id(m_Model, mjOBJ_GEOM, gname.c_str());
                    if (g < 0) continue;
                    GeomInfo& gi = m_GeomInfo[(size_t)g];
                    gi.Body = h;
                    gi.Entity = b.Desc.Shapes[k].SourceEntity != NoEntity ? b.Desc.Shapes[k].SourceEntity : b.Desc.Entity;
                    gi.Layer = std::clamp(b.Desc.Layer, 0, 31);
                    gi.Trigger = b.Desc.Shapes[k].IsTrigger;
                    gi.Static = b.Desc.Motion == MotionType::Static;
                    (gi.Trigger ? b.TriggerGeoms : b.Geoms).push_back(g);
                }
            }
            for (auto& [h, c] : m_Characters)
            {
                std::string name = "c" + std::to_string(h);
                c.MjBody = Api().name2id(m_Model, mjOBJ_BODY, name.c_str());
                c.Mocap = c.MjBody >= 0 ? m_Model->body_mocapid[c.MjBody] : -1;
                std::string gname = "cg" + std::to_string(h);
                c.Geom = Api().name2id(m_Model, mjOBJ_GEOM, gname.c_str());
                if (c.Geom >= 0)
                {
                    GeomInfo& gi = m_GeomInfo[(size_t)c.Geom];
                    gi.Entity = c.Desc.Entity;
                    gi.Layer = std::clamp(c.Desc.Layer, 0, 31);
                    gi.Character = true;
                }
            }
            for (auto& [jh, j] : m_Joints)
            {
                j.Equalities.clear();
                for (int k = 0; k < 2; ++k)
                {
                    std::string name = "q" + std::to_string(jh) + "_" + std::to_string(k);
                    int id = Api().name2id(m_Model, mjOBJ_EQUALITY, name.c_str());
                    if (id >= 0) j.Equalities.push_back(id);
                }
            }
            m_QueryGeom = Api().name2id(m_Model, mjOBJ_GEOM, "__qg");
        }

        static uint64_t Key(uint32_t a, uint32_t b)
        {
            if (a > b) std::swap(a, b);
            return ((uint64_t)a << 32) | b;
        }

        void CollectContacts()
        {
            std::unordered_map<uint64_t, ContactEvent> current;
            // Katı temaslar
            for (int i = 0; i < m_Data->ncon; ++i)
            {
                const mjContact& c = m_Data->contact[i];
                if (c.exclude != 0 || c.geom[0] < 0 || c.geom[1] < 0)
                    continue;
                const GeomInfo& g1 = m_GeomInfo[(size_t)c.geom[0]];
                const GeomInfo& g2 = m_GeomInfo[(size_t)c.geom[1]];
                if (g1.Entity == NoEntity || g2.Entity == NoEntity || g1.Entity == g2.Entity)
                    continue;
                uint64_t key = Key(g1.Entity, g2.Entity);
                if (current.count(key))
                    continue;
                ContactEvent ev;
                ev.EntityA = g1.Entity;
                ev.EntityB = g2.Entity;
                ev.Point = glm::vec3((float)c.pos[0], (float)c.pos[1], (float)c.pos[2]);
                ev.Normal = glm::vec3((float)c.frame[0], (float)c.frame[1], (float)c.frame[2]);
                if (c.efc_address >= 0 && c.efc_address < m_Data->nefc)
                    ev.Impulse = (float)(m_Data->efc_force[c.efc_address] * m_LastDt);
                current[key] = ev;
            }
            // Tetikleyiciler: mj_geomDistance ile örtüşme
            for (auto& [h, b] : m_Bodies)
                for (int t : b.TriggerGeoms)
                {
                    const mjtNum* tp = m_Data->geom_xpos + 3 * t;
                    const GeomInfo& ti = m_GeomInfo[(size_t)t];
                    for (int g = 0; g < (int)m_GeomInfo.size(); ++g)
                    {
                        const GeomInfo& gi = m_GeomInfo[(size_t)g];
                        if (g == t || gi.Trigger || gi.Entity == NoEntity || gi.Entity == ti.Entity || gi.Static)
                            continue;
                        if (!m_Settings.LayersCollide(ti.Layer, gi.Layer))
                            continue;
                        const mjtNum* gp = m_Data->geom_xpos + 3 * g;
                        double dx = gp[0] - tp[0], dy = gp[1] - tp[1], dz = gp[2] - tp[2];
                        if (std::sqrt(dx * dx + dy * dy + dz * dz) > m_Model->geom_rbound[t] + m_Model->geom_rbound[g] + 1e-3)
                            continue;
                        if (Api().geomDistance(m_Model, m_Data, t, g, 0.0, nullptr) > 0.0)
                            continue;
                        uint64_t key = Key(ti.Entity, gi.Entity);
                        ContactEvent ev;
                        ev.EntityA = ti.Entity;
                        ev.EntityB = gi.Entity;
                        ev.Trigger = true;
                        ev.Point = glm::vec3((float)gp[0], (float)gp[1], (float)gp[2]);
                        current[key] = ev;
                    }
                }
            // Enter / Stay
            for (auto& [key, ev] : current)
            {
                auto it = m_ActivePairs.find(key);
                ContactEvent out = ev;
                out.Type = it == m_ActivePairs.end() ? ContactEvent::Enter : ContactEvent::Stay;
                m_Events.push_back(out);
            }
            // Exit
            for (auto& [key, st] : m_ActivePairs)
                if (!current.count(key))
                {
                    ContactEvent ev;
                    ev.Type = ContactEvent::Exit;
                    ev.EntityA = st.A;
                    ev.EntityB = st.B;
                    ev.Trigger = st.Trigger;
                    m_Events.push_back(ev);
                }
            m_ActivePairs.clear();
            for (auto& [key, ev] : current)
                m_ActivePairs[key] = { ev.EntityA, ev.EntityB, ev.Trigger };
        }

        PhysicsSettings m_Settings;
        mjModel* m_Model = nullptr;
        mjData* m_Data = nullptr;
        bool m_Initialized = false;
        bool m_Dirty = true;
        bool m_Failed = false;
        bool m_WarnedMesh = false;
        int m_LastBadQacc = 0;
        int m_QueryGeom = -1;
        float m_LastDt = 1.0f / 60.0f;
        Handle m_NextHandle = 0;
        std::map<Handle, BodyRec> m_Bodies;
        std::map<Handle, JointRec> m_Joints;
        std::map<Handle, CharRec> m_Characters;
        std::vector<GeomInfo> m_GeomInfo;
        std::unordered_map<uint64_t, PairState> m_ActivePairs;
        std::vector<ContactEvent> m_Events;
    };

    std::unique_ptr<Backend> CreateMuJoCoBackend()
    {
        return std::make_unique<MuJoCoBackend>();
    }
}
