// GL çeviri katmanlarının ortak gölgelendirici yansıtması (DxGL, D12GL, MtlGL): GLSL -> SPIR-V (glslang, gevşek Vulkan
// kuralları: gevşek uniform'lar CQ_Globals bloğunda) -> kaynak yazmaçları + uniform yuvaları. Hedef dil (HLSL / MSL)
// arka uçta üretilir; burada bağlama numaraları son yazmaçlara taşınmış SPIR-V döner.

#include "GLReflect.h"
#include "GLShaderUtil.h"
#include "Renderer/ShaderCompiler/ShaderCompiler.h"

#include <spirv_cross.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <set>

namespace Conqueror::GLEmu
{
    namespace
    {
        // Aşama başına 16 örnekleyici: büyük örnekleyici dizileri ayrı doku dizisi + tek örnekleyiciye çevrilir
        constexpr uint32_t kMaxCombinedArray = 8;

        std::string PatchSource(const std::string& src)
        {
            std::string s = src;
            std::regex versionRe(R"(#version[^\n]*)");
            if (std::regex_search(s, versionRe))
                s = std::regex_replace(s, versionRe, "#version 450", std::regex_constants::format_first_only);
            else
                s = "#version 450\n" + s;
            s = std::regex_replace(s, std::regex(R"(\bgl_VertexID\b)"), "gl_VertexIndex");
            s = std::regex_replace(s, std::regex(R"(\bgl_InstanceID\b)"), "gl_InstanceIndex");
            return GLEmu::SplitLargeSamplerArrays(s, kMaxCombinedArray);
        }

        ShaderStage StageOf(GLenum t) { return t == GL_FRAGMENT_SHADER ? ShaderStage::Fragment : ShaderStage::Vertex; }

        GLenum GLTypeOf(const spirv_cross::SPIRType& t)
        {
            using BT = spirv_cross::SPIRType::BaseType;
            if (t.columns > 1)
            {
                if (t.columns == 2) return GL_FLOAT_MAT2;
                if (t.columns == 3) return t.vecsize == 3 ? GL_FLOAT_MAT3 : GL_FLOAT_MAT3x4;
                return t.vecsize == 4 ? GL_FLOAT_MAT4 : GL_FLOAT_MAT4x3;
            }
            if (t.basetype == BT::Int)
                return t.vecsize == 1 ? GL_INT : t.vecsize == 2 ? GL_INT_VEC2 : t.vecsize == 3 ? GL_INT_VEC3 : GL_INT_VEC4;
            if (t.basetype == BT::UInt)
                return t.vecsize == 1 ? GL_UNSIGNED_INT : GL_UNSIGNED_INT_VEC4;
            if (t.basetype == BT::Boolean)
                return t.vecsize == 1 ? GL_BOOL : GL_BOOL_VEC4;
            return t.vecsize == 1 ? GL_FLOAT : t.vecsize == 2 ? GL_FLOAT_VEC2 : t.vecsize == 3 ? GL_FLOAT_VEC3 : GL_FLOAT_VEC4;
        }

        void AddSlot(ReflectedProgram& p, const std::string& name, const UniformSlot& slot)
        {
            const int loc = (int)p.Uniforms.size();
            p.Uniforms.push_back(slot);
            p.Uniforms.back().Name = name;
            if (!p.UniformByName.count(name))
                p.UniformByName[name] = loc;
        }

