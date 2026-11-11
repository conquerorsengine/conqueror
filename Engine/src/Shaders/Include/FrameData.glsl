// Conqueror's Engine - kare başına sabitler (UBO, binding 0) ve ışık listesi (UBO, binding 1)
// C++ karşılığı: Renderer/Utilities/Renderer3D/RendererUBO.h  (düzen BİREBİR aynı olmalı)

#define CQ_MAX_CASCADES 4
#define CQ_MAX_POINT_LIGHTS 64
#define CQ_MAX_SPOT_LIGHTS 32
#define CQ_MAX_AREA_LIGHTS 16

layout(std140, binding = 0) uniform CQ_FrameBlock
{
    mat4 View;
    mat4 Projection;
    mat4 ViewProjection;
    mat4 InvView;
    mat4 InvProjection;
    vec4 CameraPosTime;          // xyz kamera, w zaman (s)
    vec4 ScreenSize;             // w, h, 1/w, 1/h

    // Ortam ışığı
    vec4 AmbientSkyColor;        // rgb, a = yoğunluk
    vec4 AmbientEquatorColor;
    vec4 AmbientGroundColor;
    ivec4 AmbientParams;         // x kaynak (0 skybox,1 renk,2 gradyan), y IBL var, z nokta ışık sayısı, w spot sayısı
    vec4 IrradianceSH[9];        // IBL difüz SH (π'ye bölünmüş, doğrudan * albedo)
    vec4 IBLParams;              // x yansıma yoğunluğu, y prefilter max mip, z cos(rot), w sin(rot)

    // Sis
    vec4 FogColor;               // rgb, a = açık mı
    vec4 FogParams;              // x mod, y yoğunluk, z başlangıç, w bitiş
    vec4 FogParams2;             // x yükseklik azalımı, y taban yüksekliği

    // Yönlü ışık (güneş)
    vec4 SunDirection;           // xyz ışığın gittiği yön, w açık mı
    vec4 SunColor;               // rgb * yoğunluk

    // Gölgeler
    vec4 ShadowParams;           // x açık, y şiddet, z kalite (0 sert,1 yumuşak,2 çok yumuşak), w yumuşaklık
    vec4 ShadowParams2;          // x derinlik bias, y normal bias, z cascade sayısı, w geçiş bandı
    vec4 ShadowColor;
    mat4 LightSpace[CQ_MAX_CASCADES];
    vec4 CascadeSplits;          // görüş uzaklığı
    vec4 CascadeTexelWorld;
    vec4 CascadeRects[CQ_MAX_CASCADES]; // atlas: xy offset, zw ölçek

    ivec4 FeatureFlags;          // x HDR çıktı, y lightmap, z APV, w yansıma probu sayısı
    ivec4 DebugParams;           // x görünüm modu (editör), y alan ışığı sayısı
    vec4 RenderParams;           // x malzeme dokusu mip sapması (yükseltme: iç çözünürlük düşükken dokular çıktı çözünürlüğündeki mipten)

    // Ray Traced GI prob hacmi (Include/DynamicGI.glsl)
    vec4 GIVolume0;              // xyz en küçük hücre, w prob aralığı (m; 0 = kapalı)
    vec4 GIVolume1;              // xyz prob sayısı, w atlas katman sütunu
    vec4 GIVolume2;              // x normal kaydırma, y görüş kaydırma (aralığa oran), z yoğunluk, w gökyüzü sızıntısı
} CQ_Frame;

struct CQ_PointLight
{
    vec4 PositionRange;          // xyz, w menzil
    vec4 ColorIntensity;         // rgb * yoğunluk
    vec4 Attenuation;            // x sabit, y doğrusal, z karesel
    vec4 Profile0, Profile1;     // Light Profile: açıya göre 8 örnek (varsayılan 1)
};

struct CQ_SpotLight
{
    vec4 PositionRange;
    vec4 ColorIntensity;
    vec4 DirectionCosOuter;      // xyz yön, w cos(dış açı)
    vec4 AttenuationCosInner;    // x sabit, y doğrusal, z karesel, w cos(iç açı)
    vec4 Profile0, Profile1;     // Light Profile: eksen -> dış açı arası 8 örnek (varsayılan 1)
};

// Dikdörtgen / disk alan ışığı (+Z = sağ x yukarı yönüne yayar)
struct CQ_AreaLight
{
    vec4 PositionRange;
    vec4 ColorIntensity;         // w: iki yüzlü
    vec4 Right;                  // xyz yarım genişlik, w şekil (0 dikdörtgen, 1 disk)
    vec4 Up;                     // xyz yarım yükseklik
};

layout(std140, binding = 1) uniform CQ_LightBlock
{
    CQ_PointLight PointLights[CQ_MAX_POINT_LIGHTS];
    CQ_SpotLight SpotLights[CQ_MAX_SPOT_LIGHTS];
    CQ_AreaLight AreaLights[CQ_MAX_AREA_LIGHTS];
} CQ_Lights;

vec3 CQ_CameraPosition() { return CQ_Frame.CameraPosTime.xyz; }
float CQ_Time() { return CQ_Frame.CameraPosTime.w; }
