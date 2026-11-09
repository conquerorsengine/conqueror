// Conqueror's Engine - PBR aydınlatma kütüphanesi
// Gerektirir: Include/Common.glsl, Include/FrameData.glsl
//
// Doku birimleri (Renderer/TextureSlots.h ile aynı):
//   12 gölge atlası, 13 IBL prefilter küp, 14 BRDF LUT, 15 lightmap,
//   16-19 yansıma probları, 20-21 APV

#define CQ_MAX_REFLECTION_PROBES 4

uniform sampler2D u_ShadowMap;
uniform samplerCube u_IBLPrefiltered;
uniform sampler2D u_BRDFLUT;
uniform sampler2D u_Lightmap;
uniform samplerCube u_ReflectionProbes[CQ_MAX_REFLECTION_PROBES];
uniform vec4 u_ProbePositionIntensity[CQ_MAX_REFLECTION_PROBES];
uniform vec4 u_ProbeBoxMin[CQ_MAX_REFLECTION_PROBES];   // w = blend mesafesi
uniform vec4 u_ProbeBoxMax[CQ_MAX_REFLECTION_PROBES];   // w = mip sayısı

// Adaptive Probe Volume
uniform sampler2D u_APVPositionTex;
uniform sampler2D u_APVSHTex;
uniform int u_APVNumProbes = 0;
uniform int u_ReceiveShadows = 1;

struct CQ_Surface
{
    vec3 Position;
    vec3 Normal;          // birim, dünya uzayı
    vec3 GeometricNormal;
    vec3 Albedo;          // doğrusal
    float Metallic;
    float Roughness;      // algısal
    float AO;
    vec3 Emission;        // doğrusal HDR
    float Alpha;
    vec2 LightmapUV;
    // Gölgelendirme modeli (Unreal Shading Model): 0 Default Lit, 1 Subsurface (deri), 2 Hair, 3 Cloth
    int ShadingModel;
    vec3 SubsurfaceColor; // deri: saçılma rengi, saç: ikincil vurgu rengi, kumaş: parlaklık (sheen) rengi
    float SubsurfaceAmount;
    vec3 Tangent;         // saç anizotropisi için
};

// ---------------------------------------------------------------- BRDF

float CQ_D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

// Height-correlated Smith (Heitz 2014) - görünürlük terimi (G / 4NL NV dahil)
float CQ_V_SmithGGXCorrelated(float NdotV, float NdotL, float a)
{
    float a2 = a * a;
    float lv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ll = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / (lv + ll + 1e-6);
}

vec3 CQ_F_Schlick(float VdotH, vec3 F0)
{
    return F0 + (1.0 - F0) * Pow5(1.0 - VdotH);
}

vec3 CQ_F_SchlickRoughness(float NdotV, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * Pow5(1.0 - NdotV);
}

// Kajiya-Kay saç vurgusu: teğete dik bileşen
float CQ_HairStrand(vec3 T, vec3 H, float exponent)
{
    float TdotH = dot(T, H);
    float sinTH = sqrt(max(1.0 - TdotH * TdotH, 0.0));
    return smoothstep(-1.0, 0.0, TdotH) * pow(sinTH, exponent);
}

