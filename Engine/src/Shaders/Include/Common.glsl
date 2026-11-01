// Conqueror's Engine - ortak shader yardımcıları
// Kullanım: #include "Include/Common.glsl"

const float PI = 3.14159265359;
const float INV_PI = 0.31830988618;
const float EPSILON = 1e-5;

float Saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 Saturate(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }
float Pow2(float x) { return x * x; }
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 SRGBToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 LinearToSRGB(vec3 c)
{
    c = max(c, vec3(0.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// Derinlik dokusu okuması. OpenGL ES'de derinlik dokuları doğrusal filtrelenemez (sürücüye NEAREST gider); ekran uzayı
// efektleri (SSAO, SSR) masaüstündeki gibi ara değerlenmiş derinlik bekler: ES hedefinde 4 okuma ile elle çift doğrusal.
// CQ_GLES gölgelendirici derleyicisi tarafından OpenGL ES hedeflerinde tanımlanır.
float SampleDepth(sampler2D depthTex, vec2 uv)
{
#ifdef CQ_GLES
    ivec2 size = textureSize(depthTex, 0);
    vec2 p = uv * vec2(size) - 0.5;
    vec2 f = fract(p);
    ivec2 i0 = ivec2(floor(p));
    ivec2 hi = size - ivec2(1);
    float d00 = texelFetch(depthTex, clamp(i0, ivec2(0), hi), 0).r;
    float d10 = texelFetch(depthTex, clamp(i0 + ivec2(1, 0), ivec2(0), hi), 0).r;
    float d01 = texelFetch(depthTex, clamp(i0 + ivec2(0, 1), ivec2(0), hi), 0).r;
    float d11 = texelFetch(depthTex, clamp(i0 + ivec2(1, 1), ivec2(0), hi), 0).r;
    return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
#else
    return texture(depthTex, uv).r;
#endif
}

// Interleaved gradient noise (Jimenez 2014) - banding/dither için
float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec2 OctEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 o = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * sign(n.xy);
    return o * 0.5 + 0.5;
}
