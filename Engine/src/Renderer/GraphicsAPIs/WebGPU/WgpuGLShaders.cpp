#ifdef CQ_WEBGPU_ENABLED

// Program bağlama: GLSL -> SPIR-V (GLEmu yansıtması, ortak) -> WGSL (Tint) -> WGPUShaderModule. Köşe gölgelendiricisinin sonuna GL
// yönü eklenir (y çevrimi, z: [-w,w] -> [0,w]). Doku / örnekleyici bağlamaları WGSL'den okunur.

#include "WgpuGL.h"
#include "TintBridge.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>

namespace Conqueror::WgpuGL
{
    using WGPU::W;

    namespace
    {
        constexpr uint32_t kTextureBase = 32;            // dokular / örnekleyiciler: GLEmu yazmacı + 32 (sabit arabellekler 0..31)
        constexpr uint32_t kSamplerOffset = 64;          // birleşik örnekleyiciden ayrılan örnekleyici = doku bağlaması + 64

        // GLEmu yazmaçları D3D / Metal gibi ayrı alanlarda (sabit arabellek b0.., doku t0..); WebGPU'da tek bağlama alanı var:
        // UniformConstant değişkenlerinin (doku, örnekleyici) Binding süslemesi kTextureBase kaydırılır
        void ShiftTextureBindings(std::vector<uint32_t>& spv)
        {
            if (spv.size() < 5)
                return;
            std::vector<uint32_t> uniformConstant;
            for (size_t i = 5; i < spv.size();)
            {
                const uint32_t op = spv[i] & 0xFFFF, count = spv[i] >> 16;
                if (count == 0)
                    break;
                if (op == 59 && count >= 4 && spv[i + 3] == 0)   // OpVariable %type %id UniformConstant
                    uniformConstant.push_back(spv[i + 2]);
                i += count;
            }
            for (size_t i = 5; i < spv.size();)
            {
                const uint32_t op = spv[i] & 0xFFFF, count = spv[i] >> 16;
                if (count == 0)
                    break;
                if (op == 71 && count >= 4 && spv[i + 2] == 33 &&   // OpDecorate %id Binding N
                    std::find(uniformConstant.begin(), uniformConstant.end(), spv[i + 1]) != uniformConstant.end())
                    spv[i + 3] += kTextureBase;
                i += count;
            }
        }

        // WGSL NaN / sonsuz temsil etmez (Tint isnan / isinf'i reddeder): bit düzeyinde aynı denetim
        const char* kNanHelpers = R"(
bool cq_isnan(float x) { return (floatBitsToUint(x) & 0x7fffffffu) > 0x7f800000u; }
bvec2 cq_isnan(vec2 x) { return greaterThan(floatBitsToUint(x) & uvec2(0x7fffffffu), uvec2(0x7f800000u)); }
bvec3 cq_isnan(vec3 x) { return greaterThan(floatBitsToUint(x) & uvec3(0x7fffffffu), uvec3(0x7f800000u)); }
bvec4 cq_isnan(vec4 x) { return greaterThan(floatBitsToUint(x) & uvec4(0x7fffffffu), uvec4(0x7f800000u)); }
bool cq_isinf(float x) { return (floatBitsToUint(x) & 0x7fffffffu) == 0x7f800000u; }
bvec2 cq_isinf(vec2 x) { return equal(floatBitsToUint(x) & uvec2(0x7fffffffu), uvec2(0x7f800000u)); }
bvec3 cq_isinf(vec3 x) { return equal(floatBitsToUint(x) & uvec3(0x7fffffffu), uvec3(0x7f800000u)); }
bvec4 cq_isinf(vec4 x) { return equal(floatBitsToUint(x) & uvec4(0x7fffffffu), uvec4(0x7f800000u)); }
#define isnan cq_isnan
#define isinf cq_isinf
)";

