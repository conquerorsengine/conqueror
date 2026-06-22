#include "ReflectionProbe.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Conqueror
{
    ReflectionProbe::ReflectionProbe(const ReflectionProbeSettings& settings)
        : m_Settings(settings)
    {
        // Cubemap olustur
        m_Cubemap = Cubemap::CreateFromFaces(nullptr);
    }

    void ReflectionProbe::Capture(Scene* scene)
    {
        if (!scene || !m_Cubemap)
            return;

        CQ_CORE_INFO("Reflection probe capturing at ({0}, {1}, {2})",
                      m_Settings.Position.x, m_Settings.Position.y, m_Settings.Position.z);

        // Her yon icin render et
        // +X, -X, +Y, -Y, +Z, -Z
        struct FaceData {
            glm::vec3 target;
            glm::vec3 up;
        };

        FaceData faces[6] = {
            { glm::vec3(1, 0, 0),  glm::vec3(0, -1, 0) },  // +X
            { glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0) },  // -X
            { glm::vec3(0, 1, 0),  glm::vec3(0, 0, 1) },   // +Y
            { glm::vec3(0, -1, 0), glm::vec3(0, 0, -1) },  // -Y
            { glm::vec3(0, 0, 1),  glm::vec3(0, -1, 0) },  // +Z
            { glm::vec3(0, 0, -1), glm::vec3(0, -1, 0) },  // -Z
        };

        // Framebuffer olustur
        FramebufferSpecification spec;
        spec.Width = m_Settings.Resolution;
        spec.Height = m_Settings.Resolution;
        spec.Attachments = { FramebufferTextureFormat::RGBA8 };
        spec.SwapChainTarget = false;

        auto framebuffer = Framebuffer::Create(spec);

        for (int i = 0; i < 6; i++)
        {
            RenderFace(scene, m_Settings.Position, m_Settings.Position + faces[i].target,
                      faces[i].up, i);
        }

        CQ_CORE_INFO("Reflection probe capture complete");
    }

    void ReflectionProbe::RenderFace(Scene* scene, const glm::vec3& position,
                                      const glm::vec3& center, const glm::vec3& up,
                                      uint32_t faceIndex)
{
        // Bu yon icin view matrix olustur
        glm::mat4 view = glm::lookAt(position, center, up);
        glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

        // Renderer3D'i bu view/projection ile baslat
        // Not: Bu su an basitlestirilmis, gercektesinde cubemap render yapilmali
        // Simdi sadece log yazalim
        CQ_CORE_INFO("Rendering reflection probe face {0}", faceIndex);
    }

    bool ReflectionProbe::ContainsPoint(const glm::vec3& point) const
    {
        glm::vec3 min = m_Settings.Position + m_Settings.BoxOffset;
        glm::vec3 max = m_Settings.Position + m_Settings.BoxOffset + m_Settings.BoxSize * 2.0f;

        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    float ReflectionProbe::DistanceToPoint(const glm::vec3& point) const
    {
        return glm::length(point - m_Settings.Position);
    }

    std::shared_ptr<ReflectionProbe> ReflectionProbe::Create(const ReflectionProbeSettings& settings)
    {
        return std::make_shared<ReflectionProbe>(settings);
    }
}
