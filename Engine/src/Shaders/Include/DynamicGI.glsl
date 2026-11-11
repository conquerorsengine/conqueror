// Ray Traced GI (Lumen benzeri dinamik küresel aydınlatma): kameranın çevresindeki ışık probu ızgarasının örneklenmesi.
// Her prob, sahnenin BVH'sine izlenen ışınlardan topladığı ışınımı (oktahedral 8x8 karo, 6x6 iç + kenar) ve yüzeylere uzaklığı
// (16x16 karo, ortalama + kare ortalaması; görünürlük için Chebyshev testi, duvar arkasından ışık sızmaz) taşır.
// Problar dünya ızgarasındadır (hücre * aralık); hacim kamerayla birlikte hücre hücre kayar, prob verisi halka tampon gibi
// (hücre mod sayı) yerinde kalır. Karolar 2D atlasa dizilir: y katmanları (layers) atlasta yan yana / alt alta bloklar.
// Forward shader: parametreler kare sabitlerinde (FrameData.glsl, GIVolume0..2); Ray Traced GI compute shader'ları
// CQ_GI_COMPUTE tanımlayıp aynı adlı uniform'ları kullanır (problar önceki karenin ışığını görür: çoklu sekme)

#ifdef CQ_GI_COMPUTE
uniform vec4 u_GIVolume0;
uniform vec4 u_GIVolume1;
uniform vec4 u_GIVolume2;
#define CQ_GI_V0 u_GIVolume0
#define CQ_GI_V1 u_GIVolume1
#define CQ_GI_V2 u_GIVolume2
#else
#define CQ_GI_V0 CQ_Frame.GIVolume0
#define CQ_GI_V1 CQ_Frame.GIVolume1
#define CQ_GI_V2 CQ_Frame.GIVolume2
#endif

uniform sampler2D u_GIIrradiance;   // ışınım atlası (oktahedral, karekök kodlu; a: gökyüzü görünürlüğü)
uniform sampler2D u_GIDistance;     // uzaklık atlası (ortalama, kare ortalaması)
uniform sampler2D u_GIProbeData;    // prob başına: xyz kayma (m), w durum (0 boş, 1 etkin, 2 geometri içinde)

#define CQ_GI_IRR_TILE 8
#define CQ_GI_DIST_TILE 16
// Işınım atlası karekök uzayında (yarım kayan nokta yeterli hassasiyette; harmanlama algısal olarak düzgün, örneklemede üs alma yok)

// Pozitif mod: hücre -> prob yuvası
ivec3 CQ_GISlot(ivec3 cell, ivec3 counts)
{
    return cell - counts * ivec3(floor(vec3(cell) / vec3(counts)));
}

// Yuvanın atlas karo konumu (karo birimi): x + Cx * (y % k), z + Cz * (y / k)
ivec2 CQ_GITile(ivec3 slot, ivec3 counts, int layerColumns)
{
    return ivec2(slot.x + counts.x * (slot.y % layerColumns), slot.z + counts.z * (slot.y / layerColumns));
}

float CQ_GISignNotZero(float v) { return v >= 0.0 ? 1.0 : -1.0; }

vec2 CQ_GIOctEncode(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8);
    vec2 e = n.xy;
    if (n.z < 0.0)
        e = (1.0 - abs(n.yx)) * vec2(CQ_GISignNotZero(n.x), CQ_GISignNotZero(n.y));
    return e;
}

