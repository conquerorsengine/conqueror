#pragma once

#include "Font.h"
#include "Scene/EditorCamera.h"
#include "Scene/SceneCamera.h"

#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace Conqueror
{
    class TextRenderer
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene(const EditorCamera& camera, bool enableDepthTest = false);
        static void BeginScene(const SceneCamera& camera, const glm::mat4& transform, bool enableDepthTest = false);
        // Ham ViewProjection matrisi ile BeginScene (Screen-Space UI render için)
        static void BeginScene(const glm::mat4& viewProjection, bool enableDepthTest = false);
        static void EndScene();

        static void DrawText(const std::string& text, const glm::vec3& position, float scale, const glm::vec4& color);
        static void DrawText(const std::string& text, const glm::mat4& transform, float scale, const glm::vec4& color);

    private:
        static void FlushText();
    };
}
