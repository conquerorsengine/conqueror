#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Conqueror
{
    enum class ShaderStage : int { Vertex = 0, Fragment, Geometry, Compute, TessControl, TessEvaluation };

    struct ShaderUniformInfo
    {
        std::string Name;
        std::string Type;          // "float", "vec3", "sampler2D", "Material" ...
        uint32_t ArraySize = 1;
        bool IsSampler = false;
        bool IsBlock = false;      // uniform blok (UBO)
        int Binding = -1;
        uint32_t Size = 0;         // bayt (bloklar)
        int StageMask = 0;         // 1 << ShaderStage
    };

    struct ShaderReflection
    {
        std::vector<ShaderUniformInfo> Uniforms;
        uint32_t InstructionCount = 0;   // SPIR-V kelime sayısı (karmaşıklık göstergesi)
    };

    // GLSL -> SPIR-V (glslang) -> hedef dil (SPIRV-Cross) hattı.
    // OpenGL arka ucu çapraz derlenmiş GLSL 450 kullanır; aynı SPIR-V ileride
    // Vulkan'a doğrudan, DirectX'e HLSL, Metal'e MSL olarak verilir.
    class CQ_API ShaderCompiler
    {
    public:
        struct Settings
        {
            bool Enabled = true;                 // CQ_SHADER_SPIRV=0 ile kapatılır
            bool UseCache = true;
            std::filesystem::path CacheDirectory;  // boşsa <çalışma dizini>/Cache/ShaderCache
        };

        struct Stats
        {
            uint32_t Compiled = 0;
            uint32_t CacheHits = 0;
            uint32_t Fallbacks = 0;
        };

        static void Init();
        static void Shutdown();
        static Settings& GetSettings();
        static const Stats& GetStats();

        // Tüm aşamaları birlikte derle + bağla (aşamalar arası arayüz eşleşmesi).
        // outGLSL: OpenGL için çapraz derlenmiş kaynaklar. Başarısızsa false ve errors.
        static bool CompileForOpenGL(const std::map<ShaderStage, std::string>& sources, const std::string& name,
                                     std::map<ShaderStage, std::string>& outGLSL, ShaderReflection* reflection, std::string& errors,
                                     std::map<ShaderStage, std::vector<uint32_t>>* outSpirv = nullptr);

        // vulkanRelaxed: Vulkan SPIR-V üretir; gevşek uniform'lar "CQ_Globals" bloğuna toplanır
        // (Vulkan doğrudan, DirectX için HLSL, Metal için MSL bu SPIR-V'den üretilir)
        static bool CompileToSPIRV(const std::map<ShaderStage, std::string>& sources, const std::string& name,
                                   std::map<ShaderStage, std::vector<uint32_t>>& outSpirv, std::string& errors, bool vulkanRelaxed = false);
        static bool CrossCompile(const std::vector<uint32_t>& spirv, const std::string& language, std::string& out, std::string& errors);
        static void Reflect(const std::vector<uint32_t>& spirv, ShaderStage stage, ShaderReflection& reflection);

        static std::string Disassemble(const std::vector<uint32_t>& spirv);
        static const char* StageName(ShaderStage stage);
    };
}