// Doğrudan ışık katkısı (tek ışık)
vec3 CQ_EvaluateBRDF(CQ_Surface s, vec3 V, vec3 L, vec3 radiance, vec3 F0, vec3 energyCompensation)
{
    vec3 N = s.Normal;
    float NdotL = dot(N, L);
    if (s.ShadingModel == 1)
    {
        // Deri (Subsurface): sarılı yayınık ışık, gölge sınırında saçılma rengi taşması, arkadan geçen ışık
        float amount = Saturate(s.SubsurfaceAmount);
        float w = 0.6 * amount;
        float wrapped = Saturate((NdotL + w) / (1.0 + w));
        float plain = Saturate(NdotL);
        vec3 bleed = s.SubsurfaceColor * max(wrapped - plain, 0.0) * 1.5;
        float transmission = pow(Saturate(dot(V, -L)), 6.0) * 0.35 * amount * (1.0 - plain);
        vec3 diffuse = s.Albedo * INV_PI * (vec3(plain) + bleed + s.SubsurfaceColor * transmission) * (1.0 - s.Metallic);
        vec3 specular = vec3(0.0);
        if (NdotL > 0.0)
        {
            vec3 H = normalize(V + L);
            float a = max(s.Roughness * s.Roughness, 0.002);
            float NdotV = max(dot(N, V), 1e-4);
            specular = CQ_D_GGX(Saturate(dot(N, H)), a) * CQ_V_SmithGGXCorrelated(NdotV, NdotL, a)
                     * CQ_F_Schlick(Saturate(dot(V, H)), F0) * energyCompensation * NdotL;
        }
        return (diffuse + specular) * radiance;
    }
    if (s.ShadingModel == 2)
    {
        // Saç (Kajiya-Kay, iki kaydırılmış vurgu): yumuşak yayınık + teğet boyunca parlak bant
        vec3 T = normalize(s.Tangent - N * dot(s.Tangent, N) + 1e-5);
        vec3 H = normalize(V + L);
        float shift = (s.SubsurfaceAmount - 0.5) * 0.3;
        vec3 T1 = normalize(T + N * shift);
        vec3 T2 = normalize(T + N * (shift - 0.15));
        float gloss = mix(400.0, 20.0, Saturate(s.Roughness));
        vec3 spec = vec3(CQ_HairStrand(T1, H, gloss)) * 0.25 + s.SubsurfaceColor * s.Albedo * CQ_HairStrand(T2, H, gloss * 0.25) * 0.35;
        float soft = Saturate(NdotL * 0.75 + 0.25);
        return (s.Albedo * INV_PI * soft + spec * Saturate(NdotL + 0.2)) * radiance;
    }
    if (NdotL <= 0.0)
        return vec3(0.0);
    if (s.ShadingModel == 3)
    {
        // Kumaş: yayınık + kenarlarda yumuşak parlaklık (sheen), keskin yansıma yok
        float NdotV = max(dot(N, V), 1e-4);
        vec3 H = normalize(V + L);
        float sinNH = sqrt(max(1.0 - Pow2(Saturate(dot(N, H))), 0.0));
        float invR = 1.0 / max(s.Roughness, 0.05);
        float charlie = (2.0 + invR) * pow(sinNH, invR) / (2.0 * PI);
        float sheenVis = 1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV) + 1e-4);
        vec3 sheen = s.SubsurfaceColor * Saturate(s.SubsurfaceAmount) * charlie * sheenVis;
        return (s.Albedo * INV_PI + sheen) * radiance * NdotL;
    }
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = Saturate(dot(N, H));
    float VdotH = Saturate(dot(V, H));

    float a = max(s.Roughness * s.Roughness, 0.002);
    float D = CQ_D_GGX(NdotH, a);
    float Vis = CQ_V_SmithGGXCorrelated(NdotV, NdotL, a);
    vec3 F = CQ_F_Schlick(VdotH, F0);

    vec3 specular = D * Vis * F * energyCompensation;
    vec3 kD = (1.0 - F) * (1.0 - s.Metallic);
    vec3 diffuse = kD * s.Albedo * INV_PI;
    return (diffuse + specular) * radiance * NdotL;
}

// ---------------------------------------------------------------- Işık sönümü

// Menzil sonunda sıfıra yumuşakça inen pencere (UE4 stili) -> sert halka yok
float CQ_RangeWindow(float dist, float range)
{
    float r = dist / max(range, 1e-3);
    float r4 = r * r * r * r;
    return Pow2(Saturate(1.0 - r4));
}

float CQ_Attenuation(float dist, float range, vec3 clq)
{
    float denom = clq.x + clq.y * dist + clq.z * dist * dist;
    return CQ_RangeWindow(dist, range) / max(denom, 1e-4);
}

// ---------------------------------------------------------------- Gölgeler

const vec2 CQ_POISSON16[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
);

vec2 CQ_ShadowAtlasUV(int cascade, vec2 uv)
{
    vec4 r = CQ_Frame.CascadeRects[cascade];
    return r.xy + uv * r.zw;
}

