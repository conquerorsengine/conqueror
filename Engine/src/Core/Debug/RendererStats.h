#pragma once

#include "Core/Base/Base.h"
#include <cstdint>

namespace Conqueror
{
    struct CQ_API RendererStats
    {
        uint32_t DrawCalls = 0;
        uint32_t Vertices = 0;
        uint32_t Indices = 0;
        uint32_t Triangles = 0;
        
        // 2D Stats
        uint32_t QuadCount = 0;
        uint32_t SpriteCount = 0;
        uint32_t TextCount = 0;
        
        // 3D Stats
        uint32_t MeshCount = 0;
        uint32_t LightCount = 0;
        uint32_t ShadowDrawCalls = 0;
        uint32_t ShadowCascades = 0;
        uint32_t TransparentDraws = 0;
        uint32_t PostProcessPasses = 0;
        uint32_t CulledObjects = 0;

        // GPU zamanlamaları (ms, timer query ile ölçülür)
        float GPUFrameMs = 0.0f;
        float GPUShadowMs = 0.0f;
        float GPUPostMs = 0.0f;
        
        // Memory (MB)
        float TextureMemory = 0.0f;
        float MeshMemory = 0.0f;

        // Debug Stats
        uint32_t DebugLineCount = 0;
        uint32_t DebugLineVertices = 0;
        uint32_t DebugSolidTriangles = 0;
        uint32_t DebugSolidVertices = 0;
        uint32_t DebugTextEntries = 0;
        uint32_t DebugTimedEntries = 0;
        
        void Reset()
        {
            DrawCalls = 0;
            Vertices = 0;
            Indices = 0;
            Triangles = 0;
            QuadCount = 0;
            SpriteCount = 0;
            TextCount = 0;
            MeshCount = 0;
            LightCount = 0;
            ShadowDrawCalls = 0;
            ShadowCascades = 0;
            TransparentDraws = 0;
            PostProcessPasses = 0;
            CulledObjects = 0;
            DebugLineCount = 0;
            DebugLineVertices = 0;
            DebugSolidTriangles = 0;
            DebugSolidVertices = 0;
            DebugTextEntries = 0;
            DebugTimedEntries = 0;
            // TextureMemory ve MeshMemory sıfırlanmıyor - persistent
        }
    };
}