        std::string PatchNan(const std::string& src)
        {
            if (src.find("isnan") == std::string::npos && src.find("isinf") == std::string::npos)
                return src;
            const size_t v = src.find("#version");
            const size_t eol = v == std::string::npos ? std::string::npos : src.find('\n', v);
            if (eol == std::string::npos)
                return std::string(kNanHelpers) + src;
            return src.substr(0, eol + 1) + kNanHelpers + src.substr(eol + 1);
        }

        // Dawn, parça çıktısının hedef biçimden az bileşenli olmasına izin vermez (vec2 -> RGBA8 geçersiz): 1-3 bileşenli çıktılar
        // özel değişkene yazılır, sarmalayıcı main 4 bileşenli gerçek çıktıya (0, 0, 1 ile) kopyalar
        std::string PatchFragmentOutputs(const std::string& src, uint8_t* comps)
        {
            static const std::regex outRe(R"((layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*)?out\s+(highp\s+|mediump\s+|lowp\s+)?(float|vec2|vec3|int|ivec2|ivec3|uint|uvec2|uvec3)\s+(\w+)\s*;)");
            std::string s = src;
            std::string copies;
            std::smatch m;
            std::string::const_iterator from = s.cbegin();
            std::string result;
            bool any = false;
            while (std::regex_search(from, s.cend(), m, outRe))
            {
                const std::string loc = m[2].matched ? m[2].str() : "0";
                const std::string type = m[4].str(), name = m[5].str();
                const bool isInt = type[0] == 'i', isUint = type[0] == 'u';
                const int n = type == "float" || type == "int" || type == "uint" ? 1 : type.back() - '0';
                const std::string v4 = isInt ? "ivec4" : isUint ? "uvec4" : "vec4";
                const std::string zero = isInt ? "0" : isUint ? "0u" : "0.0", one = isInt ? "1" : isUint ? "1u" : "1.0";
                result.append(from, m[0].first);
                result += type + " " + name + ";\nlayout(location = " + loc + ") out " + v4 + " cq_wgpu_out" + loc + ";";
                std::string expr = v4 + "(" + name;
                for (int k = n; k < 4; ++k)
                    expr += ", " + std::string(k == 3 ? one : zero);
                copies += "    cq_wgpu_out" + loc + " = " + expr + ");\n";
                if (const int l = std::atoi(loc.c_str()); l >= 0 && l < 8)
                    comps[l] = (uint8_t)n;   // yazma maskesi asıl bileşenlerle sınırlı kalır (GL: diğerleri tanımsız)
                from = m[0].second;
                any = true;
            }
            if (!any)
                return src;
            result.append(from, s.cend());
            static const std::regex mainRe(R"(\bvoid\s+main\s*\(\s*(void)?\s*\))");
            if (!std::regex_search(result, mainRe))
                return src;
            result = std::regex_replace(result, mainRe, "void cq_wgpu_frag_main()", std::regex_constants::format_first_only);
            result += "\nvoid main()\n{\n    cq_wgpu_frag_main();\n" + copies + "}\n";
            return result;
        }

        std::string PatchVertex(const std::string& src)
        {
            std::string s = src;
            std::regex mainRe(R"(\bvoid\s+main\s*\(\s*(void)?\s*\))");
            if (!std::regex_search(s, mainRe))
                return s;
            s = std::regex_replace(s, mainRe, "void cq_wgpu_main()", std::regex_constants::format_first_only);
            s += "\nvoid main()\n{\n    cq_wgpu_main();\n    gl_Position.y = -gl_Position.y;\n"
                 "    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n";
            return s;
        }

        WGPUShaderModule MakeModule(const std::string& wgsl, const char* label, std::string& log)
        {
            WGPUShaderSourceWGSL src{};
            src.chain.sType = WGPUSType_ShaderSourceWGSL;
            src.code = WGPU::Str(wgsl.c_str());
            WGPUShaderModuleDescriptor d{};
            d.nextInChain = &src.chain;
            d.label = WGPU::Str(label);
            WGPUShaderModule m = W().DeviceCreateShaderModule(Device(), &d);
            if (!m)
                log += std::string(label) + ": shader module creation failed\n";
            return m;
        }

