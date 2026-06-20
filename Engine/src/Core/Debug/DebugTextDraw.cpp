#include "DebugTextDraw.h"
#include "DebugSettings.h"
#include "DebugDrawInternal.h"

#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <unordered_map>

namespace Conqueror
{
    struct DebugTextDrawData
    {
        std::vector<DebugTextEntry> Entries;
    };

    static DebugTextDrawData* s_TextData = nullptr;

    static const std::unordered_map<char, std::vector<std::pair<glm::vec2, glm::vec2>>>& GetStrokeFont()
    {
        static std::unordered_map<char, std::vector<std::pair<glm::vec2, glm::vec2>>> font;
        if (!font.empty())
            return font;

        auto add = [&](char c, std::initializer_list<std::pair<glm::vec2, glm::vec2>> strokes)
        {
            font[c] = strokes;
        };

        add('0', { {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}} });
        add('1', { {{0.5f,0.0f},{0.5f,1.0f}} });
        add('2', { {{0.1f,1.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.9f,0.5f}}, {{0.9f,0.5f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}} });
        add('3', { {{0.1f,1.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.1f,0.0f}} });
        add('4', { {{0.1f,0.5f},{0.9f,0.5f}}, {{0.7f,0.0f},{0.7f,1.0f}} });
        add('5', { {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.5f}}, {{0.1f,0.5f},{0.9f,0.5f}}, {{0.9f,0.5f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.1f,0.0f}} });
        add('6', { {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,0.5f}}, {{0.9f,0.5f},{0.1f,0.5f}} });
        add('7', { {{0.1f,1.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.5f,0.0f}} });
        add('8', { {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}}, {{0.1f,0.5f},{0.9f,0.5f}} });
        add('9', { {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,0.5f},{0.9f,0.5f}} });
        add('A', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.9f,0.0f}}, {{0.1f,0.5f},{0.9f,0.5f}} });
        add('B', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.8f,1.0f}}, {{0.8f,1.0f},{0.9f,0.85f}}, {{0.9f,0.85f},{0.8f,0.65f}}, {{0.8f,0.65f},{0.1f,0.5f}}, {{0.1f,0.5f},{0.8f,0.35f}}, {{0.8f,0.35f},{0.9f,0.15f}}, {{0.9f,0.15f},{0.8f,0.0f}}, {{0.8f,0.0f},{0.1f,0.0f}} });
        add('C', { {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}} });
        add('D', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.7f,1.0f}}, {{0.7f,1.0f},{0.9f,0.8f}}, {{0.9f,0.8f},{0.9f,0.2f}}, {{0.9f,0.2f},{0.7f,0.0f}}, {{0.7f,0.0f},{0.1f,0.0f}} });
        add('E', { {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}}, {{0.1f,0.5f},{0.75f,0.5f}} });
        add('F', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.9f,1.0f}}, {{0.1f,0.5f},{0.75f,0.5f}} });
        add('G', { {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,0.45f}}, {{0.9f,0.45f},{0.55f,0.45f}} });
        add('H', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.9f,0.0f},{0.9f,1.0f}}, {{0.1f,0.5f},{0.9f,0.5f}} });
        add('I', { {{0.3f,0.0f},{0.7f,0.0f}}, {{0.5f,0.0f},{0.5f,1.0f}}, {{0.3f,1.0f},{0.7f,1.0f}} });
        add('J', { {{0.2f,1.0f},{0.8f,1.0f}}, {{0.7f,1.0f},{0.7f,0.2f}}, {{0.7f,0.2f},{0.4f,0.0f}}, {{0.4f,0.0f},{0.2f,0.2f}} });
        add('K', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,0.5f},{0.9f,1.0f}}, {{0.1f,0.55f},{0.85f,0.0f}} });
        add('L', { {{0.1f,1.0f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}} });
        add('M', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.5f,0.55f}}, {{0.5f,0.55f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.9f,0.0f}} });
        add('N', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,1.0f}} });
        add('O', { {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}} });
        add('P', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.85f,1.0f}}, {{0.85f,1.0f},{0.9f,0.8f}}, {{0.9f,0.8f},{0.85f,0.55f}}, {{0.85f,0.55f},{0.1f,0.5f}} });
        add('Q', { {{0.1f,0.0f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.0f}}, {{0.55f,0.35f},{0.95f,0.0f}} });
        add('R', { {{0.1f,0.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.85f,1.0f}}, {{0.85f,1.0f},{0.9f,0.8f}}, {{0.9f,0.8f},{0.85f,0.55f}}, {{0.85f,0.55f},{0.1f,0.5f}}, {{0.35f,0.5f},{0.9f,0.0f}} });
        add('S', { {{0.9f,1.0f},{0.1f,1.0f}}, {{0.1f,1.0f},{0.1f,0.55f}}, {{0.1f,0.55f},{0.9f,0.45f}}, {{0.9f,0.45f},{0.9f,0.0f}}, {{0.9f,0.0f},{0.1f,0.0f}} });
        add('T', { {{0.1f,1.0f},{0.9f,1.0f}}, {{0.5f,1.0f},{0.5f,0.0f}} });
        add('U', { {{0.1f,1.0f},{0.1f,0.15f}}, {{0.1f,0.15f},{0.45f,0.0f}}, {{0.45f,0.0f},{0.55f,0.0f}}, {{0.55f,0.0f},{0.9f,0.15f}}, {{0.9f,0.15f},{0.9f,1.0f}} });
        add('V', { {{0.1f,1.0f},{0.5f,0.0f}}, {{0.5f,0.0f},{0.9f,1.0f}} });
        add('W', { {{0.05f,1.0f},{0.25f,0.0f}}, {{0.25f,0.0f},{0.5f,0.55f}}, {{0.5f,0.55f},{0.75f,0.0f}}, {{0.75f,0.0f},{0.95f,1.0f}} });
        add('X', { {{0.1f,0.0f},{0.9f,1.0f}}, {{0.9f,0.0f},{0.1f,1.0f}} });
        add('Y', { {{0.1f,1.0f},{0.5f,0.5f}}, {{0.9f,1.0f},{0.5f,0.5f}}, {{0.5f,0.5f},{0.5f,0.0f}} });
        add('Z', { {{0.1f,1.0f},{0.9f,1.0f}}, {{0.9f,1.0f},{0.1f,0.0f}}, {{0.1f,0.0f},{0.9f,0.0f}} });
        add(' ', {});
        add('.', { {{0.45f,0.0f},{0.55f,0.0f}} });
        add(',', { {{0.45f,0.15f},{0.55f,0.0f}}, {{0.55f,0.0f},{0.45f,-0.15f}} });
        add(':', { {{0.48f,0.7f},{0.52f,0.7f}}, {{0.48f,0.3f},{0.52f,0.3f}} });
        add('-', { {{0.15f,0.5f},{0.85f,0.5f}} });
        add('_', { {{0.1f,0.0f},{0.9f,0.0f}} });
        add('(', { {{0.65f,1.0f},{0.35f,0.75f}}, {{0.35f,0.75f},{0.35f,0.25f}}, {{0.35f,0.25f},{0.65f,0.0f}} });
        add(')', { {{0.35f,1.0f},{0.65f,0.75f}}, {{0.65f,0.75f},{0.65f,0.25f}}, {{0.65f,0.25f},{0.35f,0.0f}} });
        add('[', { {{0.7f,1.0f},{0.3f,1.0f}}, {{0.3f,1.0f},{0.3f,0.0f}}, {{0.3f,0.0f},{0.7f,0.0f}} });
        add(']', { {{0.3f,1.0f},{0.7f,1.0f}}, {{0.7f,1.0f},{0.7f,0.0f}}, {{0.7f,0.0f},{0.3f,0.0f}} });
        add('/', { {{0.1f,0.0f},{0.9f,1.0f}} });
        add('\\', { {{0.1f,1.0f},{0.9f,0.0f}} });
        add('+', { {{0.5f,0.15f},{0.5f,0.85f}}, {{0.15f,0.5f},{0.85f,0.5f}} });
        add('=', { {{0.15f,0.65f},{0.85f,0.65f}}, {{0.15f,0.35f},{0.85f,0.35f}} });
        add('*', { {{0.5f,0.15f},{0.5f,0.85f}}, {{0.2f,0.35f},{0.8f,0.65f}}, {{0.8f,0.35f},{0.2f,0.65f}} });
        add('#', { {{0.35f,0.0f},{0.35f,1.0f}}, {{0.65f,0.0f},{0.65f,1.0f}}, {{0.15f,0.35f},{0.85f,0.35f}}, {{0.15f,0.65f},{0.85f,0.65f}} });
        return font;
    }

    static void DrawStrokeChar(char c, const glm::vec3& origin, const glm::vec3& right, const glm::vec3& up, float scale, const glm::vec4& color, bool depthTested)
    {
        char key = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto& font = GetStrokeFont();
        auto it = font.find(key);
        if (it == font.end())
            key = '?';
        it = font.find(key);
        if (it == font.end())
            return;

        for (const auto& stroke : it->second)
        {
            glm::vec3 start = origin + right * (stroke.first.x * scale) + up * (stroke.first.y * scale);
            glm::vec3 end = origin + right * (stroke.second.x * scale) + up * (stroke.second.y * scale);
            DebugDrawInternal::SubmitLine(start, end, color, depthTested);
        }
    }

    void DebugTextDraw::Init()
    {
        s_TextData = new DebugTextDrawData();
    }

    void DebugTextDraw::Shutdown()
    {
        delete s_TextData;
        s_TextData = nullptr;
    }

    void DebugTextDraw::Draw(const glm::vec3& position, const std::string& text, const glm::vec4& color, float scale, bool depthTested, bool billboard)
    {
        if (!s_TextData || !DebugSettings::Get().IsCategoryActive(DebugCategory::Text))
            return;

        if (s_TextData->Entries.size() >= DebugSettings::Get().MaxTextEntries)
            return;

        s_TextData->Entries.push_back({ position, text, color, scale, depthTested, billboard });
    }

    void DebugTextDraw::DrawFormatted(const glm::vec3& position, const glm::vec4& color, float scale, bool depthTested, const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Draw(position, buffer, color, scale, depthTested);
    }

    void DebugTextDraw::Render(const glm::mat4& viewProjection, const glm::vec3& cameraPosition)
    {
        (void)viewProjection;
        if (!s_TextData || !DebugSettings::Get().IsCategoryActive(DebugCategory::Text))
            return;

        for (const auto& entry : s_TextData->Entries)
        {
            glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

            if (entry.Billboard)
            {
                glm::vec3 toCamera = cameraPosition - entry.Position;
                if (glm::dot(toCamera, toCamera) > 0.0001f)
                {
                    glm::vec3 forward = glm::normalize(toCamera);
                    right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
                    if (glm::dot(right, right) < 0.0001f)
                        right = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), forward));
                    up = glm::normalize(glm::cross(forward, right));
                }
            }

            float charWidth = entry.Scale * 0.65f;
            float cursor = 0.0f;
            for (char c : entry.Text)
            {
                glm::vec3 charOrigin = entry.Position + right * cursor;
                DrawStrokeChar(c, charOrigin, right, up, entry.Scale, entry.Color, entry.DepthTested);
                cursor += charWidth;
            }
        }
    }

    void DebugTextDraw::Clear()
    {
        if (!s_TextData)
            return;
        s_TextData->Entries.clear();
    }

    uint32_t DebugTextDraw::GetEntryCount()
    {
        return s_TextData ? static_cast<uint32_t>(s_TextData->Entries.size()) : 0;
    }
}
