#include "PixelPerfect.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace PixelPerfect
    {
        View Resolve(Scene& scene, uint32_t screenWidth, uint32_t screenHeight)
        {
            View v;
            v.RenderWidth = std::max(screenWidth, 1u);
            v.RenderHeight = std::max(screenHeight, 1u);
            v.Output = glm::ivec4(0, 0, (int)v.RenderWidth, (int)v.RenderHeight);
            Entity cam = scene.GetPrimaryCameraEntity();
            if (!cam || !cam.HasComponent<PixelPerfectCameraComponent>() || !cam.HasComponent<CameraComponent>())
                return v;
            auto& cc = cam.GetComponent<CameraComponent>();
            if (cc.Camera.GetProjectionType() != SceneCamera::ProjectionType::Orthographic)
                return v;
            auto& pp = cam.GetComponent<PixelPerfectCameraComponent>();
            const int sw = (int)v.RenderWidth, sh = (int)v.RenderHeight;
            const int refW = std::max(1, (int)std::round(pp.ReferenceResolution.x)), refH = std::max(1, (int)std::round(pp.ReferenceResolution.y));
            const int ppu = std::max(1, pp.AssetsPPU);
            const int zoom = std::max(1, std::min(sw / refW, sh / refH));
            pp.CurrentZoom = zoom;
            v.Active = true;
            v.Zoom = zoom;

            int outW = sw, outH = sh;
            if (pp.UpscaleRenderTexture)
            {
                const int rw = pp.CropFrameX ? refW : std::max(1, sw / zoom);
                const int rh = pp.CropFrameY ? refH : std::max(1, sh / zoom);
                v.RenderWidth = (uint32_t)rw;
                v.RenderHeight = (uint32_t)rh;
                outW = rw * zoom;
                outH = rh * zoom;
                if (pp.StretchFill && pp.CropFrameX && pp.CropFrameY)
                {
                    const float fit = std::min((float)sw / (float)rw, (float)sh / (float)rh);
                    outW = (int)std::floor((float)rw * fit);
                    outH = (int)std::floor((float)rh * fit);
                }
                v.PointSample = true;
                cc.Camera.SetOrthographicSize((float)rh / (float)ppu);
            }
            else
            {
                outW = pp.CropFrameX ? std::min(sw, refW * zoom) : sw;
                outH = pp.CropFrameY ? std::min(sh, refH * zoom) : sh;
                v.RenderWidth = (uint32_t)outW;
                v.RenderHeight = (uint32_t)outH;
                cc.Camera.SetOrthographicSize((float)outH / (float)(zoom * ppu));
            }
            v.Output = glm::ivec4((sw - outW) / 2, (sh - outH) / 2, outW, outH);
            if (!cc.FixedAspectRatio)
                cc.Camera.SetViewportSize(v.RenderWidth, v.RenderHeight);
            return v;
        }

        float SnapGrid(Scene&, Entity camera)
        {
            if (!camera || !camera.HasComponent<PixelPerfectCameraComponent>())
                return 0.0f;
            const auto& pp = camera.GetComponent<PixelPerfectCameraComponent>();
            if (!pp.PixelSnapping && !pp.UpscaleRenderTexture)
                return 0.0f;
            // Büyütmede bir doku pikseli = bir hedef pikseli; aksi halde zoom kadar ekran pikseli
            return (float)std::max(1, pp.AssetsPPU) * (pp.UpscaleRenderTexture ? 1.0f : (float)std::max(1, pp.CurrentZoom));
        }
    }

    void RegisterPixelPerfectComponents()
    {
        using P = PixelPerfectCameraComponent;
        ComponentDef<P>("Pixel Perfect Camera", "PixelPerfectCameraComponent", "Rendering", ICON_FA_BORDER_ALL)
            .Describe("Keeps pixel art crisp: integer zoom, optional low-resolution render with nearest-neighbour upscale, crop frame and pixel snapping. Needs an orthographic camera.")
            .Field("AssetsPPU", "Assets Pixels Per Unit", &P::AssetsPPU).Range(1.0f, 10000.0f, 1.0f)
            .Field("ReferenceResolution", "Reference Resolution", &P::ReferenceResolution).Range(1.0f, 8192.0f, 1.0f)
            .Field("UpscaleRenderTexture", "Upscale Render Texture", &P::UpscaleRenderTexture)
            .Field("PixelSnapping", "Pixel Snapping", &P::PixelSnapping)
            .Field("CropFrameX", "Crop Frame X", &P::CropFrameX)
            .Field("CropFrameY", "Crop Frame Y", &P::CropFrameY)
            .Field("StretchFill", "Stretch Fill", &P::StretchFill).VisibleIf([](const P& p) { return p.CropFrameX && p.CropFrameY && p.UpscaleRenderTexture; })
            .Field("CurrentZoom", "Current Zoom", &P::CurrentZoom).ReadOnly();
    }
}