        void Flatten(spirv_cross::Compiler& comp, const spirv_cross::SPIRType& st, uint32_t base, const std::string& prefix, ReflectedProgram& p)
        {
            for (uint32_t i = 0; i < (uint32_t)st.member_types.size(); ++i)
            {
                const std::string name = prefix + comp.get_member_name(st.self, i);
                const uint32_t off = base + comp.type_struct_member_offset(st, i);
                const spirv_cross::SPIRType& mt = comp.get_type(st.member_types[i]);
                if (!mt.array.empty())
                {
                    const uint32_t count = std::max(1u, mt.array.back());
                    const uint32_t stride = comp.type_struct_member_array_stride(st, i);
                    const spirv_cross::SPIRType& elem = comp.get_type(mt.parent_type);
                    for (uint32_t e = 0; e < count; ++e)
                    {
                        const std::string en = name + "[" + std::to_string(e) + "]";
                        if (elem.basetype == spirv_cross::SPIRType::Struct)
                        {
                            Flatten(comp, elem, off + e * stride, en + ".", p);
                            continue;
                        }
                        UniformSlot slot;
                        slot.Offset = off + e * stride;
                        slot.Type = GLTypeOf(mt);
                        slot.MatrixStride = mt.columns > 1 ? comp.type_struct_member_matrix_stride(st, i) : 16;
                        slot.ArrayStride = stride;
                        slot.ArrayRemaining = (int)(count - e);
                        AddSlot(p, en, slot);
                        if (e == 0)
                            p.UniformByName[name] = (int)p.Uniforms.size() - 1;
                    }
                    continue;
                }
                if (mt.basetype == spirv_cross::SPIRType::Struct)
                {
                    Flatten(comp, mt, off, name + ".", p);
                    continue;
                }
                UniformSlot slot;
                slot.Offset = off;
                slot.Type = GLTypeOf(mt);
                slot.MatrixStride = mt.columns > 1 ? comp.type_struct_member_matrix_stride(st, i) : 16;
                AddSlot(p, name, slot);
            }
        }

        // Bağlama numaraları (D3D yazmaçları) + tek küme + uniform Location süslemelerini at
        void PatchBindings(std::vector<uint32_t>& spv, const std::map<uint32_t, uint32_t>& idToBinding)
        {
            if (spv.size() < 5)
                return;
            std::set<uint32_t> uniformVars;
            for (size_t i = 5; i < spv.size();)
            {
                const uint32_t op = spv[i] & 0xFFFF, count = spv[i] >> 16;
                if (count == 0)
                    break;
                if (op == 59 && count >= 4 && i + 3 < spv.size() && (spv[i + 3] == 0 || spv[i + 3] == 2 || spv[i + 3] == 12))
                    uniformVars.insert(spv[i + 2]);
                i += count;
            }
            std::vector<uint32_t> out(spv.begin(), spv.begin() + 5);
            out.reserve(spv.size());
            for (size_t i = 5; i < spv.size();)
            {
                const uint32_t op = spv[i] & 0xFFFF, count = spv[i] >> 16;
                if (count == 0 || i + count > spv.size())
                {
                    out.insert(out.end(), spv.begin() + (std::ptrdiff_t)i, spv.end());
                    break;
                }
                if (op == 71 && count >= 3 && (spv[i + 2] == 30 || spv[i + 2] == 31) && uniformVars.count(spv[i + 1]))
                {
                    i += count;
                    continue;
                }
                const size_t at = out.size();
                out.insert(out.end(), spv.begin() + (std::ptrdiff_t)i, spv.begin() + (std::ptrdiff_t)(i + count));
                if (op == 71 && count >= 4 && out[at + 2] == 33)
                    if (auto it = idToBinding.find(out[at + 1]); it != idToBinding.end())
                        out[at + 3] = it->second;
                if (op == 71 && count >= 4 && out[at + 2] == 34)
                    out[at + 3] = 0;
                i += count;
            }
            spv.swap(out);
        }

        struct UniformInit
        {
            std::string Name, Type;
            std::vector<double> Values;
        };

