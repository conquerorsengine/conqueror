#include "OpenGLShader.h"
#include "Core/Logging/Log.h"
#include "Core/ShaderSystem/CQShaderLoader.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstring>
#include <regex>
#include <sstream>
#include <glad/glad.h>

#include <glm/gtc/type_ptr.hpp>

namespace Conqueror
{
    static GLenum ShaderTypeFromString(const std::string& rawType)
    {
        std::string type = rawType;
        while (!type.empty() && (type.back() == ' ' || type.back() == '\t' || type.back() == '\r'))
            type.pop_back();
        if (type == "vertex")
            return GL_VERTEX_SHADER;
        if (type == "fragment" || type == "pixel")
            return GL_FRAGMENT_SHADER;
        if (type == "geometry")
            return GL_GEOMETRY_SHADER;
        if (type == "compute")
            return GL_COMPUTE_SHADER;
        if (type == "tess_control" || type == "hull")
            return GL_TESS_CONTROL_SHADER;
        if (type == "tess_evaluation" || type == "domain")
            return GL_TESS_EVALUATION_SHADER;
        return 0;
    }

    static const char* ShaderStageName(GLenum type)
    {
        switch (type)
        {
            case GL_VERTEX_SHADER: return "vertex";
            case GL_FRAGMENT_SHADER: return "fragment";
            case GL_GEOMETRY_SHADER: return "geometry";
            case GL_COMPUTE_SHADER: return "compute";
            case GL_TESS_CONTROL_SHADER: return "tess_control";
            case GL_TESS_EVALUATION_SHADER: return "tess_evaluation";
        }
        return "unknown";
    }

    static long long FileTime(const std::string& path)
    {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(path, ec);
        if (ec)
            return 0;
        return (long long)t.time_since_epoch().count();
    }

    static std::string NameFromPath(const std::string& filepath)
    {
        auto lastSlash = filepath.find_last_of("/\\");
        lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
        auto lastDot = filepath.rfind('.');
        auto count = (lastDot == std::string::npos || lastDot < lastSlash) ? filepath.size() - lastSlash : lastDot - lastSlash;
        return filepath.substr(lastSlash, count);
    }

    OpenGLShader::OpenGLShader(const std::string& filepath)
        : m_RendererID(0), m_Name(NameFromPath(filepath)), m_FilePath(filepath)
    {
        LoadFromFile(filepath);
    }

    bool OpenGLShader::LoadFromFile(const std::string& filepath)
    {
        CQShaderSource cqSource = CQShaderLoader::Load(filepath);

        if (cqSource.Source.empty())
        {
            m_LastError = "Empty or missing shader source";
            CQ_CORE_ERROR("OpenGLShader: Empty source, skipping compile for '{0}'", filepath);
            return false;
        }

        if (cqSource.Language != ShaderLanguage::GLSL)
        {
            m_LastError = "Only GLSL is supported by the OpenGL backend";
            CQ_CORE_ERROR("OpenGLShader: Only GLSL is supported. File '{0}' uses {1}", filepath, ShaderLanguageToString(cqSource.Language));
            return false;
        }

        m_Dependencies = cqSource.Dependencies;
        m_DependencyTimes.clear();
        for (const auto& dep : m_Dependencies)
            m_DependencyTimes.push_back(FileTime(dep));

        auto shaderSources = PreProcess(cqSource.Source);
        return Compile(shaderSources);
    }

    bool OpenGLShader::Reload()
    {
        if (m_FilePath.empty())
            return false;
        uint32_t oldProgram = m_RendererID;
        m_RendererID = 0;
        if (!LoadFromFile(m_FilePath))
        {
            // Derleme başarısızsa eski programla devam et (editör çökmesin)
            if (m_RendererID == 0)
                m_RendererID = oldProgram;
            return false;
        }
        if (oldProgram && oldProgram != m_RendererID)
            glDeleteProgram(oldProgram);
        m_UniformLocationCache.clear();
        CQ_CORE_INFO("Shader reloaded: {0}", m_FilePath);
        return true;
    }

