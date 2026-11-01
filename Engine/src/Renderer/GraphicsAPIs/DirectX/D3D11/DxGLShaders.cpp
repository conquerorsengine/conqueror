#ifdef CQ_D3D11_ENABLED

// GLSL -> HLSL -> DXBC çevirisi ve yansıtma ortak katmandadır (DirectX/Common/DxShaderTranslate.cpp);
// burada yalnızca D3D11 gölgelendirici nesneleri ve globaller sabit arabelleği oluşturulur.

#include "DxGL.h"
#include "Core/Logging/Log.h"

namespace Conqueror::DxGL
{
    using DX::Core;

    void DestroyProgram(Program& p)
    {
        SafeRelease(p.VS);
        SafeRelease(p.PS);
        SafeRelease(p.GlobalsBuffer);
        for (auto& [k, l] : p.InputLayouts)
            if (l) l->Release();
        p.InputLayouts.clear();
        p.VSBytecode.clear();
    }

    bool LinkProgram(Program& p)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        DestroyProgram(p);
        p.Linked = false;
        p.Log.clear();

        std::vector<DXCommon::ShaderSource> shaders;
        for (uint32_t name : p.Shaders)
            if (auto it = c.ShaderObjects.find(name); it != c.ShaderObjects.end())
                shaders.push_back({ it->second->Type, &it->second->Source });
        DXCommon::ShaderLimits limits;
        limits.ConstantBuffers = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        limits.Resources = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        limits.Samplers = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
        limits.ApiName = "Direct3D 11";
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

        core.Device()->CreateVertexShader(tp.VS.data(), tp.VS.size(), nullptr, &p.VS);
        core.Device()->CreatePixelShader(tp.PS.data(), tp.PS.size(), nullptr, &p.PS);
        p.VSBytecode = std::move(tp.VS);
        if (p.GlobalsRegister >= 0)
        {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = (UINT)p.Globals.size();
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            core.Device()->CreateBuffer(&bd, nullptr, &p.GlobalsBuffer);
        }
        p.Id = c.NextProgramId++;
        p.Linked = p.VS && p.PS && (p.GlobalsRegister < 0 || p.GlobalsBuffer);
        if (!p.Linked)
            p.Log = "Direct3D 11 shader object creation failed";
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
