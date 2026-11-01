#include "ShaderCompiler.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/Platform.h"
#include "Renderer/GraphicsAPIs/OpenGL/GLProfile.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <SPIRV/disassemble.h>

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#include <cstdlib>
#include <fstream>
#include <regex>
#include <memory>
#include <mutex>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        bool s_Initialized = false;
        std::mutex s_Mutex;
        ShaderCompiler::Settings s_Settings;
        ShaderCompiler::Stats s_Stats;
        constexpr uint32_t kCacheVersion = 5;   // 4: hedef profil (330 / 410 / ES) + ES blok adları

        EShLanguage ToGlslang(ShaderStage s)
        {
            switch (s)
            {
                case ShaderStage::Vertex: return EShLangVertex;
                case ShaderStage::Fragment: return EShLangFragment;
                case ShaderStage::Geometry: return EShLangGeometry;
                case ShaderStage::Compute: return EShLangCompute;
                case ShaderStage::TessControl: return EShLangTessControl;
                case ShaderStage::TessEvaluation: return EShLangTessEvaluation;
            }
            return EShLangVertex;
        }

        uint64_t Fnv1a(const std::string& s, uint64_t h = 1469598103934665603ull)
        {
            for (unsigned char c : s)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        // Oyunla gelen (önceden ısıtılmış) önbellek: çalışma klasöründe
        std::filesystem::path ShippedCacheDir()
        {
            std::error_code ec;
            return std::filesystem::current_path(ec) / "Cache" / "ShaderCache";
        }

        bool IsWritableDirectory(const std::filesystem::path& dir)
        {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!std::filesystem::is_directory(dir, ec))
                return false;
            const auto probe = dir / ".cq_write_test";
            {
                std::ofstream f(probe, std::ios::binary);
                if (!f || !(f << "1"))
                    return false;
            }
            std::filesystem::remove(probe, ec);
            return true;
        }

        // Yazma hedefi: çalışma klasörü yazılamıyorsa (AGL / Yocto'da /usr/share, Program Files, Android APK dışı salt
        // okunur kurulum) kullanıcı veri klasörü. Sonuç klasör başına bir kez denetlenir.
        std::filesystem::path CacheDir()
        {
            if (!s_Settings.CacheDirectory.empty())
                return s_Settings.CacheDirectory;
            static std::mutex s_DirMutex;
            static std::filesystem::path s_Checked, s_Result;
            const auto local = ShippedCacheDir();
            std::lock_guard<std::mutex> lock(s_DirMutex);
            if (local == s_Checked)
                return s_Result;
            s_Checked = local;
            s_Result = local;
            if (!IsWritableDirectory(local))
            {
                std::string base = Platform::GetUserAppDataDirectory();
                if (base.empty())
                    base = Platform::GetTempDirectory();
                char tag[32];
                snprintf(tag, sizeof(tag), "%016llx", (unsigned long long)Fnv1a(local.string()));
                s_Result = std::filesystem::path(base) / "ConquerorEngine" / "ShaderCache" / tag;
                CQ_CORE_INFO("Shader cache: {0} is read-only, using {1}", local.string(), s_Result.string());
            }
            return s_Result;
        }

        // Önbellek dosyası: [sürüm][aşama sayısı] { aşama, glsl boyutu, glsl, spirv kelime sayısı, spirv }
        bool LoadCache(const std::filesystem::path& file, std::map<ShaderStage, std::string>& glsl, std::map<ShaderStage, std::vector<uint32_t>>& spirv)
        {
            std::ifstream f(file, std::ios::binary);
            if (!f)
                return false;
            uint32_t version = 0, count = 0;
            f.read((char*)&version, 4);
            f.read((char*)&count, 4);
            if (!f || version != kCacheVersion || count > 8)
                return false;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint32_t stage = 0, glslSize = 0, words = 0;
                f.read((char*)&stage, 4);
                f.read((char*)&glslSize, 4);
                if (!f || glslSize > (64u << 20))
                    return false;
                std::string text(glslSize, '\0');
                f.read(text.data(), glslSize);
                f.read((char*)&words, 4);
                if (!f || words > (16u << 20))
                    return false;
                std::vector<uint32_t> code(words);
                f.read((char*)code.data(), (std::streamsize)words * 4);
                if (!f)
                    return false;
                glsl[(ShaderStage)stage] = std::move(text);
                spirv[(ShaderStage)stage] = std::move(code);
            }
            return true;
        }

        void SaveCache(const std::filesystem::path& file, const std::map<ShaderStage, std::string>& glsl, const std::map<ShaderStage, std::vector<uint32_t>>& spirv)
        {
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            std::filesystem::path tmp = file;
            tmp += ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary);
                if (!f)
                    return;
                uint32_t version = kCacheVersion, count = (uint32_t)glsl.size();
                f.write((const char*)&version, 4);
                f.write((const char*)&count, 4);
                for (const auto& [stage, text] : glsl)
                {
                    uint32_t st = (uint32_t)stage, size = (uint32_t)text.size();
                    const auto it = spirv.find(stage);
                    uint32_t words = it != spirv.end() ? (uint32_t)it->second.size() : 0;
                    f.write((const char*)&st, 4);
                    f.write((const char*)&size, 4);
                    f.write(text.data(), size);
                    f.write((const char*)&words, 4);
                    if (words)
                        f.write((const char*)it->second.data(), (std::streamsize)words * 4);
                }
            }
            std::filesystem::rename(tmp, file, ec);
        }

        std::string TypeName(const spirv_cross::Compiler& comp, const spirv_cross::SPIRType& type)
        {
            using BT = spirv_cross::SPIRType::BaseType;
            if (type.basetype == BT::Struct)
                return comp.get_name(type.self).empty() ? "struct" : comp.get_name(type.self);
            if (type.basetype == BT::SampledImage || type.basetype == BT::Image || type.basetype == BT::Sampler)
            {
                switch (type.image.dim)
                {
                    case spv::Dim2D: return type.image.arrayed ? "sampler2DArray" : (type.image.depth ? "sampler2DShadow" : "sampler2D");
                    case spv::Dim3D: return "sampler3D";
                    case spv::DimCube: return "samplerCube";
                    default: return "sampler";
                }
            }
            std::string base;
            switch (type.basetype)
            {
                case BT::Float: base = "float"; break;
                case BT::Int: base = "int"; break;
                case BT::UInt: base = "uint"; break;
                case BT::Boolean: base = "bool"; break;
                case BT::Double: base = "double"; break;
                default: base = "?"; break;
            }
            if (type.columns > 1)
                return (base == "float" ? std::string("mat") : base.substr(0, 1) + "mat") + std::to_string(type.columns) +
                       (type.vecsize != type.columns ? "x" + std::to_string(type.vecsize) : "");
            if (type.vecsize > 1)
            {
                std::string prefix = base == "float" ? "" : base == "int" ? "i" : base == "uint" ? "u" : base == "bool" ? "b" : "d";
                return prefix + "vec" + std::to_string(type.vecsize);
            }
            return base;
        }
    }

    const char* ShaderCompiler::StageName(ShaderStage s)
    {
        switch (s)
        {
            case ShaderStage::Vertex: return "vertex";
            case ShaderStage::Fragment: return "fragment";
            case ShaderStage::Geometry: return "geometry";
            case ShaderStage::Compute: return "compute";
            case ShaderStage::TessControl: return "tess_control";
            case ShaderStage::TessEvaluation: return "tess_evaluation";
        }
        return "?";
    }

    void ShaderCompiler::Init()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Initialized)
            return;
        glslang::InitializeProcess();
        s_Initialized = true;
        if (const char* env = std::getenv("CQ_SHADER_SPIRV"))
            s_Settings.Enabled = std::string(env) != "0";
        CQ_CORE_INFO("ShaderCompiler: SPIR-V pipeline {0} (glslang + SPIRV-Cross)", s_Settings.Enabled ? "enabled" : "disabled");
    }

    void ShaderCompiler::Shutdown()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (!s_Initialized)
            return;
        glslang::FinalizeProcess();
        s_Initialized = false;
        CQ_CORE_INFO("ShaderCompiler: {0} compiled, {1} cache hits, {2} fallbacks", s_Stats.Compiled, s_Stats.CacheHits, s_Stats.Fallbacks);
    }

    ShaderCompiler::Settings& ShaderCompiler::GetSettings() { return s_Settings; }
    const ShaderCompiler::Stats& ShaderCompiler::GetStats() { return s_Stats; }

    bool ShaderCompiler::CompileToSPIRV(const std::map<ShaderStage, std::string>& sources, const std::string& name,
                                        std::map<ShaderStage, std::vector<uint32_t>>& outSpirv, std::string& errors, bool vulkanRelaxed)
    {
        if (!s_Initialized)
            Init();

        std::vector<std::unique_ptr<glslang::TShader>> shaders;
        glslang::TProgram program;
        const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgDefault);
        std::vector<std::string> names;
        names.reserve(sources.size());

        for (const auto& [stage, src] : sources)
        {
            auto shader = std::make_unique<glslang::TShader>(ToGlslang(stage));
            const char* str = src.c_str();
            names.push_back(name + ":" + StageName(stage));
            const char* nm = names.back().c_str();
            shader->setStringsWithLengthsAndNames(&str, nullptr, &nm, 1);
            if (vulkanRelaxed)
            {
                shader->setEnvInput(glslang::EShSourceGlsl, ToGlslang(stage), glslang::EShClientVulkan, 100);
                shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
                shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
                shader->setEnvInputVulkanRulesRelaxed();
                shader->setGlobalUniformBlockName("CQ_Globals");
                shader->setGlobalUniformSet(0);
                shader->setGlobalUniformBinding(15);
            }
            else
            {
                shader->setEnvInput(glslang::EShSourceGlsl, ToGlslang(stage), glslang::EShClientOpenGL, 450);
                shader->setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
                shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
                // OpenGL ES hedefi: gölgelendiriciler ES'ye özgü yolları seçebilsin (ör. derinlik dokusu filtrelenemez)
                if (GLProfile::Current().GLSLES)
                    shader->setPreamble("#define CQ_GLES 1\n");
            }
            shader->setAutoMapLocations(true);
            shader->setAutoMapBindings(true);
            if (!shader->parse(GetDefaultResources(), 450, false, messages))
            {
                errors += std::string("[") + StageName(stage) + "] " + shader->getInfoLog() + "\n";
                return false;
            }
            program.addShader(shader.get());
            shaders.push_back(std::move(shader));
        }

        if (!program.link(messages))
        {
            errors += std::string("[link] ") + program.getInfoLog() + "\n";
            return false;
        }
        if (!program.mapIO())
        {
            errors += std::string("[mapIO] ") + program.getInfoLog() + "\n";
            return false;
        }

        for (const auto& [stage, src] : sources)
        {
            glslang::TIntermediate* inter = program.getIntermediate(ToGlslang(stage));
            if (!inter)
            {
                errors += std::string("[") + StageName(stage) + "] no intermediate\n";
                return false;
            }
            glslang::SpvOptions options;
            options.generateDebugInfo = false;
            options.disableOptimizer = true;
            options.validate = false;
            std::vector<uint32_t> spirv;
            spv::SpvBuildLogger logger;
            glslang::GlslangToSpv(*inter, spirv, &logger, &options);
            if (spirv.empty())
            {
                errors += std::string("[") + StageName(stage) + "] SPIR-V generation failed: " + logger.getAllMessages() + "\n";
                return false;
            }
            outSpirv[stage] = std::move(spirv);
        }
        return true;
    }

    bool ShaderCompiler::CrossCompile(const std::vector<uint32_t>& spirv, const std::string& language, std::string& out, std::string& errors)
    {
        try
        {
            if (language == "glsl")
            {
                spirv_cross::CompilerGLSL comp(spirv);
                // Gevşek uniform'ların otomatik atanan location/binding'leri aşama başına bağımsızdır ve
                // bağlamada çakışır; orijinal kaynaktaki gibi sürücüye bırakılır (ada göre sorgulanır).
                comp.get_shader_resources(); // tüm değişkenleri tara
                comp.get_active_interface_variables();
                for (uint32_t id = 0; id < (uint32_t)comp.get_ir().ids.size(); ++id)
                {
                    if (comp.get_ir().ids[id].get_type() != spirv_cross::TypeVariable)
                        continue;
                    if (comp.get_storage_class(id) != spv::StorageClassUniformConstant)
                        continue;
                    comp.unset_decoration(id, spv::DecorationBinding);
                    comp.unset_decoration(id, spv::DecorationLocation);
                }
                // Hedef: etkin OpenGL profili (4.5: 450; 4.1 / 3.3 core; ES 3.x: 300/310/320 es)
                const auto& profile = GLProfile::Current();
                spirv_cross::CompilerGLSL::Options opt;
                opt.version = profile.GLSLVersion;
                opt.es = profile.GLSLES;
                opt.vulkan_semantics = false;
                opt.enable_420pack_extension = profile.GLSLVersion >= 420 || profile.Use420Pack;
                if (profile.GLSLES)
                {
                    opt.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
                    opt.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
                }
                comp.set_common_options(opt);
                // GLSL ES'de aşamalar arası bloklar düzleştirilir ve değişkenler ADLA eşleşir: "vs_out" / "fs_in" gibi
                // farklı örnek adları yerine iki aşamada da aynı olan blok tür adı (VS_OUT_FragPos) kullanılır
                if (profile.GLSLES)
                {
                    const auto resources = comp.get_shader_resources();
                    for (const auto* list : { &resources.stage_inputs, &resources.stage_outputs })
                        for (const auto& io : *list)
                            if (comp.get_type(io.base_type_id).basetype == spirv_cross::SPIRType::Struct)
                            {
                                const std::string blockName = comp.get_name(io.base_type_id);
                                if (!blockName.empty())
                                    comp.set_name(io.id, blockName);
                            }
                }
                // layout(binding) yazılamayan hedeflerde blok bağlama noktaları bağlamadan sonra glUniformBlockBinding ile
                std::string directives;
                if (!profile.UniformBindingLayouts)
                    for (const auto& ub : comp.get_shader_resources().uniform_buffers)
                        if (comp.has_decoration(ub.id, spv::DecorationBinding))
                            directives += "// CQ_UBO_BINDING " + comp.get_name(ub.base_type_id) + " " +
                                          std::to_string(comp.get_decoration(ub.id, spv::DecorationBinding)) + "\n";
                out = comp.compile();
                // ES: uniform başlangıç değeri yazılamaz -> bildirimden çıkar, bağlamadan sonra glUniform ile ver
                if (!profile.UniformInitializers)
                {
                    static const std::regex initRe(R"(uniform\s+((?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*=\s*([^;]+);)");
                    std::string result;
                    auto begin = std::sregex_iterator(out.begin(), out.end(), initRe);
                    size_t last = 0;
                    for (auto it = begin; it != std::sregex_iterator(); ++it)
                    {
                        const std::smatch& m = *it;
                        result += out.substr(last, (size_t)m.position(0) - last);
                        result += "uniform " + m[1].str() + m[2].str() + " " + m[3].str() + ";";
                        directives += "// CQ_UNIFORM_DEFAULT " + m[2].str() + " " + m[3].str() + " " + m[4].str() + "\n";
                        last = (size_t)(m.position(0) + m.length(0));
                    }
                    result += out.substr(last);
                    out = std::move(result);
                }
                if (!directives.empty())
                    out += "\n" + directives;
            }
            else if (language == "hlsl")
            {
                spirv_cross::CompilerHLSL comp(spirv);
                spirv_cross::CompilerHLSL::Options opt;
                opt.shader_model = 50;
                comp.set_hlsl_options(opt);
                out = comp.compile();
            }
            else if (language == "msl")
            {
                spirv_cross::CompilerMSL comp(spirv);
                spirv_cross::CompilerMSL::Options opt;
                opt.platform = spirv_cross::CompilerMSL::Options::macOS;
                opt.set_msl_version(2, 1);
                comp.set_msl_options(opt);
                out = comp.compile();
            }
            else
            {
                errors = "unknown target language " + language;
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            errors = std::string("SPIRV-Cross (") + language + "): " + e.what();
            return false;
        }
    }

    void ShaderCompiler::Reflect(const std::vector<uint32_t>& spirv, ShaderStage stage, ShaderReflection& reflection)
    {
        try
        {
            spirv_cross::Compiler comp(spirv);
            auto resources = comp.get_shader_resources();
            const int mask = 1 << (int)stage;
            auto add = [&](ShaderUniformInfo info) {
                for (auto& u : reflection.Uniforms)
                    if (u.Name == info.Name)
                    {
                        u.StageMask |= mask;
                        return;
                    }
                info.StageMask = mask;
                reflection.Uniforms.push_back(std::move(info));
            };
            for (const auto& ub : resources.uniform_buffers)
            {
                ShaderUniformInfo info;
                const auto& type = comp.get_type(ub.base_type_id);
                info.Name = comp.get_name(ub.id).empty() ? ub.name : comp.get_name(ub.id);
                info.Type = comp.get_name(ub.base_type_id);
                info.IsBlock = true;
                info.Binding = comp.has_decoration(ub.id, spv::DecorationBinding) ? (int)comp.get_decoration(ub.id, spv::DecorationBinding) : -1;
                info.Size = (uint32_t)comp.get_declared_struct_size(type);
                add(info);
            }
            for (const auto& s : resources.sampled_images)
            {
                ShaderUniformInfo info;
                const auto& type = comp.get_type(s.type_id);
                info.Name = s.name;
                info.Type = TypeName(comp, comp.get_type(s.base_type_id));
                info.IsSampler = true;
                info.ArraySize = type.array.empty() ? 1 : type.array[0];
                info.Binding = comp.has_decoration(s.id, spv::DecorationBinding) ? (int)comp.get_decoration(s.id, spv::DecorationBinding) : -1;
                add(info);
            }
            // Gevşek (blok dışı) uniform'lar: OpenGL'de UniformConstant depolama
            comp.get_active_interface_variables();
            for (uint32_t id : comp.get_active_interface_variables())
            {
                if (comp.get_storage_class(id) != spv::StorageClassUniformConstant)
                    continue;
                const auto& type = comp.get_type(comp.get_type_from_variable(id).self);
                const auto& varType = comp.get_type_from_variable(id);
                if (type.basetype == spirv_cross::SPIRType::SampledImage || type.basetype == spirv_cross::SPIRType::Image ||
                    type.basetype == spirv_cross::SPIRType::Sampler)
                    continue;
                ShaderUniformInfo info;
                info.Name = comp.get_name(id);
                if (info.Name.empty())
                    continue;
                info.Type = TypeName(comp, type);
                info.ArraySize = varType.array.empty() ? 1 : varType.array[0];
                add(info);
            }
            reflection.InstructionCount += (uint32_t)spirv.size();
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("ShaderCompiler: reflection failed: {0}", e.what());
        }
    }

    std::string ShaderCompiler::Disassemble(const std::vector<uint32_t>& spirv)
    {
        std::ostringstream ss;
        spv::Disassemble(ss, spirv);
        return ss.str();
    }

    bool ShaderCompiler::CompileForOpenGL(const std::map<ShaderStage, std::string>& sources, const std::string& name,
                                          std::map<ShaderStage, std::string>& outGLSL, ShaderReflection* reflection, std::string& errors,
                                          std::map<ShaderStage, std::vector<uint32_t>>* outSpirv)
    {
        if (!s_Settings.Enabled)
            return false;

        // Önbellek anahtarı: tüm aşama kaynakları + hat sürümü
        // Hedef profil anahtarın parçası: 450 ile "330" / "300 es" çıktıları karışmasın
        const auto& profile = GLProfile::Current();
        uint64_t h = Fnv1a("cq-spirv-gl" + std::to_string(profile.GLSLVersion) + (profile.GLSLES ? "es" : "") +
                           (profile.UniformBindingLayouts ? "b" : "") + "-v" + std::to_string(kCacheVersion));
        for (const auto& [stage, src] : sources)
            h = Fnv1a(std::string(StageName(stage)) + "\n" + src, h);
        char hex[32];
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);
        const std::filesystem::path cacheFile = CacheDir() / (std::string(hex) + ".cqsc");

        std::map<ShaderStage, std::vector<uint32_t>> spirv;
        auto loadCached = [&]() {
            if (LoadCache(cacheFile, outGLSL, spirv) && outGLSL.size() == sources.size())
                return true;
            // Yazma hedefi kullanıcı klasörüne taşındıysa oyunla gelen önbellek yine okunur
            const auto shipped = ShippedCacheDir() / cacheFile.filename();
            outGLSL.clear();
            spirv.clear();
            return shipped != cacheFile && LoadCache(shipped, outGLSL, spirv) && outGLSL.size() == sources.size();
        };
        if (s_Settings.UseCache && loadCached())
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Stats.CacheHits++;
        }
        else
        {
            outGLSL.clear();
            spirv.clear();
            if (!CompileToSPIRV(sources, name, spirv, errors))
            {
                std::lock_guard<std::mutex> lock(s_Mutex);
                s_Stats.Fallbacks++;
                return false;
            }
            for (const auto& [stage, code] : spirv)
            {
                std::string glsl, err;
                if (!CrossCompile(code, "glsl", glsl, err))
                {
                    errors += err;
                    std::lock_guard<std::mutex> lock(s_Mutex);
                    s_Stats.Fallbacks++;
                    return false;
                }
                if (std::getenv("CQ_SHADER_DUMP"))
                {
                    std::error_code ec;
                    std::filesystem::path dir = CacheDir().parent_path() / "ShaderDump";
                    std::filesystem::create_directories(dir, ec);
                    std::ofstream(dir / (name + "_" + StageName(stage) + ".glsl")) << glsl;
                    std::ofstream(dir / (name + "_" + StageName(stage) + ".src.glsl")) << sources.at(stage);
                }
                outGLSL[stage] = std::move(glsl);
            }
            if (s_Settings.UseCache)
                SaveCache(cacheFile, outGLSL, spirv);
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Stats.Compiled++;
        }

        if (reflection)
        {
            reflection->Uniforms.clear();
            reflection->InstructionCount = 0;
            for (const auto& [stage, code] : spirv)
                Reflect(code, stage, *reflection);
        }
        if (outSpirv)
            *outSpirv = std::move(spirv);
        return true;
    }
}
