#pragma once

#include "Renderer/RHI/Shader.h"
#include "Renderer/ShaderCompiler/ShaderCompiler.h"
#include <glm/glm.hpp>

// TODO: REMOVE!
typedef unsigned int GLenum;

namespace Conqueror
{
    class OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& filepath);
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
        virtual ~OpenGLShader();

        void Bind() const override;
        void Unbind() const override;

        void SetInt(const std::string& name, int value) override;
        void SetIntArray(const std::string& name, int* values, uint32_t count) override;
        void SetFloat(const std::string& name, float value) override;
        void SetFloat2(const std::string& name, const glm::vec2& value) override;
        void SetFloat3(const std::string& name, const glm::vec3& value) override;
        void SetFloat4(const std::string& name, const glm::vec4& value) override;
        void SetMat4(const std::string& name, const glm::mat4& value) override;
        void SetMat4Array(const std::string& name, const glm::mat4* matrices, uint32_t count) override;

        void SetFloatArray(const std::string& name, const float* values, uint32_t count) override;
        void SetFloat3Array(const std::string& name, const glm::vec3* values, uint32_t count) override;

        const std::string& GetName() const override { return m_Name; }
        bool IsValid() const override { return m_RendererID != 0; }
        bool Reload() override;
        const std::string& GetFilePath() const override { return m_FilePath; }
        const std::string& GetLastError() const override { return m_LastError; }
        uint32_t GetRendererID() const override { return m_RendererID; }
        bool IsSourceModified() const override;
        const ShaderReflection* GetReflection() const override { return m_FromSPIRV ? &m_Reflection : nullptr; }
        bool IsCompiledFromSPIRV() const override { return m_FromSPIRV; }
        std::string GetStageCode(int stage, const std::string& view) const override;

        void UploadUniformInt(const std::string& name, int value);
        void UploadUniformIntArray(const std::string& name, int* values, uint32_t count);

        void UploadUniformFloat(const std::string& name, float value);
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value);
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value);
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value);

        void UploadUniformMat3(const std::string& name, const glm::mat3& matrix);
        void UploadUniformMat4(const std::string& name, const glm::mat4& matrix);

    private:
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<GLenum, std::string> PreProcess(const std::string& source);
        bool Compile(const std::unordered_map<GLenum, std::string>& shaderSources);
        static void ApplyLinkDirectives(unsigned int program, const std::unordered_map<GLenum, std::string>& sources);
        bool LoadFromFile(const std::string& filepath);
        int GetLocationCached(const std::string& name) const;

    private:
        uint32_t m_RendererID = 0;
        std::string m_Name;
        std::string m_FilePath;
        std::string m_LastError;
        std::vector<std::string> m_Dependencies;
        std::vector<long long> m_DependencyTimes;
        mutable std::unordered_map<std::string, int> m_UniformLocationCache;
        ShaderReflection m_Reflection;
        std::map<ShaderStage, std::string> m_ExpandedSources;
        std::map<ShaderStage, std::string> m_CrossSources;
        std::map<ShaderStage, std::vector<uint32_t>> m_SPIRV;
        bool m_FromSPIRV = false;
    };
}
