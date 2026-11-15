// textureGather yoksa (WebGL 2 / OpenGL ES 3.0, OpenGL 3.3: CQ_NO_GATHER) dört komşu texel tek tek okunur. Sıra ve konum
// GL ile aynıdır: x = (i0, j1), y = (i1, j1), z = (i1, j0), w = (i0, j0); kenarda sıkıştırma (son işlem dokuları gibi).
#ifdef CQ_NO_GATHER
vec4 cqGather(sampler2D s, vec2 uv, int comp)
{
    ivec2 size = textureSize(s, 0);
    vec2 p = uv * vec2(size) - 0.5;
    ivec2 i0 = ivec2(floor(p));
    ivec2 hi = size - ivec2(1);
    ivec2 a = clamp(i0 + ivec2(0, 1), ivec2(0), hi);
    ivec2 b = clamp(i0 + ivec2(1, 1), ivec2(0), hi);
    ivec2 c = clamp(i0 + ivec2(1, 0), ivec2(0), hi);
    ivec2 d = clamp(i0, ivec2(0), hi);
    return vec4(texelFetch(s, a, 0)[comp], texelFetch(s, b, 0)[comp], texelFetch(s, c, 0)[comp], texelFetch(s, d, 0)[comp]);
}
vec4 cqGather(sampler2D s, vec2 uv) { return cqGather(s, uv, 0); }
#define textureGather cqGather
#endif