        bool ToWgsl(const std::vector<uint32_t>& spirv, const std::vector<uint32_t>& combined, std::string& out, std::string& log, const char* stage)
        {
            char* wgsl = nullptr;
            char* err = nullptr;
            const int ok = cq_tint_spirv_to_wgsl(spirv.data(), spirv.size(), combined.data(), combined.size(), kSamplerOffset, &wgsl, &err);
            if (!ok)
            {
                log += std::string("[") + stage + "] Tint: " + (err ? err : "conversion failed") + "\n";
                cq_tint_free(err);
                return false;
            }
            out = wgsl;
            cq_tint_free(wgsl);
            return true;
        }

        // Parça çıktılarının bileşen sayıları: "-> @location(0) vec2<f32>" ya da çıktı yapısının @location üyeleri
        void ScanOutputs(const std::string& wgsl, uint8_t out[8])
        {
            auto comps = [](const std::string& type) -> uint8_t {
                if (type.rfind("vec", 0) == 0 && type.size() > 3 && type[3] >= '2' && type[3] <= '4')
                    return (uint8_t)(type[3] - '0');
                return 1;
            };
            static const std::regex single(R"(@fragment\s*fn\s+\w+\s*\([^)]*\)\s*->\s*@location\((\d+)u?\)\s*([a-z0-9]+))");
            std::smatch m;
            if (std::regex_search(wgsl, m, single))
            {
                const int loc = std::stoi(m[1].str());
                if (loc >= 0 && loc < 8)
                    out[loc] = comps(m[2].str());
                return;
            }
            static const std::regex ret(R"(@fragment\s*fn\s+\w+\s*\([^)]*\)\s*->\s*(\w+))");
            if (!std::regex_search(wgsl, m, ret))
                return;
            const std::string name = m[1].str();
            const size_t at = wgsl.find("struct " + name + " {");
            if (at == std::string::npos)
                return;
            const size_t end = wgsl.find('}', at);
            const std::string body = wgsl.substr(at, end - at);
            static const std::regex member(R"(@location\((\d+)u?\)\s*\w+\s*:\s*([a-z0-9]+))");
            for (auto it = std::sregex_iterator(body.begin(), body.end(), member); it != std::sregex_iterator(); ++it)
            {
                const int loc = std::stoi((*it)[1].str());
                if (loc >= 0 && loc < 8)
                    out[loc] = comps((*it)[2].str());
            }
        }