float CQ_ShadowCascade(int cascade, vec3 worldPos, vec3 N, float NdotL, vec2 pixel)
{
    float texelWorld = CQ_Frame.CascadeTexelWorld[cascade];
    float normalBias = max(CQ_Frame.ShadowParams2.y, texelWorld * 1.0) * (1.0 - 0.5 * NdotL);
    vec3 biasedPos = worldPos + N * normalBias;

    vec4 ls = CQ_Frame.LightSpace[cascade] * vec4(biasedPos, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (p.z >= 1.0 || p.z <= 0.0 || any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0))))
        return 1.0;

    // Derinlik sapması dünya biriminde, cascade texel boyutuna göre (eğim ve PCF çekirdeğiyle büyür). Eskiden
    // normalize derinlikte sabitti: ışık frustumunun derinliği 8 x cascade yarıçapı olduğundan ~40 texel'e denk
    // geliyordu ve iç mekânlarda duvar / tavan birleşimlerinden ışık sızıyordu. DepthBias ayarı çarpan olarak kalır
    // (varsayılan 0.005 = 1x).
    int quality = int(CQ_Frame.ShadowParams.z + 0.5);
    float softnessK = max(CQ_Frame.ShadowParams.w, 0.0);
    float slope = clamp(sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.08), 0.0, 6.0);
    float kernel = quality == 0 ? 0.5 : (quality == 1 ? 1.5 * softnessK : 4.0 * softnessK);
    float biasTexels = (CQ_Frame.ShadowParams2.x / 0.005) * (1.0 + slope * (0.75 + 0.5 * kernel));
    mat4 lsm = CQ_Frame.LightSpace[cascade];
    float depthRange = 2.0 / max(length(vec3(lsm[0][2], lsm[1][2], lsm[2][2])), 1e-6);
    float bias = max(biasTexels * texelWorld / depthRange, 0.00002);
    float receiver = p.z - bias;

    vec4 rect = CQ_Frame.CascadeRects[cascade];
    vec2 atlasTexel = 1.0 / vec2(textureSize(u_ShadowMap, 0));
    vec2 rectMin = rect.xy + atlasTexel;
    vec2 rectMax = rect.xy + rect.zw - atlasTexel;
    vec2 center = CQ_ShadowAtlasUV(cascade, p.xy);

    if (quality == 0)
        return receiver > texture(u_ShadowMap, clamp(center, rectMin, rectMax)).r ? 0.0 : 1.0;

    // Poisson diski her pikselde döndür -> bantlaşma yerine ince gürültü
    float angle = InterleavedGradientNoise(pixel) * 6.2831853;
    float ca = cos(angle), sa = sin(angle);
    mat2 rot = mat2(ca, sa, -sa, ca);

    float softness = max(CQ_Frame.ShadowParams.w, 0.0);
    float radiusTexels = 1.5 * softness;

    if (quality >= 2)
    {
        // PCSS: engelleyici araması -> yarı gölge genişliği
        float searchRadius = 6.0 * softness;
        float blockerSum = 0.0;
        float blockers = 0.0;
        for (int i = 0; i < 16; ++i)
        {
            vec2 uv = clamp(center + rot * CQ_POISSON16[i] * searchRadius * atlasTexel, rectMin, rectMax);
            float d = texture(u_ShadowMap, uv).r;
            if (d < receiver)
            {
                blockerSum += d;
                blockers += 1.0;
            }
        }
        if (blockers < 1.0)
            return 1.0;
        float avgBlocker = blockerSum / blockers;
        float penumbra = (receiver - avgBlocker) / max(avgBlocker, 1e-4);
        radiusTexels = clamp(penumbra * 220.0 * softness, 1.0, 14.0 * max(softness, 0.25));
    }

    float lit = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        vec2 uv = clamp(center + rot * CQ_POISSON16[i] * radiusTexels * atlasTexel, rectMin, rectMax);
        lit += receiver > texture(u_ShadowMap, uv).r ? 0.0 : 1.0;
    }
    return lit / 16.0;
}

