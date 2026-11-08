// Conqueror's Engine - PBR forward fragment gövdesi (PBR.cqsh ve PBRSkinned.cqsh paylaşır)
// Shader Graph enjeksiyon işaretleri burada; MaterialCompiler include'ları açtıktan sonra değiştirir.

#include "Include/Common.glsl"
#include "Include/FrameData.glsl"
#include "Include/Lighting.glsl"

layout(location = 0) out vec4 FragColor;
// SSR için: xy oktahedral dünya normali, z pürüzlülük, w 2 + metalik (opak yüzey; bu karede hareket eden nesnede
// 4 + metalik: Temporal AA hareket bayrağı). Şeffaflar 0 yazar: karışımda kaynak alfası 0 olduğundan alttaki opak
// yüzeyin verisi korunur.
layout(location = 1) out vec4 CQ_GBufferNormal;
// 3: güneş gölgesi (TAA: gölgesi değişen yüzey, ör. hareket eden nesnenin gölgesi, geçmişe güvenmez)
layout(location = 3) out float CQ_GBufferShadow;
uniform float u_MotionFlag = 0.0;   // 2: bu karede hareket etti (Renderer3D)
// SSGI için: doğrusal albedo (rgb) + ortam örtmesi (a); şeffaflar 0 (alttaki opak korunur)
layout(location = 2) out vec4 CQ_GBufferAlbedo;

vec2 CQ_OctEncode(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-5);
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.xy;
}

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec3 Tangent;
    vec2 TexCoord;
    vec2 TexCoord2;
    mat3 TBN;
    vec4 Color;
} fs_in;

struct Material {
    vec3 Albedo;
    float Metallic;
    float Roughness;
    float AO;
    vec3 EmissionColor;
    float EmissionStrength;
    vec2 Tiling;
    vec2 Offset;
    // HLSL (D3D11/12) paketlemesiyle std140 aynı ofsetleri versin: vec3 16 bayt sınırında, int en sonda
    vec3 SubsurfaceColor;
    float SubsurfaceAmount;
    int ShadingModel;
};

uniform Material u_Material;
uniform sampler2D u_AlbedoMap;
uniform sampler2D u_NormalMap;
uniform sampler2D u_MetallicMap;
uniform sampler2D u_RoughnessMap;
uniform sampler2D u_AOMap;
uniform sampler2D u_EmissionMap;

uniform bool u_UseAlbedoMap;
uniform bool u_UseNormalMap;
uniform bool u_UseMetallicMap;
uniform bool u_UseRoughnessMap;
uniform bool u_UseAOMap;
uniform bool u_UseEmissionMap;

uniform int u_RenderMode = 0;      // 0 opak, 1 cutout, 2 şeffaf
uniform int u_WorldAligned = 0;    // 1: UV dünya konumundan (üç eksenli izdüşüm), 2: nesne uzayında (metre); Tiling = metre başına tekrar
uniform float u_AlphaClip = 0.5;
uniform int u_MetallicChannel = 0;   // glTF metal/pürüz dokusu: metal B (2), pürüz G (1)
uniform int u_RoughnessChannel = 0;
uniform bool u_MapsScaleFactors = false; // glTF: doku * çarpan (yoksa doku çarpanın yerine geçer)
uniform float u_Opacity = 1.0;
uniform vec3 u_Tint = vec3(1.0);         // örnek renk çarpanı (Model.Tint), malzeme rengi gibi sRGB
uniform mat4 u_Transform;

// Shader Graph uyumluluğu
#define u_CameraPos CQ_CameraPosition()
#define u_Time CQ_Time()

// #GRAPH_INJECT_UNIFORMS#

// Kod shader'ı (Create > Shader > Lit Shader): include'dan önce CQ_USER_SURFACE tanımlanırsa kullanıcı yüzeyi değiştirir
#ifdef CQ_USER_SURFACE
void CQ_UserSurface(inout CQ_Surface surface, vec2 uv);
#endif

