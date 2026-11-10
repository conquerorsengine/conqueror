// Kemik paleti ve blend shape (morph target) farkları: animasyonlu modellerin köşe aşaması.
//  - <= 128 kemik: u_BoneTransforms uniform dizisi (eski yol)
//  - daha çok kemik (MetaHuman yüz / gövde): u_BonePalette RGBA32F dokusu, kemik başına 4 texel, satır 1024 texel
//  - morph: u_MorphDeltas RGBA32F dokusu, köşe başına 2 texel (konum farkı, normal farkı), satır 1024 texel;
//    farklar CPU'da etkin ağırlıklarla toplanır (BlendShapes), köşe gl_VertexID ile bulunur
// Doku yolu GL 3.3 / 4.1 / ES 3.0 profillerinde de çalışır (SSBO / doku tamponu gerekmez).
#ifndef CQ_SKINNING_GLSL
#define CQ_SKINNING_GLSL

#define CQ_MAX_UNIFORM_BONES 128
uniform mat4 u_BoneTransforms[CQ_MAX_UNIFORM_BONES];
uniform int u_BoneTexture = 0;
uniform int u_BoneCount = 0;
uniform sampler2D u_BonePalette;
uniform int u_HasMorph = 0;
uniform sampler2D u_MorphDeltas;

mat4 CQ_BoneMatrix(int id)
{
    if (u_BoneTexture == 1)
    {
        int t = id * 4;
        ivec2 p = ivec2(t % 1024, t / 1024);
        return mat4(texelFetch(u_BonePalette, p, 0), texelFetch(u_BonePalette, p + ivec2(1, 0), 0),
                    texelFetch(u_BonePalette, p + ivec2(2, 0), 0), texelFetch(u_BonePalette, p + ivec2(3, 0), 0));
    }
    return u_BoneTransforms[id];
}

mat4 CQ_SkinMatrix(ivec4 ids, vec4 weights)
{
    int limit = u_BoneTexture == 1 ? u_BoneCount : CQ_MAX_UNIFORM_BONES;
    mat4 skin = mat4(0.0);
    float total = 0.0;
    for (int i = 0; i < 4; i++)
    {
        float w = weights[i];
        int b = ids[i];
        if (w <= 0.0 || b < 0 || b >= limit)
            continue;
        skin += CQ_BoneMatrix(b) * w;
        total += w;
    }
    return total < 1e-5 ? mat4(1.0) : skin / total;   // ağırlık toplamı 1 değilse normalize
}

void CQ_ApplyMorph(inout vec3 position, inout vec3 normal)
{
    if (u_HasMorph == 1)
    {
        int t = gl_VertexID * 2;
        position += texelFetch(u_MorphDeltas, ivec2(t % 1024, t / 1024), 0).xyz;
        normal += texelFetch(u_MorphDeltas, ivec2((t + 1) % 1024, (t + 1) / 1024), 0).xyz;
    }
}

#endif
