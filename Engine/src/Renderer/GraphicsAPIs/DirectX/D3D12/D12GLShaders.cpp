#ifdef CQ_D3D12_ENABLED

// GLSL -> HLSL -> DXBC çevirisi ve yansıtma ortak katmandadır (DirectX/Common/DxShaderTranslate.cpp). D3D12'de
// gölgelendirici nesnesi yoktur: bayt kodu boru hattı durumu (PSO) oluşturulurken kullanılır.

#include "D12GL.h"
#include "Core/Logging/Log.h"

namespace Conqueror::D12GL
{
    void DestroyProgram(Program& p)
    {
        p.VS.clear();
        p.PS.clear();
        p.GlobalsAddress = 0;
        p.GlobalsFrame = 0;
        // Bu programın PSO'ları anahtarda program kimliği taşır; yeni bağlamada yeni kimlik alınır
    }

    bool LinkProgram(Program& p)
    {
        auto& c = Ctx();
        DestroyProgram(p);
        p.Linked = false;
        p.Log.clear();

        std::vector<DXCommon::ShaderSource> shaders;
        for (uint32_t name : p.Shaders)
            if (auto it = c.ShaderObjects.find(name); it != c.ShaderObjects.end())
                shaders.push_back({ it->second->Type, &it->second->Source });
        DXCommon::ShaderLimits limits;
        limits.ConstantBuffers = DX12::kRootCBVs;
        limits.Resources = DX12::kMaxSRVs;
        limits.Samplers = DX12::kMaxSamplers;
        limits.ApiName = "Direct3D 12";
        DXCommon::TranslatedProgram tp;
        if (!DXCommon::TranslateProgram(shaders, c.NextProgramId, limits, tp, p.Log))
            return false;

        p.Uniforms = std::move(tp.Uniforms);
        p.UniformByName = std::move(tp.UniformByName);
        p.Samplers = std::move(tp.Samplers);
        p.Blocks = std::move(tp.Blocks);
        p.Inputs = std::move(tp.Inputs);
        p.ColorOutputs = tp.ColorOutputs;
        p.ColorOutputMask = tp.ColorOutputMask;
        p.GlobalsRegister = tp.GlobalsRegister;
        p.GlobalsSize = tp.GlobalsSize;
        p.Globals = std::move(tp.Globals);
        p.GlobalsDirty = true;
        p.VS = std::move(tp.VS);
        p.PS = std::move(tp.PS);
        p.Id = c.NextProgramId++;
        p.Linked = !p.VS.empty() && !p.PS.empty();
        if (!p.Linked)
            p.Log = "Direct3D 12 shader compilation produced no bytecode";
        return p.Linked;
    }

    UniformSlot* FindUniform(Program& p, GLint location)
    {
        if (location < 0 || location >= (GLint)p.Uniforms.size())
            return nullptr;
        return &p.Uniforms[(size_t)location];
    }
}

#endif