void main()
{
    vec2 uv = fs_in.TexCoord * u_Material.Tiling + u_Material.Offset;
    vec4 albedoSample = vec4(1.0);
    if (u_WorldAligned != 0)
    {
        // Üç eksenli izdüşüm: yüzeye en dik eksenler ağırlıklı karışır; diğer haritalar baskın düzlemi kullanır
        vec3 pos = fs_in.FragPos;
        vec3 nrm = normalize(fs_in.Normal);
        if (u_WorldAligned == 2)
        {
            // Nesne uzayı (dönüşsüz, ölçekli): doku nesneyle hareket eder, ölçekte metre boyutunu korur
            mat3 m = mat3(u_Transform);
            vec3 sc = vec3(length(m[0]), length(m[1]), length(m[2]));
            mat3 rot = mat3(m[0] / max(sc.x, 1e-6), m[1] / max(sc.y, 1e-6), m[2] / max(sc.z, 1e-6));
            pos = transpose(rot) * (fs_in.FragPos - vec3(u_Transform[3]));
            nrm = normalize(transpose(rot) * nrm);
        }
        vec3 wn = abs(nrm);
        vec3 w = pow(wn, vec3(8.0));
        w /= max(w.x + w.y + w.z, 1e-5);
        vec3 p = pos * vec3(u_Material.Tiling.x, u_Material.Tiling.y, u_Material.Tiling.x);
        vec2 uvX = p.zy + u_Material.Offset;
        vec2 uvY = p.xz + u_Material.Offset;
        vec2 uvZ = p.xy + u_Material.Offset;
        uv = (wn.x > wn.y && wn.x > wn.z) ? uvX : ((wn.y > wn.z) ? uvY : uvZ);
        if (u_UseAlbedoMap)
            albedoSample = texture(u_AlbedoMap, uvX, CQ_Frame.RenderParams.x) * w.x + texture(u_AlbedoMap, uvY, CQ_Frame.RenderParams.x) * w.y + texture(u_AlbedoMap, uvZ, CQ_Frame.RenderParams.x) * w.z;
    }
    else if (u_UseAlbedoMap)
        albedoSample = texture(u_AlbedoMap, uv, CQ_Frame.RenderParams.x);
    float alpha = albedoSample.a * u_Opacity;
    if (u_RenderMode == 1 && alpha < u_AlphaClip)
        discard;

    // Mesh Paint köşe rengi albedoyu çarpar (boyanmamış nesnede beyaz)
    vec3 albedo = SRGBToLinear(albedoSample.rgb * u_Material.Albedo * u_Tint * fs_in.Color.rgb);

    float metallic = u_UseMetallicMap ? texture(u_MetallicMap, uv, CQ_Frame.RenderParams.x)[u_MetallicChannel] * (u_MapsScaleFactors ? u_Material.Metallic : 1.0)
                                      : u_Material.Metallic;
    float roughness = u_UseRoughnessMap ? texture(u_RoughnessMap, uv, CQ_Frame.RenderParams.x)[u_RoughnessChannel] * (u_MapsScaleFactors ? u_Material.Roughness : 1.0)
                                        : u_Material.Roughness;
    float ao = u_UseAOMap ? texture(u_AOMap, uv, CQ_Frame.RenderParams.x).r * u_Material.AO : u_Material.AO;

    vec3 emission = u_UseEmissionMap ? SRGBToLinear(texture(u_EmissionMap, uv, CQ_Frame.RenderParams.x).rgb) * u_Material.EmissionColor
                                     : u_Material.EmissionColor;
    emission *= u_Material.EmissionStrength;

    vec3 geometricNormal = normalize(fs_in.Normal);
    if (!gl_FrontFacing)
        geometricNormal = -geometricNormal; // çift taraflı materyaller

    vec3 N;
    if (u_UseNormalMap)
    {
        vec3 tangentNormal = texture(u_NormalMap, uv, CQ_Frame.RenderParams.x).xyz * 2.0 - 1.0;
        N = normalize(fs_in.TBN * tangentNormal);
        if (!gl_FrontFacing)
            N = -N;
    }
    else
    {
        N = geometricNormal;
    }

    // #GRAPH_INJECT_EVALUATION#

    CQ_Surface surface;
    surface.Position = fs_in.FragPos;
    surface.Normal = normalize(N);
    surface.GeometricNormal = geometricNormal;
    surface.Albedo = albedo;
    surface.Metallic = Saturate(metallic);
    surface.Roughness = Saturate(roughness);
    surface.AO = Saturate(ao);
    surface.Emission = emission;
    surface.Alpha = (u_RenderMode == 2) ? alpha : 1.0;
    surface.LightmapUV = fs_in.TexCoord2;
    surface.ShadingModel = u_Material.ShadingModel;
    surface.SubsurfaceColor = u_Material.SubsurfaceColor;
    surface.SubsurfaceAmount = u_Material.SubsurfaceAmount;
    surface.Tangent = fs_in.TBN[0];
#ifdef CQ_USER_SURFACE
    CQ_UserSurface(surface, uv);
#endif

    // Unlit: ekran-uzayı yansıma / GI bu yüzeye eklenmesin diye G-buffer'da tam pürüzlü
    bool unlit = surface.ShadingModel == 4;
    CQ_GBufferNormal = u_RenderMode == 2 ? vec4(0.0) : vec4(CQ_OctEncode(surface.Normal), unlit ? 1.0 : surface.Roughness, 2.0 + u_MotionFlag + clamp(surface.Metallic, 0.0, 1.0));
    CQ_GBufferAlbedo = u_RenderMode == 2 ? vec4(0.0) : vec4(surface.Albedo * (1.0 - surface.Metallic), surface.AO);
    CQ_GBufferShadow = 1.0;   // gölgelendirme (aşağıda) güneş gölgesini yazar; hata ayıklama / ışıksız yolda aydınlık

    // Editör hata ayıklama görünümleri
    int debugView = CQ_Frame.DebugParams.x;
    if (debugView == 1)
    {
        FragColor = CQ_FinalizeOutput(vec4(surface.Albedo + surface.Emission, 1.0));
        return;
    }
    if (debugView == 2)
    {
        FragColor = CQ_FinalizeOutput(vec4(SRGBToLinear(surface.Normal * 0.5 + 0.5), 1.0));
        return;
    }
    if (debugView == 3)
    {
        surface.Albedo = vec3(0.75);
        surface.Metallic = 0.0;
        surface.Emission = vec3(0.0);
    }
    else if (debugView == 4)
    {
        FragColor = CQ_FinalizeOutput(vec4(SRGBToLinear(vec3(surface.Metallic, surface.Roughness, 0.0)), 1.0));
        return;
    }
    else if (debugView == 5)
    {
        FragColor = CQ_FinalizeOutput(vec4(SRGBToLinear(vec3(surface.AO)), 1.0));
        return;
    }

    if (unlit && debugView == 0)
    {
        FragColor = CQ_FinalizeOutput(vec4(surface.Albedo + surface.Emission, surface.Alpha));
        return;
    }
    FragColor = CQ_FinalizeOutput(CQ_ShadeSurface(surface));
    CQ_GBufferShadow = g_CQSunShadow;
}