// 1 = tamamen aydınlık, 0 = tam gölge
float CQ_SunShadow(vec3 worldPos, vec3 N, vec3 L, vec2 pixel)
{
    if (CQ_Frame.ShadowParams.x < 0.5 || u_ReceiveShadows == 0)
        return 1.0;
    int cascadeCount = int(CQ_Frame.ShadowParams2.z + 0.5);
    if (cascadeCount <= 0)
        return 1.0;

    float viewDepth = -(CQ_Frame.View * vec4(worldPos, 1.0)).z;
    int cascade = -1;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (viewDepth < CQ_Frame.CascadeSplits[i])
        {
            cascade = i;
            break;
        }
    }
    if (cascade < 0)
        return 1.0; // gölge mesafesinin dışında

    float NdotL = Saturate(dot(N, L));
    float shadow = CQ_ShadowCascade(cascade, worldPos, N, NdotL, pixel);

    // Cascade geçişlerinde yumuşak karışım (patlama yok)
    float fade = CQ_Frame.ShadowParams2.w;
    float split = CQ_Frame.CascadeSplits[cascade];
    if (fade > 0.0 && viewDepth > split - fade)
    {
        float t = Saturate((viewDepth - (split - fade)) / fade);
        float next = (cascade + 1 < cascadeCount) ? CQ_ShadowCascade(cascade + 1, worldPos, N, NdotL, pixel) : 1.0;
        shadow = mix(shadow, next, t);
    }

    return mix(1.0, shadow, Saturate(CQ_Frame.ShadowParams.y));
}

// ---------------------------------------------------------------- IBL

vec3 CQ_RotateSky(vec3 d)
{
    float c = CQ_Frame.IBLParams.z;
    float s = CQ_Frame.IBLParams.w;
    return vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
}

vec3 CQ_EvaluateSH(vec3 n)
{
    // SH L2, kosinüs lobu ile önceden konvolüsyon yapılmış, π'ye bölünmüş
    vec3 r = CQ_Frame.IrradianceSH[0].rgb * 0.282095;
    r += CQ_Frame.IrradianceSH[1].rgb * 0.488603 * n.y;
    r += CQ_Frame.IrradianceSH[2].rgb * 0.488603 * n.z;
    r += CQ_Frame.IrradianceSH[3].rgb * 0.488603 * n.x;
    r += CQ_Frame.IrradianceSH[4].rgb * 1.092548 * n.x * n.y;
    r += CQ_Frame.IrradianceSH[5].rgb * 1.092548 * n.y * n.z;
    r += CQ_Frame.IrradianceSH[6].rgb * 0.315392 * (3.0 * n.z * n.z - 1.0);
    r += CQ_Frame.IrradianceSH[7].rgb * 1.092548 * n.x * n.z;
    r += CQ_Frame.IrradianceSH[8].rgb * 0.546274 * (n.x * n.x - n.y * n.y);
    return max(r, vec3(0.0));
}

vec3 CQ_AmbientDiffuse(vec3 N)
{
    int source = CQ_Frame.AmbientParams.x;
    float intensity = CQ_Frame.AmbientSkyColor.a;
    if (source == 0)
    {
        if (CQ_Frame.AmbientParams.y == 1)
            return CQ_EvaluateSH(CQ_RotateSky(N)) * intensity;
        return CQ_Frame.AmbientSkyColor.rgb * intensity;
    }
    if (source == 1)
        return CQ_Frame.AmbientSkyColor.rgb * intensity;

    // Gradyan (yarıküre)
    float up = N.y;
    vec3 sky = CQ_Frame.AmbientSkyColor.rgb;
    vec3 equator = CQ_Frame.AmbientEquatorColor.rgb;
    vec3 ground = CQ_Frame.AmbientGroundColor.rgb;
    vec3 c = up >= 0.0 ? mix(equator, sky, smoothstep(0.0, 1.0, up)) : mix(equator, ground, smoothstep(0.0, 1.0, -up));
    return c * intensity;
}

// Kutu izdüşümü ile paralaks düzeltmeli yansıma yönü
vec3 CQ_BoxProject(vec3 R, vec3 worldPos, vec3 boxMin, vec3 boxMax, vec3 probePos)
{
    vec3 firstPlane = (boxMax - worldPos) / R;
    vec3 secondPlane = (boxMin - worldPos) / R;
    vec3 furthest = max(firstPlane, secondPlane);
    float dist = min(min(furthest.x, furthest.y), furthest.z);
    vec3 hit = worldPos + R * dist;
    return hit - probePos;
}