vec3 CQ_GIOctDecode(vec2 e)
{
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

// Karo içi doku koordinatı (atlas uv): iç texel'ler 1..tile-2, kenar texel'leri bilinear için sarmalı kopyalar.
// e: oktahedral kod (0..1)
vec2 CQ_GIAtlasUVEnc(ivec2 tile, vec2 e, int tileSize, vec2 atlasSize)
{
    float inner = float(tileSize - 2);
    vec2 texel = vec2(tile * tileSize) + 1.0 + e * inner;
    return texel / atlasSize;
}

vec2 CQ_GIAtlasUV(ivec2 tile, vec3 dir, int tileSize, vec2 atlasSize)
{
    return CQ_GIAtlasUVEnc(tile, CQ_GIOctEncode(dir) * 0.5 + 0.5, tileSize, atlasSize);
}

// P noktasında N normalli yüzeyin aldığı ışınım (E / π; albedo ile çarpılır). V: yüzeyden kameraya.
// coverage: 0 = hacim dışında / prob yok (çağıran eski ortam ışığına döner), 1 = tam; hacim kenarındaki son hücrede yumuşak geçiş.
// skyVisibility: N çevresinde (kosinüs lobu) gökyüzüne ulaşan ışınların payı (kapalı yerde 0: gökyüzü yansıması da kapanır)
vec3 CQ_GIIrradianceVis(vec3 P, vec3 N, vec3 V, out float coverage, out float skyVisibility)
{
    coverage = 0.0;
    skyVisibility = 1.0;
    vec4 v0 = CQ_GI_V0, v1 = CQ_GI_V1, v2 = CQ_GI_V2;
    float spacing = v0.w;
    if (spacing <= 0.0)
        return vec3(0.0);
    ivec3 minCell = ivec3(floor(v0.xyz + 0.5));
    ivec3 counts = ivec3(floor(v1.xyz + 0.5));
    int layerColumns = max(int(v1.w + 0.5), 1);
    // Kendi yüzeyinden kaçış: normal ve kamera yönünde (aralığa oranla) kaydırılmış nokta
    vec3 biased = P + (N * v2.x + V * v2.y) * spacing;
    vec3 g = biased / spacing;
    vec3 lo = vec3(minCell), hi = vec3(minCell + counts - ivec3(1));
    vec3 dLo = g - lo, dHi = hi - g;
    float edge = min(min(min(dLo.x, dLo.y), dLo.z), min(min(dHi.x, dHi.y), dHi.z));
    if (edge <= 0.0)
        return vec3(0.0);
    ivec3 base = ivec3(floor(g));
    base = clamp(base, minCell, minCell + counts - ivec3(2));
    vec3 alpha = clamp(g - vec3(base), 0.0, 1.0);
    vec2 irrSize = vec2(textureSize(u_GIIrradiance, 0));
    vec2 distSize = vec2(textureSize(u_GIDistance, 0));
    vec2 encN = CQ_GIOctEncode(N) * 0.5 + 0.5;   // ışınım yönü 8 probda aynı
    vec3 sum = vec3(0.0);
    float wsum = 0.0, visSum = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 cell = base + off;
        ivec3 slot = CQ_GISlot(cell, counts);
        ivec2 tile = CQ_GITile(slot, counts, layerColumns);
        vec4 pd = texelFetch(u_GIProbeData, tile, 0);
        if (pd.w < 0.5 || pd.w > 1.5)
            continue;   // henüz güncellenmemiş ya da geometrinin içinde
        vec3 probePos = vec3(cell) * spacing + pd.xyz;
        vec3 tri = mix(1.0 - alpha, alpha, vec3(off));
        float w = max(tri.x * tri.y * tri.z, 1e-5);
        // Probdan (kaydırılmış) noktaya yön ve uzaklık
        vec3 probeToPoint = biased - probePos;
        float dist = length(probeToPoint);
        vec3 dirFromProbe = dist > 1e-5 ? probeToPoint / dist : -N;
        // Arkada kalan prob az katkı verir (yumuşak)
        float wrap = (dot(-dirFromProbe, N) + 1.0) * 0.5;
        w *= wrap * wrap + 0.2;
        // Görünürlük: probdan noktaya uzaklık, probun o yöndeki yüzey uzaklığı dağılımıyla (Chebyshev) karşılaştırılır
        vec2 m = textureLod(u_GIDistance, CQ_GIAtlasUV(tile, dirFromProbe, CQ_GI_DIST_TILE, distSize), 0.0).rg;
        float mean = m.x;
        float variance = abs(m.y - mean * mean);
        float visibility = 1.0;
        if (dist > mean)
        {
            float d = dist - mean;
            visibility = variance / (variance + d * d);
            visibility = visibility * visibility * visibility;
        }
        w *= max(visibility, 0.05);
        // Çok küçük ağırlıklar bastırılır (sızıntı)
        const float crush = 0.2;
        if (w < crush)
            w *= (w * w) / (crush * crush);
        vec4 texel = textureLod(u_GIIrradiance, CQ_GIAtlasUVEnc(tile, encN, CQ_GI_IRR_TILE, irrSize), 0.0);
        vec3 irr = max(texel.rgb, vec3(0.0));   // karekök uzayında harmanlanır (RTXGI)
        sum += irr * w;
        visSum += clamp(texel.a, 0.0, 1.0) * w;
        wsum += w;
    }
    if (wsum <= 1e-6)
        return vec3(0.0);
    vec3 r = sum / wsum;
    coverage = clamp(edge, 0.0, 1.0);
    skyVisibility = visSum / wsum;
    return r * r;
}

vec3 CQ_GIIrradiance(vec3 P, vec3 N, vec3 V, out float coverage)
{
    float vis;
    return CQ_GIIrradianceVis(P, N, V, coverage, vis);
}
