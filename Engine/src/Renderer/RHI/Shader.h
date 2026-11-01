#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace Conqueror
{
    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual void SetInt(const std::string& name, int value) = 0;
        virtual void SetIntArray(const std::string& name, int* values, uint32_t count) = 0;
        virtual void SetFloat(const std::string& name, float value) = 0;
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) = 0;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;
        virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;
        virtual void SetMat4Array(const std::string& name, const glm::mat4* matrices, uint32_t count) = 0;

        virtual void SetFloatArray(const std::string& name, const float* values, uint32_t count) { for (uint32_t i = 0; i < count; ++i) SetFloat(name + "[" + std::to_string(i) + "]", values[i]); }
        virtual void SetFloat3Array(const std::string& name, const glm::vec3* values, uint32_t count) { for (uint32_t i = 0; i < count; ++i) SetFloat3(name + "[" + std::to_string(i) + "]", values[i]); }

        virtual const std::string& GetName() const = 0;
        virtual bool IsValid() const { return true; }

        // Hot-reload / editor desteği
        virtual bool Reload() { return false; }
        virtual const std::string& GetFilePath() const { static std::string empty; return empty; }
        virtual const std::string& GetLastError() const { static std::string empty; return empty; }
        virtual uint32_t GetRendererID() const { return 0; }
        // Kaynak dosyalardan biri değiştiyse true (include'lar dahil)
        virtual bool IsSourceModified() const { return false; }
        // SPIR-V hattından geçtiyse yansıtma verisi (uniform/sampler/blok listesi)
        virtual const struct ShaderReflection* GetReflection() const { return nullptr; }
        virtual bool IsCompiledFromSPIRV() const { return false; }
        // Aşama kodu: view = "source" (include'lar açılmış GLSL), "glsl" (SPIR-V'den), "spirv" (disassembly), "hlsl", "msl"
        // stage: 0 vertex, 1 fragment, 2 geometry, 3 compute
        virtual std::string GetStageCode(int stage, const std::string& view) const { (void)stage; (void)view; return {}; }

        static std::shared_ptr<Shader> Create(const std::string& filepath);
        static std::shared_ptr<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
    };

    class ShaderLibrary
    {
    public:
        void Add(const std::string& name, const std::shared_ptr<Shader>& shader);
        void Add(const std::shared_ptr<Shader>& shader);
        std::shared_ptr<Shader> Load(const std::string& filepath);
        std::shared_ptr<Shader> Load(const std::string& name, const std::string& filepath);

        std::shared_ptr<Shader> Get(const std::string& name);

        bool Exists(const std::string& name) const;

    private:
        std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
    };
}
