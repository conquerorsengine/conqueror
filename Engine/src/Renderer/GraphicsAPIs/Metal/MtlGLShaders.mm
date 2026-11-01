#ifdef CQ_METAL_ENABLED

// Yansıtma ortak (GLEmu/GLReflect.cpp); burada SPIR-V -> MSL (SPIRV-Cross) -> MTLLibrary (çalışma anında derleme).
// Dizinler: sabit arabellek yazmacı b -> [[buffer(16 + b)]], doku / örnekleyici yazmacı -> [[texture(r)]] / [[sampler(r)]].

#include "MtlGL.h"
#include "Core/Logging/Log.h"

#include <spirv_msl.hpp>

#include <cstdlib>
#include <fstream>

namespace Conqueror::MtlGL
{
    using MTL::Core;

    namespace
    {
        id<MTLFunction> CompileStage(std::vector<uint32_t> words, bool vertex, uint64_t id, std::string& log)
        {
            std::string msl;
            std::string entry = "main0";
            try
            {
                spirv_cross::CompilerMSL comp(std::move(words));
                spirv_cross::CompilerMSL::Options mo;
                mo.platform = spirv_cross::CompilerMSL::Options::macOS;
                mo.set_msl_version(2, 1);
                mo.enable_decoration_binding = true;   // SPIR-V Binding = Metal dizini
                comp.set_msl_options(mo);
                spirv_cross::CompilerGLSL::Options go = comp.get_common_options();
                go.vertex.flip_vert_y = true;          // GL satır düzeni korunur (görüntüler GL yönünde tutulur)
                go.vertex.fixup_clipspace = true;      // z: [-w,w] -> [0,w]
                comp.set_common_options(go);
                // Sabit arabellekler köşe öznitelik arabelleklerinden (0..15) sonra
                spirv_cross::ShaderResources res = comp.get_shader_resources();
                for (const auto& ub : res.uniform_buffers)
                {
                    const uint32_t b = comp.get_decoration(ub.id, spv::DecorationBinding);
                    comp.set_decoration(ub.id, spv::DecorationBinding, MTL::kConstantBufferBase + b);
                }
                msl = comp.compile();
                for (const auto& ep : comp.get_entry_points_and_stages())
                    entry = comp.get_cleansed_entry_point_name(ep.name, ep.execution_model);
            }
            catch (const std::exception& e)
            {
                log = std::string("SPIRV-Cross MSL: ") + e.what();
                return nil;
            }
            static const char* s_Dump = std::getenv("CQ_MTLGL_DUMP");
            if (s_Dump && *s_Dump)
                std::ofstream(std::string(s_Dump) + "/prog" + std::to_string(id) + (vertex ? ".vs.metal" : ".fs.metal")) << msl;
            NSError* err = nil;
            MTLCompileOptions* opts = [MTLCompileOptions new];
            id<MTLLibrary> lib = [Core::Get().Device() newLibraryWithSource:[NSString stringWithUTF8String:msl.c_str()] options:opts error:&err];
            if (!lib)
            {
                log = std::string(vertex ? "[vertex] " : "[fragment] ") + (err ? [[err localizedDescription] UTF8String] : "MSL compile failed");
                return nil;
            }
            id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:entry.c_str()]];
            if (!fn)
                log = "MSL entry point '" + entry + "' not found";
            return fn;
        }
    }

    void DestroyProgram(Program& p)
    {
        p.VS = nil;
        p.PS = nil;
        p.GlobalsBuffer = nil;
        p.GlobalsFrame = 0;
    }

    bool LinkProgram(Program& p)
    {
        auto& c = Ctx();
        DestroyProgram(p);
        p.Linked = false;
        p.Log.clear();

        std::vector<GLEmu::ShaderSource> shaders;
        for (uint32_t name : p.Shaders)
            if (auto it = c.ShaderObjects.find(name); it != c.ShaderObjects.end())
                shaders.push_back({ it->second->Type, &it->second->Source });
        GLEmu::ResourceLimits limits;
        limits.ConstantBuffers = MTL::kMaxConstantBuffers;
        limits.Resources = MTL::kMaxTextures;
        limits.Samplers = MTL::kMaxSamplers;
        limits.ApiName = "Metal";
        GLEmu::ReflectedProgram rp;
        if (!GLEmu::ReflectProgram(shaders, limits, rp, p.Log))
            return false;
        const uint64_t id = c.NextProgramId;
        p.VS = CompileStage(std::move(rp.VertexSpirv), true, id, p.Log);
        if (!p.VS)
            return false;
        p.PS = CompileStage(std::move(rp.FragmentSpirv), false, id, p.Log);
        if (!p.PS)
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
        p.Id = c.NextProgramId++;
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