// Örnekleyici dizileri yalnızca sabit dizinle örneklenir: Direct3D 11 (SM 5.0) ve bazı GL sürücüleri değişken
// dizinli örnekleyici dizisini derlemez (HLSL X3512). Döngü değişkeni switch ile sabit dizine çevrilir.
#if CQ_MAX_REFLECTION_PROBES != 4
#error "CQ_SampleReflectionProbe has one case per probe: update it with CQ_MAX_REFLECTION_PROBES"
#endif
vec3 CQ_SampleReflectionProbe(int i, vec3 dir, float lod)
{
    switch (i)
    {
        case 0: return textureLod(u_ReflectionProbes[0], dir, lod).rgb;
        case 1: return textureLod(u_ReflectionProbes[1], dir, lod).rgb;
        case 2: return textureLod(u_ReflectionProbes[2], dir, lod).rgb;
        default: return textureLod(u_ReflectionProbes[3], dir, lod).rgb;
    }
}

vec3 CQ_SpecularEnvironment(vec3 R, float roughness, vec3 worldPos)
{
    vec3 sky = vec3(0.0);
    if (CQ_Frame.AmbientParams.y == 1)
    {
        float lod = roughness * CQ_Frame.IBLParams.y;
        sky = textureLod(u_IBLPrefiltered, CQ_RotateSky(R), lod).rgb * CQ_Frame.AmbientSkyColor.a;
    }
    else
    {
        sky = CQ_AmbientDiffuse(R);
    }

    int probeCount = CQ_Frame.FeatureFlags.w;
    vec3 result = sky;
    float bestWeight = 0.0;
    for (int i = 0; i < CQ_MAX_REFLECTION_PROBES; ++i)
    {
        if (i >= probeCount)
            break;
        vec3 bmin = u_ProbeBoxMin[i].xyz;
        vec3 bmax = u_ProbeBoxMax[i].xyz;
        float blend = max(u_ProbeBoxMin[i].w, 1e-3);
        vec3 d = min(worldPos - bmin, bmax - worldPos);
        float inside = min(min(d.x, d.y), d.z);
        if (inside <= 0.0)
            continue;
        float w = Saturate(inside / blend);
        if (w <= bestWeight)
            continue;
        vec3 dir = CQ_BoxProject(R, worldPos, bmin, bmax, u_ProbePositionIntensity[i].xyz);
        float lod = roughness * max(u_ProbeBoxMax[i].w - 1.0, 0.0);
        vec3 probe = CQ_SampleReflectionProbe(i, dir, lod) * u_ProbePositionIntensity[i].w;
        result = mix(sky, probe, w);
        bestWeight = w;
    }
    return result * CQ_Frame.IBLParams.x;
}

// ---------------------------------------------------------------- APV

vec3 CQ_APVIrradiance(vec3 worldPos, vec3 n)
{
    if (CQ_Frame.FeatureFlags.z == 0 || u_APVNumProbes == 0)
        return vec3(0.0);

    float bestD[8];
    int bestI[8];
    for (int i = 0; i < 8; ++i) { bestD[i] = 1e20; bestI[i] = -1; }

    for (int p = 0; p < u_APVNumProbes; ++p)
    {
        vec4 pd = texelFetch(u_APVPositionTex, ivec2(p, 0), 0);
        if (pd.a < 0.5)
            continue;
        vec3 dv = pd.xyz - worldPos;
        float d = dot(dv, dv);
        if (d < bestD[7])
        {
            bestD[7] = d; bestI[7] = p;
            for (int i = 6; i >= 0; --i)
            {
                if (bestD[i + 1] < bestD[i])
                {
                    float td = bestD[i]; bestD[i] = bestD[i + 1]; bestD[i + 1] = td;
                    int ti = bestI[i]; bestI[i] = bestI[i + 1]; bestI[i + 1] = ti;
                }
                else break;
            }
        }
    }

    vec3 sh[9];
    for (int i = 0; i < 9; ++i) sh[i] = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        if (bestI[i] < 0) continue;
        float w = 1.0 / (bestD[i] + 1e-4);
        for (int c = 0; c < 9; ++c)
            sh[c] += texelFetch(u_APVSHTex, ivec2(c, bestI[i]), 0).rgb * w;
        total += w;
    }
    if (total <= 0.0)
        return vec3(0.0);
    for (int c = 0; c < 9; ++c) sh[c] /= total;

    vec3 r = sh[0] * 0.282095
           + sh[1] * 0.488603 * n.y + sh[2] * 0.488603 * n.z + sh[3] * 0.488603 * n.x
           + sh[4] * 1.092548 * n.x * n.y + sh[5] * 1.092548 * n.y * n.z
           + sh[6] * 0.315392 * (3.0 * n.z * n.z - 1.0)
           + sh[7] * 1.092548 * n.x * n.z + sh[8] * 0.546274 * (n.x * n.x - n.y * n.y);
    return max(r, vec3(0.0));
}