    bool OpenGLShader::IsSourceModified() const
    {
        for (size_t i = 0; i < m_Dependencies.size() && i < m_DependencyTimes.size(); ++i)
            if (FileTime(m_Dependencies[i]) != m_DependencyTimes[i])
                return true;
        return false;
    }

    OpenGLShader::OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(name)
    {
        std::unordered_map<GLenum, std::string> sources;
        sources[GL_VERTEX_SHADER] = vertexSrc;
        sources[GL_FRAGMENT_SHADER] = fragmentSrc;
        Compile(sources);
    }

    OpenGLShader::~OpenGLShader()
    {
        if (m_RendererID)
            glDeleteProgram(m_RendererID);
    }

    std::string OpenGLShader::ReadFile(const std::string& filepath)
    {
        std::string result;
        std::ifstream in(filepath, std::ios::in | std::ios::binary);
        if (in)
        {
            in.seekg(0, std::ios::end);
            auto size = in.tellg();
            if (size != std::streampos(-1))
            {
                result.resize((size_t)size);
                in.seekg(0, std::ios::beg);
                in.read(&result[0], size);
            }
            else
            {
                CQ_CORE_ERROR("Could not read from file '{0}'", filepath);
            }
        }
        else
        {
            CQ_CORE_ERROR("Could not open file '{0}'", filepath);
        }

        return result;
    }

    std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
    {
        std::unordered_map<GLenum, std::string> shaderSources;

        const char* typeToken = "#type";
        size_t typeTokenLength = strlen(typeToken);
        size_t pos = source.find(typeToken, 0);
        while (pos != std::string::npos)
        {
            size_t eol = source.find_first_of("\r\n", pos);
            if (eol == std::string::npos)
            {
                m_LastError = "Syntax error: '#type' without a stage body";
                CQ_CORE_ERROR("OpenGLShader '{0}': {1}", m_Name, m_LastError);
                break;
            }
            size_t begin = pos + typeTokenLength + 1;
            std::string type = source.substr(begin, eol - begin);
            GLenum glType = ShaderTypeFromString(type);

            size_t nextLinePos = source.find_first_not_of("\r\n", eol);
            if (nextLinePos == std::string::npos)
                break;
            pos = source.find(typeToken, nextLinePos);

            if (!glType)
            {
                m_LastError = "Unknown shader stage '" + type + "'";
                CQ_CORE_ERROR("OpenGLShader '{0}': {1}", m_Name, m_LastError);
                continue;
            }

            // Satır numaraları dosyadakiyle eşleşsin diye kaç satır atlandığını ekle
            size_t lineOffset = (size_t)std::count(source.begin(), source.begin() + (std::ptrdiff_t)nextLinePos, '\n');
            std::string body = (pos == std::string::npos) ? source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);
            // "#version" ilk satır olmalı; #line onun hemen arkasına eklenir
            size_t versionEnd = body.find('\n');
            if (body.rfind("#version", 0) == 0 && versionEnd != std::string::npos)
                body.insert(versionEnd + 1, "#line " + std::to_string(lineOffset + 2) + "\n");
            shaderSources[glType] = body;
        }

