#if defined(CQ_D3D11_ENABLED) || defined(CQ_D3D12_ENABLED)

// Yansıtma ortak (GLEmu/GLReflect.cpp); burada SPIR-V -> HLSL (SPIRV-Cross, SM 5.0, y çevirme + derinlik [0,1]) ->
// DXBC (d3dcompiler_47). D3D11 ve D3D12 aynı DXBC'yi kullanır.

#include "DxShared.h"
#include "Core/Logging/Log.h"

#include <d3dcompiler.h>
#include <spirv_hlsl.hpp>

#include <cstdlib>
#include <fstream>
#include <regex>

namespace Conqueror::DXCommon
{
    namespace
    {
        // Bazı HLSL derleyicileri (Wine/vkd3d) isnan/isinf içsel işlevlerini tanımaz: bit deseniyle birebir eşdeğeri
        std::string PortableHLSL(const std::string& hlsl)
        {
            static const std::regex s_IsNan(R"(\bisnan\s*\()"), s_IsInf(R"(\bisinf\s*\()");
            const bool nan = std::regex_search(hlsl, s_IsNan), inf = std::regex_search(hlsl, s_IsInf);
            if (!nan && !inf)
                return hlsl;
            std::string helpers;
            const char* types[][2] = { { "float", "bool" }, { "float2", "bool2" }, { "float3", "bool3" }, { "float4", "bool4" } };
            for (const auto& t : types)
            {
                if (nan)
                    helpers += std::string(t[1]) + " cq_isnan(" + t[0] + " x) { return (asuint(x) & 0x7FFFFFFFu) > 0x7F800000u; }\n";
                if (inf)
                    helpers += std::string(t[1]) + " cq_isinf(" + t[0] + " x) { return (asuint(x) & 0x7FFFFFFFu) == 0x7F800000u; }\n";
            }
            std::string out = std::regex_replace(hlsl, s_IsNan, "cq_isnan(");
            out = std::regex_replace(out, s_IsInf, "cq_isinf(");
            return helpers + out;
        }

    }

    bool CompileHLSL(const std::string& source, const char* name, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& errors)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompile(source.data(), source.size(), name, nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL2, 0, &blob, &err);
        if (err)
        {
            errors.assign((const char*)err->GetBufferPointer(), err->GetBufferSize());
            err->Release();
        }
        if (FAILED(hr) || !blob)
        {
            if (errors.empty())
                errors = "D3DCompile failed";
            if (blob)
                blob->Release();
            return false;
        }
        out.assign((const uint8_t*)blob->GetBufferPointer(), (const uint8_t*)blob->GetBufferPointer() + blob->GetBufferSize());
        blob->Release();
        return true;
    }

    bool TranslateProgram(const std::vector<ShaderSource>& shaders, uint64_t id, const ShaderLimits& limits, TranslatedProgram& p, std::string& log)
    {
        p = TranslatedProgram{};
        if (!GLEmu::ReflectProgram(shaders, limits, p, log))
            return false;
        static const char* s_Dump = std::getenv("CQ_DXGL_DUMP");
        for (int stage = 0; stage < 2; ++stage)
        {
            const bool vs = stage == 0;
            std::string hlsl;
            try
            {
                spirv_cross::CompilerHLSL hc(vs ? p.VertexSpirv : p.FragmentSpirv);
                spirv_cross::CompilerHLSL::Options ho;
                ho.shader_model = 50;
                ho.flatten_matrix_vertex_input_semantics = true;
                hc.set_hlsl_options(ho);
                spirv_cross::CompilerGLSL::Options go = hc.get_common_options();
                go.vertex.flip_vert_y = true;          // GL satır düzeni korunur (görüntüler GL yönünde tutulur)
                go.vertex.fixup_clipspace = true;      // z: [-w,w] -> [0,w]
                hc.set_common_options(go);
                hlsl = PortableHLSL(hc.compile());
            }
            catch (const std::exception& e)
            {
                log = std::string("SPIRV-Cross HLSL: ") + e.what();
                return false;
            }
            if (s_Dump && *s_Dump)
                std::ofstream(std::string(s_Dump) + "/prog" + std::to_string(id) + (vs ? ".vs.hlsl" : ".ps.hlsl")) << hlsl;
            std::string err;
            if (!CompileHLSL(hlsl, vs ? "dxgl_vs" : "dxgl_ps", "main", vs ? "vs_5_0" : "ps_5_0", vs ? p.VS : p.PS, err))
            {
                log = std::string(vs ? "[vertex] " : "[fragment] ") + err;
                return false;
            }
        }
        return true;
    }
}

#endif