// ---------------------------------------------------------------- Sis

vec3 CQ_ApplyFog(vec3 color, vec3 worldPos)
{
    if (CQ_Frame.FogColor.a < 0.5)
        return color;
    float dist = length(worldPos - CQ_CameraPosition());
    int mode = int(CQ_Frame.FogParams.x + 0.5);
    float density = CQ_Frame.FogParams.y;
    float f;
    if (mode == 0)
        f = Saturate((dist - CQ_Frame.FogParams.z) / max(CQ_Frame.FogParams.w - CQ_Frame.FogParams.z, 1e-3));
    else if (mode == 1)
        f = 1.0 - exp(-density * dist);
    else
        f = 1.0 - exp(-Pow2(density * dist));

    float heightFalloff = CQ_Frame.FogParams2.x;
    if (heightFalloff > 0.0)
        f *= exp(-heightFalloff * max(worldPos.y - CQ_Frame.FogParams2.y, 0.0));

    return mix(color, CQ_Frame.FogColor.rgb, Saturate(f));
}

// ---------------------------------------------------------------- Alan ışıkları
// Difüz: çokgenin tam Lambert form faktörü (kenar integrali); yansıma: yansıyan ışına en yakın
// "temsilci nokta" + Karis (2013) enerji normalizasyonu (lob ışığın açısal boyutu kadar genişletilir).

float CQ_EdgeIntegral(vec3 a, vec3 b, vec3 n)
{
    float c = clamp(dot(a, b), -0.9999, 0.9999);
    vec3 cr = cross(a, b);
    float len = length(cr);
    if (len < 1e-6)
        return 0.0;
    return acos(c) * dot(cr / len, n);
}

vec3 CQ_AreaLightContribution(CQ_Surface s, CQ_AreaLight al, vec3 V, vec3 F0, vec3 energyCompensation)
{
    vec3 C = al.PositionRange.xyz;
    vec3 X = al.Right.xyz;
    vec3 Y = al.Up.xyz;
    float hx = length(X), hy = length(Y);
    if (hx < 1e-5 || hy < 1e-5)
        return vec3(0.0);
    vec3 Xn = X / hx, Yn = Y / hy;
    vec3 Nl = normalize(cross(Xn, Yn));
    vec3 toP = s.Position - C;
    float side = dot(toP, Nl);
    bool twoSided = al.ColorIntensity.w > 0.5;
    if (side <= 0.0 && !twoSided)
        return vec3(0.0);
    float dist = length(toP);
    if (dist >= al.PositionRange.w)
        return vec3(0.0);
    float window = CQ_RangeWindow(dist, al.PositionRange.w);
    bool disc = al.Right.w > 0.5;

    // Difüz: köşe yönleriyle kenar integrali (disk: 8 köşeli çokgen)
    vec3 N = s.Normal;
    float ff = 0.0;
    if (disc)
    {
        vec3 prev = normalize(C + X - s.Position);
        for (int k = 1; k <= 8; ++k)
        {
            float t = float(k) * 0.78539816;
            vec3 cur = normalize(C + X * cos(t) + Y * sin(t) - s.Position);
            ff += CQ_EdgeIntegral(prev, cur, N);
            prev = cur;
        }
    }
    else
    {
        vec3 v0 = normalize(C - X - Y - s.Position);
        vec3 v1 = normalize(C + X - Y - s.Position);
        vec3 v2 = normalize(C + X + Y - s.Position);
        vec3 v3 = normalize(C - X + Y - s.Position);
        ff = CQ_EdgeIntegral(v0, v1, N) + CQ_EdgeIntegral(v1, v2, N) + CQ_EdgeIntegral(v2, v3, N) + CQ_EdgeIntegral(v3, v0, N);
    }
    // Köşeler +Z'den bakınca saat yönü tersine: ön taraftaki alıcı için toplam negatif
    ff = (side > 0.0 ? -ff : ff) * (0.5 * INV_PI);
    ff = max(ff, 0.0);
    vec3 radiance = al.ColorIntensity.rgb * window;

    float NdotV = max(dot(N, V), 1e-4);
    vec3 Fd = CQ_F_SchlickRoughness(NdotV, F0, s.Roughness);
    vec3 kD = (1.0 - Fd) * (1.0 - s.Metallic);
    vec3 diffuse = kD * s.Albedo * radiance * ff;

    // Yansıma: yansıyan ışının ışık düzlemini kestiği nokta, şekle kırpılır
    vec3 R = reflect(-V, N);
    float denom = dot(R, Nl);
    vec3 P;
    if (abs(denom) > 1e-4)
    {
        float t = dot(C - s.Position, Nl) / denom;
        P = t > 0.0 ? s.Position + R * t : s.Position - Nl * side;
    }
    else
        P = s.Position - Nl * side;
    vec3 local = P - C;
    float u = dot(local, Xn), v = dot(local, Yn);
    if (disc)
    {
        vec2 uv = vec2(u / hx, v / hy);
        float l = length(uv);
        if (l > 1.0) uv /= l;
        u = uv.x * hx;
        v = uv.y * hy;
    }
    else
    {
        u = clamp(u, -hx, hx);
        v = clamp(v, -hy, hy);
    }
    vec3 Pc = C + Xn * u + Yn * v;
    vec3 toL = Pc - s.Position;
    float dL = max(length(toL), 1e-4);
    vec3 L = toL / dL;
    float NdotL = dot(N, L);
    vec3 specular = vec3(0.0);
    if (NdotL > 0.0)
    {
        float area = disc ? 3.14159265 * hx * hy : 4.0 * hx * hy;
        float cosL = twoSided ? abs(dot(-L, Nl)) : max(dot(-L, Nl), 0.0);
        float solidAngle = min(area * cosL / (dL * dL), 6.2831853);
        float a = max(s.Roughness * s.Roughness, 0.002);
        float sourceRadius = sqrt(area * INV_PI);
        float aWide = Saturate(a + sourceRadius / (2.0 * dL));
        vec3 H = normalize(V + L);
        float NdotH = Saturate(dot(N, H));
        float VdotH = Saturate(dot(V, H));
        float D = CQ_D_GGX(NdotH, aWide);
        float Vis = CQ_V_SmithGGXCorrelated(NdotV, NdotL, a);
        vec3 F = CQ_F_Schlick(VdotH, F0);
        specular = D * Vis * F * energyCompensation * radiance * NdotL * solidAngle;
    }
    return diffuse + specular;
}