        return shaderSources;
    }

    std::string OpenGLShader::GetStageCode(int stage, const std::string& view) const
    {
        ShaderStage st = (ShaderStage)stage;
        if (view == "source")
        {
            auto it = m_ExpandedSources.find(st);
            return it != m_ExpandedSources.end() ? it->second : std::string();
        }
        if (view == "glsl")
        {
            auto it = m_CrossSources.find(st);
            return it != m_CrossSources.end() ? it->second : std::string("// SPIR-V pipeline was not used for this shader");
        }
        if (view == "spirv")
        {
            auto it = m_SPIRV.find(st);
            return it != m_SPIRV.end() ? ShaderCompiler::Disassemble(it->second) : std::string("// No SPIR-V for this stage");
        }
        // HLSL / MSL: Vulkan kurallarıyla (global uniform bloğu) yeniden derle ve çapraz derle
        if (m_ExpandedSources.empty())
            return "// No source";
        std::map<ShaderStage, std::vector<uint32_t>> vk;
        std::string err;
        if (!ShaderCompiler::CompileToSPIRV(m_ExpandedSources, m_Name, vk, err, true))
            return "// Vulkan SPIR-V compile failed:\n// " + err;
        auto it = vk.find(st);
        if (it == vk.end())
            return "// No such stage";
        std::string out;
        if (!ShaderCompiler::CrossCompile(it->second, view, out, err))
            return "// " + err;
        return out;
    }

    bool OpenGLShader::Compile(const std::unordered_map<GLenum, std::string>& shaderSources)
    {
        if (shaderSources.empty())
        {
            if (m_LastError.empty())
                m_LastError = "No shader stages to compile";
            CQ_CORE_ERROR("OpenGLShader '{0}': {1}", m_Name, m_LastError);
            m_RendererID = 0;
            return false;
        }

        // GLSL -> SPIR-V -> GLSL 450 (doğrulama, yansıtma, önbellek). Başarısızsa doğrudan GLSL.
        std::unordered_map<GLenum, std::string> finalSources = shaderSources;
        m_FromSPIRV = false;
        if (ShaderCompiler::GetSettings().Enabled)
        {
            std::map<ShaderStage, std::string> in, out;
            bool supported = true;
            for (auto& kv : shaderSources)
            {
                switch (kv.first)
                {
                    case GL_VERTEX_SHADER: in[ShaderStage::Vertex] = kv.second; break;
                    case GL_FRAGMENT_SHADER: in[ShaderStage::Fragment] = kv.second; break;
                    case GL_GEOMETRY_SHADER: in[ShaderStage::Geometry] = kv.second; break;
                    case GL_COMPUTE_SHADER: in[ShaderStage::Compute] = kv.second; break;
                    case GL_TESS_CONTROL_SHADER: in[ShaderStage::TessControl] = kv.second; break;
                    case GL_TESS_EVALUATION_SHADER: in[ShaderStage::TessEvaluation] = kv.second; break;
                    default: supported = false; break;
                }
            }
            std::string spirvErrors;
            m_ExpandedSources = in;
            m_CrossSources.clear();
            m_SPIRV.clear();
            if (supported && ShaderCompiler::CompileForOpenGL(in, m_Name, out, &m_Reflection, spirvErrors, &m_SPIRV))
            {
                m_CrossSources = out;
                finalSources.clear();
                for (auto& [stage, text] : out)
                {
                    GLenum t = stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : stage == ShaderStage::Fragment ? GL_FRAGMENT_SHADER :
                               stage == ShaderStage::Geometry ? GL_GEOMETRY_SHADER : stage == ShaderStage::Compute ? GL_COMPUTE_SHADER :
                               stage == ShaderStage::TessControl ? GL_TESS_CONTROL_SHADER : GL_TESS_EVALUATION_SHADER;
                    finalSources[t] = text;
                }
                m_FromSPIRV = true;
            }
            else if (supported)
            {
                CQ_CORE_WARN("Shader '{0}': SPIR-V path failed, compiling GLSL directly:\n{1}", m_Name, spirvErrors);
            }
        }

        GLuint program = glCreateProgram();
        std::vector<GLuint> glShaderIDs;
        bool failed = false;
        m_LastError.clear();

        for (auto& kv : finalSources)
        {
            GLenum type = kv.first;
            const std::string& source = kv.second;

            GLuint shader = glCreateShader(type);
            const GLchar* sourceCStr = source.c_str();
            glShaderSource(shader, 1, &sourceCStr, 0);
            glCompileShader(shader);

            GLint isCompiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
            if (isCompiled == GL_FALSE)
            {
                GLint maxLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
                std::vector<GLchar> infoLog((size_t)std::max(maxLength, 1));
                glGetShaderInfoLog(shader, maxLength, &maxLength, infoLog.data());
                glDeleteShader(shader);

                m_LastError += std::string("[") + ShaderStageName(type) + "] " + infoLog.data() + "\n";
                CQ_CORE_ERROR("Shader '{0}' ({1}) compile error:\n{2}", m_Name, ShaderStageName(type), infoLog.data());
                failed = true;
                continue;
            }

            glAttachShader(program, shader);
            glShaderIDs.push_back(shader);
        }

        auto retryWithoutSPIRV = [&]() {
            CQ_CORE_WARN("Shader '{0}': cross-compiled GLSL rejected by the driver, retrying original GLSL: {1}", m_Name, m_LastError);
            auto& settings = ShaderCompiler::GetSettings();
            bool prev = settings.Enabled;
            settings.Enabled = false;
            bool ok = Compile(shaderSources);
            settings.Enabled = prev;
            return ok;
        };

        if (failed)
        {
            for (GLuint id : glShaderIDs)
                glDeleteShader(id);
            glDeleteProgram(program);
            m_RendererID = 0;
            if (m_FromSPIRV)
                return retryWithoutSPIRV();
            return false;
        }

        glLinkProgram(program);

        GLint isLinked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
        if (isLinked == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
            std::vector<GLchar> infoLog((size_t)std::max(maxLength, 1));
            glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());
            glDeleteProgram(program);
            for (GLuint id : glShaderIDs)
                glDeleteShader(id);

            m_LastError = std::string("[link] ") + infoLog.data();
            m_RendererID = 0;
            if (m_FromSPIRV)
                return retryWithoutSPIRV();
            CQ_CORE_ERROR("Shader '{0}' link error:\n{1}", m_Name, infoLog.data());
            return false;
        }

        for (GLuint id : glShaderIDs)
        {
            glDetachShader(program, id);
            glDeleteShader(id);
        }

        ApplyLinkDirectives(program, finalSources);
        m_RendererID = program;
        m_UniformLocationCache.clear();
        return true;
    }

    // Çapraz derleyicinin bıraktığı yönergeler (layout(binding) / uniform başlangıç değeri yazılamayan
    // OpenGL 3.3, 4.1 ve ES hedefleri): "// CQ_UBO_BINDING Blok N", "// CQ_UNIFORM_DEFAULT tür ad değer"
    void OpenGLShader::ApplyLinkDirectives(unsigned int program, const std::unordered_map<GLenum, std::string>& sources)
    {
        GLint previous = 0;
        bool used = false;
        for (const auto& [stage, text] : sources)
        {
            size_t pos = 0;
            while ((pos = text.find("// CQ_", pos)) != std::string::npos)
            {
                const size_t end = text.find('\n', pos);
                std::istringstream line(text.substr(pos + 3, (end == std::string::npos ? text.size() : end) - pos - 3));
                pos = end == std::string::npos ? text.size() : end;
                std::string kind;
                line >> kind;
                if (kind == "CQ_UBO_BINDING")
                {
                    std::string block;
                    GLuint binding = 0;
                    line >> block >> binding;
                    const GLuint index = glGetUniformBlockIndex(program, block.c_str());
                    if (index != GL_INVALID_INDEX)
                        glUniformBlockBinding(program, index, binding);
                }
                else if (kind == "CQ_UNIFORM_DEFAULT")
                {
                    std::string type, name, value;
                    line >> type >> name;
                    std::getline(line, value);
                    const GLint location = glGetUniformLocation(program, name.c_str());
                    if (location < 0)
                        continue;
                    if (!used)
                    {
                        glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
                        glUseProgram(program);
                        used = true;
                    }
                    // Bileşen sayısı türden (vec3 -> 3); sayılar kurucu adından sonra (vec3(1.0) -> tek değer yayılır)
                    int components = 1;
                    if (type.size() > 3 && std::isdigit((unsigned char)type.back()) && type.find("vec") != std::string::npos)
                        components = type.back() - '0';
                    std::string args = value;
                    if (size_t paren = args.find('('); paren != std::string::npos)
                        args = args.substr(paren + 1);
                    std::vector<float> numbers;
                    static const std::regex numberRe(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)");
                    for (auto it = std::sregex_iterator(args.begin(), args.end(), numberRe); it != std::sregex_iterator(); ++it)
                        numbers.push_back(std::stof(it->str()));
                    if (value.find("true") != std::string::npos) numbers = { 1.0f };
                    if (value.find("false") != std::string::npos) numbers = { 0.0f };
                    if (numbers.empty())
                        continue;
                    while ((int)numbers.size() < components)
                        numbers.push_back(numbers.back());
                    numbers.resize((size_t)components);
                    const bool isInt = type == "int" || type == "bool" || type == "uint" || type.rfind("ivec", 0) == 0 || type.rfind("bvec", 0) == 0;
                    switch (numbers.size())
                    {
                        case 1: if (isInt) glUniform1i(location, (GLint)numbers[0]); else glUniform1f(location, numbers[0]); break;
                        case 2: if (isInt) glUniform2i(location, (GLint)numbers[0], (GLint)numbers[1]); else glUniform2f(location, numbers[0], numbers[1]); break;
                        case 3: if (isInt) glUniform3i(location, (GLint)numbers[0], (GLint)numbers[1], (GLint)numbers[2]); else glUniform3f(location, numbers[0], numbers[1], numbers[2]); break;
                        default: if (isInt) glUniform4i(location, (GLint)numbers[0], (GLint)numbers[1], (GLint)numbers[2], (GLint)numbers[3]); else glUniform4f(location, numbers[0], numbers[1], numbers[2], numbers[3]); break;
                    }
                }
            }
        }
        if (used)
            glUseProgram((GLuint)previous);
    }

    void OpenGLShader::Bind() const
    {
        if (!m_RendererID)
            return;
        glUseProgram(m_RendererID);
    }

    void OpenGLShader::Unbind() const
    {
        glUseProgram(0);
    }

    void OpenGLShader::SetInt(const std::string& name, int value)
    {
        UploadUniformInt(name, value);
    }

    void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count)
    {
        UploadUniformIntArray(name, values, count);
    }

    void OpenGLShader::SetFloat(const std::string& name, float value)
    {
        UploadUniformFloat(name, value);
    }

    void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        UploadUniformFloat2(name, value);
    }

    void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        UploadUniformFloat3(name, value);
    }

    void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        UploadUniformFloat4(name, value);
    }

    void OpenGLShader::SetFloatArray(const std::string& name, const float* values, uint32_t count)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0 && values && count)
            glUniform1fv(location, (GLsizei)count, values);
    }

    void OpenGLShader::SetFloat3Array(const std::string& name, const glm::vec3* values, uint32_t count)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0 && values && count)
            glUniform3fv(location, (GLsizei)count, glm::value_ptr(values[0]));
    }

    void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        UploadUniformMat4(name, value);
    }

    void OpenGLShader::SetMat4Array(const std::string& name, const glm::mat4* matrices, uint32_t count)
    {
        if (!matrices || count == 0)
            return;
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniformMatrix4fv(location, static_cast<GLsizei>(count), GL_FALSE, glm::value_ptr(matrices[0]));
    }

    void OpenGLShader::UploadUniformInt(const std::string& name, int value)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniform1i(location, value);
    }

    void OpenGLShader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniform1iv(location, count, values);
    }

    void OpenGLShader::UploadUniformFloat(const std::string& name, float value)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniform1f(location, value);
    }

    void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& value)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniform2f(location, value.x, value.y);
    }

    void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& value)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniform3f(location, value.x, value.y, value.z);
    }

    void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& value)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniform4f(location, value.x, value.y, value.z, value.w);
    }

    void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& matrix)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix)
    {
        GLint location = GetLocationCached(name);
        if (location >= 0)
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    int OpenGLShader::GetLocationCached(const std::string& name) const
    {
        auto it = m_UniformLocationCache.find(name);
        if (it != m_UniformLocationCache.end())
            return it->second;
        // Never call GL with an invalid program (silently ignore uncompiled shaders)
        if (!m_RendererID)
        {
            m_UniformLocationCache[name] = -1;
            return -1;
        }
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        m_UniformLocationCache[name] = location;
        return location;
    }
}
