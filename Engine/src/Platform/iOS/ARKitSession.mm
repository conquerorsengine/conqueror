// ARCore API'sinin (XR/ARCore.h) iOS / iPadOS arka ucu: Apple ARKit (256). Unity AR Foundation'ın ARKit sağlayıcısı ve Unreal'ın
// Apple ARKit eklentisi gibi aynı AR işlevleri: dünya izleme, yatay / dikey düzlem algılama, isabet testi, cihaz kamerasının pozu ve
// görüş açısı, kamera görüntüsü sahnenin arka planı. Motor Metal'de MtlGL çeviri katmanıyla GL çağırır: kamera görüntüsü CPU'da
// (YCbCr 4:2:0 tam aralık, BT.601 -> RGB) ARKit'in ekran dönüşümüyle döndürülüp kırpılarak bir GL dokusuna yazılır (ARCore'daki gibi
// ekranın en-boy oranında). Yalnız iOS derlemesinde (Platform/iOS/*.mm) derlenir
#include "XR/ARKitBridge.h"

#include "Core/Logging/Log.h"
#include "Platform/iOS/iOSPlatform.h"
#include "Platform/iOS/ARKitImage.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#import <ARKit/ARKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Conqueror::ARKitBridge
{
    namespace
    {
        ARSession* s_Session = nil;
        bool s_Running = false;
        double s_LastTimestamp = -1.0;
        uint64_t s_Frames = 0;
        bool s_CamValid = false;
        glm::vec3 s_CamPos{ 0.0f };
        glm::quat s_CamRot{ 1.0f, 0.0f, 0.0f, 0.0f };
        float s_FovY = 0.0f;
        GLuint s_Texture = 0;
        int s_TexW = 0, s_TexH = 0;
        bool s_HasImage = false;
        std::vector<uint8_t> s_Rgba;

        glm::mat4 ToGlm(const simd_float4x4& m)
        {
            glm::mat4 r(1.0f);
            for (int c = 0; c < 4; ++c)
                r[c] = glm::vec4(m.columns[c][0], m.columns[c][1], m.columns[c][2], m.columns[c][3]);
            return r;
        }

        uint64_t IdOf(NSUUID* uuid)
        {
            uuid_t bytes;
            [uuid getUUIDBytes:bytes];
            uint64_t id = 0;
            std::memcpy(&id, bytes, sizeof(id));
            return id ? id : 1;
        }

        UIInterfaceOrientation Orientation()
        {
            if (@available(iOS 13.0, *))
            {
                for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
                    if ([scene isKindOfClass:[UIWindowScene class]])
                        return ((UIWindowScene*)scene).interfaceOrientation;
            }
            return UIInterfaceOrientationPortrait;
        }

        // Ekranın nokta cinsinden boyutu (yöne göre) ve piksel ölçeği
        CGSize ViewportPoints()
        {
            return [UIScreen mainScreen].bounds.size;
        }

        // Kamera görüntüsü -> RGBA8 (ekran boyutunun en uzun kenarı en çok 1280 piksel), ARKit'in ekran dönüşümüyle döndürülmüş ve
        // kırpılmış; GL dokusu alttan başladığı için satırlar ters sırada yazılır
        void CopyCameraImage(ARFrame* frame, UIInterfaceOrientation orientation, CGSize viewport)
        {
            CVPixelBufferRef pb = frame.capturedImage;
            if (!pb || CVPixelBufferGetPlaneCount(pb) < 2 || viewport.width <= 0.0 || viewport.height <= 0.0)
                return;
            const float scale = std::max(1.0f, Conqueror::iOS::ContentScale());
            float w = (float)viewport.width * scale, h = (float)viewport.height * scale;
            const float shrink = std::min(1.0f, 1280.0f / std::max(w, h));
            const int W = std::max(1, (int)std::lround(w * shrink)), H = std::max(1, (int)std::lround(h * shrink));
            // Görünümün normalleştirilmiş noktası -> görüntünün normalleştirilmiş noktası (ikisi de sol üst köken)
            const CGAffineTransform toImage = CGAffineTransformInvert([frame displayTransformForOrientation:orientation viewportSize:viewport]);

            CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            const int yw = (int)CVPixelBufferGetWidthOfPlane(pb, 0), yh = (int)CVPixelBufferGetHeightOfPlane(pb, 0);
            const int cw = (int)CVPixelBufferGetWidthOfPlane(pb, 1), ch = (int)CVPixelBufferGetHeightOfPlane(pb, 1);
            const uint8_t* yPlane = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
            const uint8_t* cPlane = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
            const size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0), cStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
            if (!yPlane || !cPlane || yw <= 0 || yh <= 0 || cw <= 0 || ch <= 0)
            {
                CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
                return;
            }
            s_Rgba.resize((size_t)W * (size_t)H * 4);
            uint8_t* out = s_Rgba.data();
            ARKitImage::Planes planes;
            planes.Y = yPlane;
            planes.YWidth = yw;
            planes.YHeight = yh;
            planes.YStride = yStride;
            planes.CbCr = cPlane;
            planes.CWidth = cw;
            planes.CHeight = ch;
            planes.CStride = cStride;
            const ARKitImage::Affine t{ toImage.a, toImage.b, toImage.c, toImage.d, toImage.tx, toImage.ty };
            dispatch_apply((size_t)H, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t row) {
                ARKitImage::ConvertRow(planes, t, W, H, (int)row, out);
            });
            CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

            GLint prevTexture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
            if (!s_Texture)
                glGenTextures(1, &s_Texture);
            glBindTexture(GL_TEXTURE_2D, s_Texture);
            if (W != s_TexW || H != s_TexH)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                s_TexW = W;
                s_TexH = H;
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, s_Rgba.data());
            glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture);
            s_HasImage = true;
        }

        void ReadPlanes(ARFrame* frame, std::vector<ARCore::Plane>& planes)
        {
            planes.clear();
            for (ARAnchor* anchor in frame.anchors)
            {
                if (![anchor isKindOfClass:[ARPlaneAnchor class]])
                    continue;
                ARPlaneAnchor* pa = (ARPlaneAnchor*)anchor;
                ARCore::Plane p;
                p.Id = IdOf(pa.identifier);
                // Çapanın yerel +Y ekseni düzlemin normali (ARCore'daki merkez pozu gibi); merkez çapanın yerel uzayında
                const glm::mat4 m = ToGlm(pa.transform);
                const simd_float3 center = pa.center;
                p.Position = glm::vec3(m * glm::vec4(center.x, center.y, center.z, 1.0f));
                p.Rotation = glm::normalize(glm::quat_cast(glm::mat3(m)));
                if (@available(iOS 16.0, *))
                {
                    p.ExtentX = pa.planeExtent.width;
                    p.ExtentZ = pa.planeExtent.height;
                }
                else
                {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    p.ExtentX = pa.extent.x;
                    p.ExtentZ = pa.extent.z;
#pragma clang diagnostic pop
                }
                p.Type = 0;
                if (pa.alignment == ARPlaneAnchorAlignmentVertical)
                    p.Type = 2;
                else if (ARPlaneAnchor.classificationSupported && pa.classification == ARPlaneClassificationCeiling)
                    p.Type = 1;
                // Sınır: çapanın yerel XZ'si, merkeze göre (ARCore'un çokgeni gibi)
                ARPlaneGeometry* geo = pa.geometry;
                for (NSUInteger i = 0; geo && i < geo.boundaryVertexCount; ++i)
                {
                    const simd_float3 v = geo.boundaryVertices[i];
                    p.Polygon.emplace_back(v.x - center.x, v.z - center.z);
                }
                planes.push_back(std::move(p));
            }
        }
    }

    bool Start(std::string& state, std::string& error)
    {
        Stop();
        if (![ARWorldTrackingConfiguration isSupported])
        {
            state = "Unavailable";
            error = "This device does not support ARKit world tracking (iPhone 6s / iPad 5th generation or newer, A9 chip)";
            return false;
        }
        const AVAuthorizationStatus camera = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if (camera == AVAuthorizationStatusDenied || camera == AVAuthorizationStatusRestricted)
        {
            state = "Unavailable";
            error = "Camera access is denied (Settings > Privacy & Security > Camera)";
            return false;
        }
        ARWorldTrackingConfiguration* config = [ARWorldTrackingConfiguration new];
        config.planeDetection = ARPlaneDetectionHorizontal | ARPlaneDetectionVertical;
        config.lightEstimationEnabled = YES;
        s_Session = [ARSession new];
        // Kamera izni ilk açılışta iOS tarafından sorulur (Info.plist NSCameraUsageDescription)
        [s_Session runWithConfiguration:config];
        s_Running = true;
        s_LastTimestamp = -1.0;
        state = camera == AVAuthorizationStatusNotDetermined ? "Waiting for camera permission" : "Initializing";
        error.clear();
        CQ_CORE_INFO("[ARKit] world tracking session started");
        return true;
    }

    void Stop()
    {
        if (s_Session)
            [s_Session pause];
        s_Session = nil;
        s_Running = false;
        s_CamValid = false;
        s_HasImage = false;
        if (s_Texture)
        {
            glDeleteTextures(1, &s_Texture);
            s_Texture = 0;
            s_TexW = s_TexH = 0;
        }
    }

    void Update(std::string& state, std::string& error, std::vector<ARCore::Plane>& planes)
    {
        if (!s_Running || !s_Session)
            return;
        @autoreleasepool
        {
            ARFrame* frame = s_Session.currentFrame;
            if (!frame)
            {
                if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] == AVAuthorizationStatusDenied)
                {
                    state = "Unavailable";
                    error = "Camera access is denied (Settings > Privacy & Security > Camera)";
                }
                return;
            }
            if (frame.timestamp == s_LastTimestamp)
                return;
            s_LastTimestamp = frame.timestamp;
            ++s_Frames;

            ARCamera* camera = frame.camera;
            switch (camera.trackingState)
            {
                case ARTrackingStateNormal: state = "Tracking"; break;
                case ARTrackingStateLimited: state = "Limited"; break;
                default: state = "Initializing"; break;
            }
            const UIInterfaceOrientation orientation = Orientation();
            const CGSize viewport = ViewportPoints();
            s_CamValid = camera.trackingState != ARTrackingStateNotAvailable;
            if (s_CamValid)
            {
                // Ekran yönündeki görüş matrisinin tersi: kameranın dünya dönüşümü (ARKit: sağ el, +Y yukarı, kamera -Z'ye bakar)
                const glm::mat4 world = glm::inverse(ToGlm([camera viewMatrixForOrientation:orientation]));
                s_CamPos = glm::vec3(world[3]);
                s_CamRot = glm::normalize(glm::quat_cast(glm::mat3(world)));
                const simd_float4x4 proj = [camera projectionMatrixForOrientation:orientation viewportSize:viewport zNear:0.05 zFar:1000.0];
                const float f = proj.columns[1][1];
                s_FovY = f > 0.0f ? glm::degrees(2.0f * std::atan(1.0f / f)) : 0.0f;
            }
            ReadPlanes(frame, planes);
            CopyCameraImage(frame, orientation, viewport);
        }
    }

    bool IsRunning() { return s_Running; }

    bool CameraPose(glm::vec3& position, glm::quat& rotation)
    {
        if (!s_Running || !s_CamValid)
            return false;
        position = s_CamPos;
        rotation = s_CamRot;
        return true;
    }

    float CameraFovY() { return s_CamValid ? s_FovY : 0.0f; }

    bool Raycast(float pixelX, float pixelY, ARCore::HitResult& out)
    {
        if (!s_Running || !s_Session)
            return false;
        @autoreleasepool
        {
            ARFrame* frame = s_Session.currentFrame;
            if (!frame)
                return false;
            const CGSize viewport = ViewportPoints();
            const float scale = std::max(1.0f, Conqueror::iOS::ContentScale());
            if (viewport.width <= 0.0 || viewport.height <= 0.0)
                return false;
            // Ekran pikseli (sol üst köken) -> görünümün normalleştirilmiş noktası -> görüntünün normalleştirilmiş noktası
            const CGPoint view = CGPointMake(pixelX / (viewport.width * scale), pixelY / (viewport.height * scale));
            const CGAffineTransform toImage = CGAffineTransformInvert([frame displayTransformForOrientation:Orientation() viewportSize:viewport]);
            const CGPoint image = CGPointApplyAffineTransform(view, toImage);
            ARRaycastResult* hit = nil;
            // Önce algılanmış düzlemin kendisi (ARCore'un düzlem çokgeni testi gibi), yoksa tahmini düzlem
            for (int pass = 0; pass < 2 && !hit; ++pass)
            {
                ARRaycastQuery* query = [frame raycastQueryFromPoint:image
                                                      allowingTarget:pass == 0 ? ARRaycastTargetExistingPlaneGeometry : ARRaycastTargetEstimatedPlane
                                                           alignment:ARRaycastTargetAlignmentAny];
                if (!query)
                    continue;
                NSArray<ARRaycastResult*>* results = [s_Session raycast:query];
                if (results.count > 0)
                    hit = results.firstObject;
            }
            if (!hit)
                return false;
            const glm::mat4 m = ToGlm(hit.worldTransform);
            out.Point = glm::vec3(m[3]);
            out.Normal = glm::normalize(glm::vec3(m[1]));
            out.PlaneId = hit.anchor ? IdOf(hit.anchor.identifier) : 0;
            out.Distance = s_CamValid ? glm::length(out.Point - s_CamPos) : 0.0f;
            return true;
        }
    }

    uint32_t BackgroundTexture() { return (s_Running && s_HasImage) ? s_Texture : 0; }
    uint64_t FramesUpdated() { return s_Frames; }
}