        std::vector<UniformInit> ParseUniformInitializers(const std::string& src)
        {
            std::vector<UniformInit> out;
            static const std::regex s_Init(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234])\s+(\w+)\s*=\s*([^;]+);)");
            static const std::regex s_Number(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|true|false)");
            for (std::sregex_iterator m(src.begin(), src.end(), s_Init), end; m != end; ++m)
            {
                UniformInit u;
                u.Type = (*m)[1].str();
                u.Name = (*m)[2].str();
                const std::string expr = (*m)[3].str();
                for (std::sregex_iterator n(expr.begin(), expr.end(), s_Number), e2; n != e2; ++n)
                {
                    const std::string t = n->str();
                    u.Values.push_back(t == "true" ? 1.0 : t == "false" ? 0.0 : std::strtod(t.c_str(), nullptr));
                }
                if (u.Type.size() > 3 && std::isdigit((unsigned char)u.Type.back()) && !u.Values.empty() && expr.find('(') != std::string::npos)
                {
                    const std::string head = expr.substr(0, expr.find('('));
                    if (head.find_first_of("0123456789") != std::string::npos)
                        u.Values.erase(u.Values.begin());
                }
                if (!u.Values.empty())
                    out.push_back(std::move(u));
            }
            return out;
        }

        void ApplyUniformInitializers(ReflectedProgram& p, const std::vector<UniformInit>& inits)
        {
            for (const auto& u : inits)
            {
                auto it = p.UniformByName.find(u.Name);
                if (it == p.UniformByName.end())
                    continue;
                const UniformSlot& slot = p.Uniforms[(size_t)it->second];
                if (slot.Sampler >= 0)
                    continue;
                int comps = 1;
                if (u.Type.size() > 3 && std::isdigit((unsigned char)u.Type.back()))
                    comps = u.Type.back() - '0';
                const bool isFloat = u.Type == "float" || u.Type.rfind("vec", 0) == 0;
                for (int i = 0; i < comps; ++i)
                {
                    const double v = u.Values[std::min<size_t>((size_t)i, u.Values.size() - 1)];
                    const size_t off = (size_t)slot.Offset + (size_t)i * 4;
                    if (off + 4 > p.Globals.size())
                        break;
                    if (isFloat)
                    {
                        const float f = (float)v;
                        std::memcpy(p.Globals.data() + off, &f, 4);
                    }
                    else
                    {
                        const int32_t iv = (int32_t)v;
                        std::memcpy(p.Globals.data() + off, &iv, 4);
                    }
                }
            }
        }
    }

    bool ReflectProgram(const std::vector<ShaderSource>& shaders, const ResourceLimits& limits, ReflectedProgram& p, std::string& log)
    {
        p = ReflectedProgram{};
        std::map<ShaderStage, std::string> sources;
        std::map<std::string, int> explicitBindings;
        std::vector<UniformInit> inits;
        static const std::regex s_LayoutUniform(R"(layout\s*\(([^)]*)\)\s*uniform\s+(?:(?:lowp|mediump|highp)\s+)?\w+\s+(\w+))");
        static const std::regex s_Binding(R"(binding\s*=\s*(\d+))");
        for (const ShaderSource& sh : shaders)
        {
            if (!sh.Source)
                continue;
            if (sh.Type != GL_VERTEX_SHADER && sh.Type != GL_FRAGMENT_SHADER)
            {
                log = std::string("Only vertex and fragment shaders are supported by the ") + limits.ApiName + " backend";
                return false;
            }
            const std::string& src = *sh.Source;
            sources[StageOf(sh.Type)] = PatchSource(src);
            for (auto& u : ParseUniformInitializers(src))
                inits.push_back(std::move(u));
            for (std::sregex_iterator m(src.begin(), src.end(), s_LayoutUniform), end; m != end; ++m)
            {
                const std::string qualifiers = (*m)[1].str();
                std::smatch b;
                if (std::regex_search(qualifiers, b, s_Binding))
                    explicitBindings[(*m)[2].str()] = std::stoi(b[1].str());
            }
        }
        if (!sources.count(ShaderStage::Vertex) || !sources.count(ShaderStage::Fragment))
        {
            log = "Program needs a vertex and a fragment shader";
            return false;
        }
        std::map<ShaderStage, std::vector<uint32_t>> spirv;
        std::string errors;
        if (!ShaderCompiler::CompileToSPIRV(sources, "dxgl", spirv, errors, true))
        {
            log = errors;
            return false;
        }

        // ── Yansıtma ──
        struct Pending
        {
            std::string Name;
            enum Kind { Block, Globals, Combined, Image, Sampler } K = Block;
            uint32_t OrigBinding = 0, Size = 0, Count = 1;
            GLenum Target = GL_TEXTURE_2D;
            bool Shadow = false, Integer = false;
        };
        std::vector<Pending> resources;
        auto findRes = [&](const std::string& name) -> int {
            for (int i = 0; i < (int)resources.size(); ++i)
                if (resources[(size_t)i].Name == name)
                    return i;
            return -1;
        };
        std::map<ShaderStage, std::map<uint32_t, int>> stageIdToRes;
        auto imageInfo = [&](spirv_cross::Compiler& comp, const spirv_cross::SPIRType& type, Pending& r) {
            r.Count = type.array.empty() ? 1u : std::max(1u, type.array.back());
            r.Target = type.image.dim == spv::DimCube ? GL_TEXTURE_CUBE_MAP : type.image.ms ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
            r.Shadow = type.image.depth;
            const auto& sampled = comp.get_type(type.image.type);
            r.Integer = sampled.basetype == spirv_cross::SPIRType::Int || sampled.basetype == spirv_cross::SPIRType::UInt;
        };

        for (auto& [stage, words] : spirv)
        {
            spirv_cross::Compiler comp(words);
            spirv_cross::ShaderResources res = comp.get_shader_resources();
            for (const auto& ub : res.uniform_buffers)
            {
                const auto& type = comp.get_type(ub.base_type_id);
                std::string blockName = comp.get_name(ub.base_type_id);
                if (blockName.empty())
                    blockName = ub.name;
                const bool globals = blockName == "CQ_Globals";
                int idx = findRes(blockName);
                const uint32_t size = (uint32_t)comp.get_declared_struct_size(type);
                if (idx < 0)
                {
                    Pending r;
                    r.Name = blockName;
                    r.K = globals ? Pending::Globals : Pending::Block;
                    r.OrigBinding = comp.get_decoration(ub.id, spv::DecorationBinding);
                    r.Size = size;
                    resources.push_back(r);
                    idx = (int)resources.size() - 1;
                }
                else
                    resources[(size_t)idx].Size = std::max(resources[(size_t)idx].Size, size);
                stageIdToRes[stage][ub.id] = idx;
                if (globals && size >= p.GlobalsSize)
                {
                    p.Uniforms.erase(std::remove_if(p.Uniforms.begin(), p.Uniforms.end(), [](const UniformSlot& u) { return u.Sampler < 0; }), p.Uniforms.end());
                    p.UniformByName.clear();
                    Flatten(comp, type, 0, "", p);
                    p.GlobalsSize = size;
                }
            }
            auto addImage = [&](const spirv_cross::Resource& r, Pending::Kind kind) {
                int idx = findRes(r.name);
                if (idx < 0)
                {
                    Pending pr;
                    pr.Name = r.name;
                    pr.K = kind;
                    pr.OrigBinding = comp.get_decoration(r.id, spv::DecorationBinding);
                    if (kind != Pending::Sampler)
                        imageInfo(comp, comp.get_type(r.type_id), pr);
                    resources.push_back(pr);
                    idx = (int)resources.size() - 1;
                }
                stageIdToRes[stage][r.id] = idx;
            };
            for (const auto& si : res.sampled_images)
                addImage(si, Pending::Combined);
            for (const auto& si : res.separate_images)
                addImage(si, Pending::Image);
            for (const auto& si : res.separate_samplers)
                addImage(si, Pending::Sampler);
            if (stage == ShaderStage::Vertex)
            {
                for (const auto& in : res.stage_inputs)
                {
                    const auto& type = comp.get_type(in.type_id);
                    const uint32_t base = comp.get_decoration(in.id, spv::DecorationLocation);
                    const uint32_t columns = std::max(1u, type.columns);
                    const uint32_t elements = type.array.empty() ? 1u : std::max(1u, type.array[0]);
                    for (uint32_t k = 0; k < columns * elements; ++k)
                    {
                        VertexInput vi;
                        vi.Location = (int)(base + k);
                        vi.Integer = type.basetype == spirv_cross::SPIRType::Int || type.basetype == spirv_cross::SPIRType::UInt;
                        vi.Components = type.vecsize;
                        vi.Name = k == 0 ? in.name : in.name + "#" + std::to_string(k);
                        p.Inputs.push_back(vi);
                    }
                }
            }
            if (stage == ShaderStage::Fragment)
            {
                uint32_t outputs = 0, mask = 0;
                for (const auto& out : res.stage_outputs)
                {
                    const auto& type = comp.get_type(out.type_id);
                    const uint32_t loc = comp.get_decoration(out.id, spv::DecorationLocation);
                    const uint32_t count = type.array.empty() ? 1u : std::max(1u, type.array[0]);
                    for (uint32_t k = 0; k < count && loc + k < 32; ++k)
                        mask |= 1u << (loc + k);
                    outputs = std::max(outputs, loc + count);
                }
                p.ColorOutputs = std::max(1u, outputs);
                p.ColorOutputMask = mask;
            }
        }

        // ── D3D yazmaçları: b# bloklar sonra globaller; birleşik örnekleyiciler t#=s#; ayrık dokular / örnekleyiciler ardından ──
        std::vector<uint32_t> finalBinding(resources.size(), 0);
        uint32_t nextB = 0, nextTS = 0;
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < resources.size(); ++i)
            {
                auto& r = resources[i];
                if ((pass == 0 && r.K == Pending::Block) || (pass == 1 && r.K == Pending::Globals))
                {
                    finalBinding[i] = nextB;
                    if (r.K == Pending::Block)
                        p.Blocks.push_back({ r.Name, nextB, r.OrigBinding, r.Size });
                    else
                        p.GlobalsRegister = (int)nextB;
                    ++nextB;
                }
            }
        for (size_t i = 0; i < resources.size(); ++i)
            if (resources[i].K == Pending::Combined)
            {
                finalBinding[i] = nextTS;
                nextTS += resources[i].Count;
            }
        uint32_t nextT = nextTS, nextS = nextTS;
        for (size_t i = 0; i < resources.size(); ++i)
        {
            if (resources[i].K == Pending::Image)
            {
                finalBinding[i] = nextT;
                nextT += resources[i].Count;
            }
            else if (resources[i].K == Pending::Sampler)
                finalBinding[i] = nextS++;
        }
        if (nextB > limits.ConstantBuffers || nextS > limits.Samplers || nextT > limits.Resources)
        {
            log = std::string("Shader exceeds ") + limits.ApiName + " resource limits (" + std::to_string(nextB) + " cbuffers, " + std::to_string(nextT) +
                  " textures, " + std::to_string(nextS) + " samplers)";
            return false;
        }

        // Örnekleyici kayıtları + uniform konumları (glUniform1i ile birim)
        for (size_t i = 0; i < resources.size(); ++i)
        {
            const auto& r = resources[i];
            if (r.K != Pending::Combined && r.K != Pending::Image)
                continue;
            SamplerBinding sb;
            sb.Register = finalBinding[i];
            sb.Count = r.Count;
            sb.Target = r.Target;
            sb.Shadow = r.Shadow;
            sb.Integer = r.Integer;
            std::string glName = r.Name;
            if (r.K == Pending::Image)
            {
                // "<ad>_cqtex": eşleşen "<ad>_cqsmp" örnekleyicisi
                sb.SharedSampler = true;
                glName = r.Name.size() > 6 ? r.Name.substr(0, r.Name.size() - 6) : r.Name;
                const int smp = findRes(glName + "_cqsmp");
                sb.SamplerRegister = smp >= 0 ? finalBinding[(size_t)smp] : 0;
            }
            else
                sb.SamplerRegister = sb.Register;
            sb.Name = glName;
            sb.Units.assign(r.Count, 0);
            if (auto eb = explicitBindings.find(glName); eb != explicitBindings.end())
                for (uint32_t e = 0; e < r.Count; ++e)
                    sb.Units[e] = eb->second + (int)e;
            p.Samplers.push_back(sb);
            const int sIdx = (int)p.Samplers.size() - 1;
            for (uint32_t e = 0; e < r.Count; ++e)
            {
                UniformSlot slot;
                slot.Sampler = sIdx;
                slot.SamplerElement = (int)e;
                slot.Type = GL_SAMPLER_2D;
                slot.ArrayRemaining = (int)(r.Count - e);
                const std::string n = r.Count > 1 ? glName + "[" + std::to_string(e) + "]" : glName;
                AddSlot(p, n, slot);
                if (e == 0 && r.Count > 1)
                    p.UniformByName[glName] = (int)p.Uniforms.size() - 1;
            }
        }

        // Bağlama numaraları son yazmaçlara (küme 0) taşınmış SPIR-V
        for (auto& [stage, words] : spirv)
        {
            std::map<uint32_t, uint32_t> remap;
            for (const auto& [rid, resIdx] : stageIdToRes[stage])
                remap[rid] = finalBinding[(size_t)resIdx];
            PatchBindings(words, remap);
        }
        p.VertexSpirv = std::move(spirv[ShaderStage::Vertex]);
        p.FragmentSpirv = std::move(spirv[ShaderStage::Fragment]);
        p.Globals.assign((std::max<uint32_t>(p.GlobalsSize, 16) + 15) & ~15u, 0);
        ApplyUniformInitializers(p, inits);
        return true;
    }
}