        // WGSL'deki doku bağlamaları: @group(0) @binding(N) var X : texture_depth_2d / texture_2d<f32> / texture_cube<i32> ...
        struct WgslTexture { uint32_t Binding; bool Depth; bool Integer; bool Multisampled; bool Cube; };
        std::vector<WgslTexture> ScanTextures(const std::string& wgsl)
        {
            std::vector<WgslTexture> out;
            static const std::regex re(R"(@binding\((\d+)u?\)\s*var\s+\w+\s*:\s*(texture_[a-z_0-9]+)(<\s*([a-z0-9]+)\s*>)?)");
            for (auto it = std::sregex_iterator(wgsl.begin(), wgsl.end(), re); it != std::sregex_iterator(); ++it)
            {
                const std::string type = (*it)[2].str();
                const std::string elem = (*it)[4].str();
                WgslTexture t{};
                t.Binding = (uint32_t)std::stoul((*it)[1].str());
                t.Depth = type.find("depth") != std::string::npos;
                t.Multisampled = type.find("multisampled") != std::string::npos;
                t.Cube = type.find("cube") != std::string::npos;
                t.Integer = elem == "i32" || elem == "u32";
                out.push_back(t);
            }
            return out;
        }
    }

    void DestroyProgram(Program& p)
    {
        if (p.VS) W().ShaderModuleRelease(p.VS);
        if (p.FS) W().ShaderModuleRelease(p.FS);
        p.VS = p.FS = nullptr;
        p.GlobalsFrame = 0;
        p.Textures.clear();
    }

    bool LinkProgram(Program& p)
    {
        auto& c = Ctx();
        DestroyProgram(p);
        p.Linked = false;
        p.Log.clear();

        uint8_t narrowOutputs[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<std::string> patched;
        patched.reserve(p.Shaders.size());
        std::vector<GLEmu::ShaderSource> shaders;
        for (uint32_t name : p.Shaders)
            if (auto it = c.ShaderObjects.find(name); it != c.ShaderObjects.end())
            {
                patched.push_back(PatchNan(it->second->Type == GL_VERTEX_SHADER ? PatchVertex(it->second->Source)
                                                                                 : PatchFragmentOutputs(it->second->Source, narrowOutputs)));
                shaders.push_back({ it->second->Type, nullptr });
            }
        for (size_t i = 0; i < shaders.size(); ++i)
            shaders[i].Source = &patched[i];
        // Tint'in SPIR-V okuyucusu birleşik çok örnekli örnekleyicide iç hatayla durur (sayfa kapanırdı): önceden reddet
        for (const auto& src : patched)
            if (src.find("sampler2DMS") != std::string::npos)
            {
                p.Log = "Multisample textures (sampler2DMS) are not supported by the WebGPU backend yet";
                return false;
            }
        GLEmu::ResourceLimits limits;
        const WGPULimits& dl = DeviceLimits();           // cihazın sınırları (aşama başına; varsayılan 12 / 16 / 16)
        limits.ConstantBuffers = std::max<uint32_t>(12, dl.maxUniformBuffersPerShaderStage);
        limits.Resources = std::max<uint32_t>(16, dl.maxSampledTexturesPerShaderStage);
        limits.Samplers = std::max<uint32_t>(16, dl.maxSamplersPerShaderStage);
        limits.ApiName = "WebGPU";
        GLEmu::ReflectedProgram rp;
        if (!GLEmu::ReflectProgram(shaders, limits, rp, p.Log))
            return false;

        ShiftTextureBindings(rp.VertexSpirv);
        ShiftTextureBindings(rp.FragmentSpirv);
        std::vector<uint32_t> combined;
        for (const auto& s : rp.Samplers)
            if (!s.SharedSampler)
                for (uint32_t e = 0; e < std::max(1u, s.Count); ++e)
                    combined.push_back(kTextureBase + s.Register + e);
        std::string vs, fs;
        if (!ToWgsl(rp.VertexSpirv, combined, vs, p.Log, "vertex") || !ToWgsl(rp.FragmentSpirv, combined, fs, p.Log, "fragment"))
            return false;
        static const char* s_Dump = std::getenv("CQ_WGPUGL_DUMP");
        if (s_Dump && std::strncmp(s_Dump, "log:", 4) == 0)
        {
            // CQ_WGPUGL_DUMP=log:<metin>: GLSL kaynağında <metin> geçen programın WGSL'i günlüğe (web: dosya sistemi yok)
            bool match = false;
            for (const auto& src : patched)
                match = match || src.find(s_Dump + 4) != std::string::npos;
            if (match)
                CQ_CORE_INFO("[WgpuGL dump] program {0}\n--- vertex ---\n{1}\n--- fragment ---\n{2}", p.Id, vs, fs);
        }
        else if (s_Dump && *s_Dump)
        {
            std::ofstream(std::string(s_Dump) + "/prog" + std::to_string(p.Id) + ".vs.wgsl") << vs;
            std::ofstream(std::string(s_Dump) + "/prog" + std::to_string(p.Id) + ".fs.wgsl") << fs;
        }
        p.VS = MakeModule(vs, "vertex", p.Log);
        p.FS = MakeModule(fs, "fragment", p.Log);
        if (!p.VS || !p.FS)
            return false;

        p.Uniforms = std::move(rp.Uniforms);
        p.UniformByName = std::move(rp.UniformByName);
        p.Samplers = std::move(rp.Samplers);
        p.Blocks = std::move(rp.Blocks);
        p.Inputs = std::move(rp.Inputs);
        p.ColorOutputs = rp.ColorOutputs;
        p.ColorOutputMask = rp.ColorOutputMask;
        p.GlobalsRegister = rp.GlobalsRegister;
        p.GlobalsSize = rp.GlobalsSize;
        p.Globals = std::move(rp.Globals);
        p.GlobalsDirty = true;

        for (auto& oc : p.OutputComponents)
            oc = 4;
        ScanOutputs(fs, p.OutputComponents);
        for (int i = 0; i < 8; ++i)
            if (narrowOutputs[i])
                p.OutputComponents[i] = narrowOutputs[i];
        // Doku yuvaları: hangi aşamada kullanıldıkları ve türleri (WGSL'den), GL doku birimleri (yansıtmadan)
        const auto vsTex = ScanTextures(vs), fsTex = ScanTextures(fs);
        std::memset(p.BlockVisibility, 0, sizeof(p.BlockVisibility));
        auto usedIn = [](const std::string& wgsl, uint32_t binding) {
            const std::string tag = "@binding(" + std::to_string(binding) + ")";
            const std::string tagU = "@binding(" + std::to_string(binding) + "u)";
            return wgsl.find(tag) != std::string::npos || wgsl.find(tagU) != std::string::npos;
        };
        for (const auto& b : p.Blocks)
            if (b.Register < 64)
                p.BlockVisibility[b.Register] = (usedIn(vs, b.Register) ? WGPUShaderStage_Vertex : 0) | (usedIn(fs, b.Register) ? WGPUShaderStage_Fragment : 0);
        if (p.GlobalsRegister >= 0 && p.GlobalsRegister < 64)
            p.BlockVisibility[p.GlobalsRegister] = (usedIn(vs, (uint32_t)p.GlobalsRegister) ? WGPUShaderStage_Vertex : 0) |
                                                   (usedIn(fs, (uint32_t)p.GlobalsRegister) ? WGPUShaderStage_Fragment : 0);
        for (size_t si = 0; si < p.Samplers.size(); ++si)
        {
            const auto& s = p.Samplers[si];
            for (uint32_t e = 0; e < std::max(1u, s.Count); ++e)
            {
                TextureSlot slot;
                slot.Binding = kTextureBase + s.Register + e;
                slot.Target = s.Target;
                slot.Shadow = s.Shadow;
                slot.Integer = s.Integer;
                slot.ArrayElement = (int)e;
                slot.SamplerIndex = (int)si;
                slot.Unit = e < s.Units.size() ? s.Units[e] : 0;
                for (const auto* list : { &vsTex, &fsTex })
                    for (const auto& t : *list)
                        if (t.Binding == slot.Binding)
                        {
                            slot.Shadow = slot.Shadow || t.Depth;
                            slot.Integer = slot.Integer || t.Integer;
                            slot.Multisampled = t.Multisampled;
                        }
                uint32_t vis = 0;
                for (const auto& t : vsTex)
                    if (t.Binding == slot.Binding)
                        vis |= WGPUShaderStage_Vertex;
                for (const auto& t : fsTex)
                    if (t.Binding == slot.Binding)
                        vis |= WGPUShaderStage_Fragment;
                if (!vis)
                    continue;   // eniyileştirmeyle düşen doku
                slot.Visibility = (WGPUShaderStage)vis;
                const uint32_t samplerBinding = s.SharedSampler ? kTextureBase + s.SamplerRegister : kSamplerOffset + slot.Binding;
                if (usedIn(vs, samplerBinding) || usedIn(fs, samplerBinding))
                    slot.SamplerBinding = (int)samplerBinding;
                p.Textures.push_back(slot);
            }
        }
        p.Linked = true;
        return true;
    }

    UniformSlot* FindUniform(Program& p, GLint location)
    {
        if (location < 0 || location >= (GLint)p.Uniforms.size())
            return nullptr;
        return &p.Uniforms[(size_t)location];
    }
}

#endif