// ---------------------------------------------------------------- Ana fonksiyon

// Son gölgelendirilen yüzeyin güneş gölgesi (G-buffer gölge eki / TAA gölge değişimi)
float g_CQSunShadow = 1.0;

// Light Profile: 8 örnek arasında doğrusal (t: 0..1)
float CQ_LightProfile(vec4 p0, vec4 p1, float t)
{
    float x = clamp(t, 0.0, 1.0) * 7.0;
    int i = min(int(x), 6);
    float samples[8] = float[8](p0.x, p0.y, p0.z, p0.w, p1.x, p1.y, p1.z, p1.w);
    return mix(samples[i], samples[i + 1], x - float(i));
}


vec4 CQ_ShadeSurface(CQ_Surface s)
{
    vec3 V = normalize(CQ_CameraPosition() - s.Position);
    float NdotV = max(dot(s.Normal, V), 1e-4);
    float roughness = clamp(s.Roughness, 0.045, 1.0);
    s.Roughness = roughness;

    vec3 F0 = mix(vec3(0.04), s.Albedo, s.Metallic);
    vec2 envBRDF = texture(u_BRDFLUT, vec2(NdotV, roughness)).rg;
    // Çoklu saçılma enerji telafisi (pürüzlü metaller kararmaz)
    vec3 energyCompensation = 1.0 + F0 * (1.0 / max(envBRDF.x + envBRDF.y, 1e-3) - 1.0);

    vec2 pixel = gl_FragCoord.xy;
    vec3 Lo = vec3(0.0);

    // Güneş
    if (CQ_Frame.SunDirection.w > 0.5)
    {
        vec3 L = normalize(-CQ_Frame.SunDirection.xyz);
        float shadow = CQ_SunShadow(s.Position, s.GeometricNormal, L, pixel);
        g_CQSunShadow = shadow;
        vec3 radiance = CQ_Frame.SunColor.rgb;
        vec3 direct = CQ_EvaluateBRDF(s, V, L, radiance, F0, energyCompensation);
        Lo += direct * mix(CQ_Frame.ShadowColor.rgb, vec3(1.0), shadow);
    }

    // Nokta ışıklar
    int pointCount = min(CQ_Frame.AmbientParams.z, CQ_MAX_POINT_LIGHTS);
    for (int i = 0; i < pointCount; ++i)
    {
        CQ_PointLight pl = CQ_Lights.PointLights[i];
        vec3 toLight = pl.PositionRange.xyz - s.Position;
        float dist = length(toLight);
        if (dist >= pl.PositionRange.w)
            continue;
        vec3 L = toLight / max(dist, 1e-4);
        float att = CQ_Attenuation(dist, pl.PositionRange.w, pl.Attenuation.xyz);
        att *= CQ_LightProfile(pl.Profile0, pl.Profile1, acos(clamp(-L.y, -1.0, 1.0)) / 3.14159265);
        Lo += CQ_EvaluateBRDF(s, V, L, pl.ColorIntensity.rgb * att, F0, energyCompensation);
    }

    // Spot ışıklar
    int spotCount = min(CQ_Frame.AmbientParams.w, CQ_MAX_SPOT_LIGHTS);
    for (int i = 0; i < spotCount; ++i)
    {
        CQ_SpotLight sl = CQ_Lights.SpotLights[i];
        vec3 toLight = sl.PositionRange.xyz - s.Position;
        float dist = length(toLight);
        if (dist >= sl.PositionRange.w)
            continue;
        vec3 L = toLight / max(dist, 1e-4);
        float cosTheta = dot(-L, normalize(sl.DirectionCosOuter.xyz));
        float cone = smoothstep(sl.DirectionCosOuter.w, sl.AttenuationCosInner.w, cosTheta);
        if (cone <= 0.0)
            continue;
        float att = CQ_Attenuation(dist, sl.PositionRange.w, sl.AttenuationCosInner.xyz) * cone;
        att *= CQ_LightProfile(sl.Profile0, sl.Profile1, acos(clamp(cosTheta, -1.0, 1.0)) / max(acos(clamp(sl.DirectionCosOuter.w, -1.0, 1.0)), 1e-3));
        Lo += CQ_EvaluateBRDF(s, V, L, sl.ColorIntensity.rgb * att, F0, energyCompensation);
    }

    // Alan ışıkları
    int areaCount = min(CQ_Frame.DebugParams.y, CQ_MAX_AREA_LIGHTS);
    for (int i = 0; i < areaCount; ++i)
        Lo += CQ_AreaLightContribution(s, CQ_Lights.AreaLights[i], V, F0, energyCompensation);

    // Dolaylı ışık
    vec3 F = CQ_F_SchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - s.Metallic);

    vec3 irradiance;
    if (CQ_Frame.FeatureFlags.y == 1)
        irradiance = texture(u_Lightmap, s.LightmapUV).rgb;
    else
        irradiance = CQ_AmbientDiffuse(s.Normal);
    if (CQ_Frame.FeatureFlags.z == 1)
        irradiance += CQ_APVIrradiance(s.Position, s.Normal);

    vec3 R = reflect(-V, s.Normal);
    // Pürüzlülük arttıkça yansıma normale doğru kayar (Frostbite)
    R = mix(R, s.Normal, roughness * roughness);
    vec3 prefiltered = CQ_SpecularEnvironment(R, roughness, s.Position);
    vec3 specularIndirect = prefiltered * (F0 * envBRDF.x + envBRDF.y) * energyCompensation;

    float specOcclusion = Saturate(pow(NdotV + s.AO, exp2(-16.0 * roughness - 1.0)) - 1.0 + s.AO);
    vec3 indirect = kD * irradiance * s.Albedo * s.AO + specularIndirect * specOcclusion;

    vec3 color = Lo + indirect + s.Emission;
    color = CQ_ApplyFog(color, s.Position);
    return vec4(color, s.Alpha);
}

// HDR çıktı kapalıysa (eski yol) burada ton eşleme + gamma uygulanır
vec4 CQ_FinalizeOutput(vec4 c)
{
    if (CQ_Frame.FeatureFlags.x == 1)
        return c;
    vec3 x = c.rgb;
    x = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return vec4(LinearToSRGB(x), c.a);
}
