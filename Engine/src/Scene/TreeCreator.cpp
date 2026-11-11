#include "TreeCreator.h"

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/GameplaySystems.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace Conqueror
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        // Üretilen modelin işareti: ağaç bileşeni kaldırılınca Mesh Renderer'daki eski ağaç temizlenir
        const char* kTreeModelTag = "<tree>";

        // Belirleyici rastgele sayı (xorshift32): aynı tohum her yerde aynı ağacı verir
        struct Rng
        {
            uint32_t S;
            explicit Rng(uint32_t seed) : S(seed * 2654435761u + 0x9E3779B9u) { if (S == 0) S = 0x1234567u; Next(); Next(); }
            uint32_t Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
            float Float() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }
            float Range(float a, float b) { return a + (b - a) * Float(); }
            glm::vec3 Unit()
            {
                const float z = Range(-1.0f, 1.0f), a = Range(0.0f, 2.0f * kPi), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
                return { r * std::cos(a), z, r * std::sin(a) };
            }
        };

        uint32_t HashInt(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
            return x;
        }

        // Periyodik değer gürültüsü (döşenebilir dokular): ızgara periyot kadar sarar
        float PeriodicNoise(float x, float y, int px, int py, uint32_t seed)
        {
            const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
            const float fx = x - (float)x0, fy = y - (float)y0;
            auto lattice = [&](int ix, int iy) {
                ix = ((ix % px) + px) % px;
                iy = ((iy % py) + py) % py;
                return (float)(HashInt((uint32_t)ix * 73856093u ^ (uint32_t)iy * 19349663u ^ seed) & 0xFFFF) / 65535.0f;
            };
            const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
            const float a = lattice(x0, y0), b = lattice(x0 + 1, y0), c = lattice(x0, y0 + 1), d = lattice(x0 + 1, y0 + 1);
            return glm::mix(glm::mix(a, b, sx), glm::mix(c, d, sx), sy);
        }

        // ── Varsayılan dokular (dosya gerekmez) ──

        // Kabuk: koyu dikey yarıklar, açık sırtlar; her iki yönde döşenir
        // Varsayılan dokular / malzemeler bilinçli olarak yok edilmez: statik yıkıcılar GL bağlamı kapandıktan sonra çalışır
        // (kapanışta çöker). Tek kopya, süreç bitince işletim sistemi bırakır
        std::shared_ptr<Texture2D> BarkTexture()
        {
            static std::shared_ptr<Texture2D>& s_Tex = *new std::shared_ptr<Texture2D>();
            if (s_Tex)
                return s_Tex;
            const int N = 256;
            std::vector<unsigned char> px((size_t)N * N * 4);
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                {
                    const float u = (float)x / N, v = (float)y / N;
                    // Dikey uzamış sırtlar + ince doku
                    const float ridge = PeriodicNoise(u * 10.0f, v * 2.0f, 10, 2, 11u) * 0.65f + PeriodicNoise(u * 20.0f, v * 4.0f, 20, 4, 23u) * 0.35f;
                    const float fine = PeriodicNoise(u * 48.0f, v * 24.0f, 48, 24, 37u);
                    const float crack = glm::smoothstep(0.30f, 0.55f, ridge);
                    const glm::vec3 dark(0.17f, 0.12f, 0.085f), light(0.47f, 0.38f, 0.29f);
                    glm::vec3 c = glm::mix(dark, light, crack) * (0.82f + 0.3f * fine);
                    c = glm::clamp(c, 0.0f, 1.0f);
                    unsigned char* p = &px[((size_t)y * N + x) * 4];
                    p[0] = (unsigned char)(c.b * 255.0f);
                    p[1] = (unsigned char)(c.g * 255.0f);
                    p[2] = (unsigned char)(c.r * 255.0f);
                    p[3] = 255;
                }
            s_Tex = Texture2D::CreateFromRawBGRA(N, N, px.data());
            return s_Tex;
        }

        // Yaprak kümesi: ortadaki sapın iki yanına dizilmiş yapraklar (saydam arka plan). Alfa kapsaması ~%55: uzakta küçük
        // mip seviyelerinde de kırpma eşiğinin üstünde kalır, ağaç uzaktan kelleşmez
        std::shared_ptr<Texture2D> LeafTexture()
        {
            static std::shared_ptr<Texture2D>& s_Tex = *new std::shared_ptr<Texture2D>();
            if (s_Tex)
                return s_Tex;
            const int N = 256;
            std::vector<glm::vec4> img((size_t)N * N, glm::vec4(0.30f, 0.45f, 0.10f, 0.0f));
            struct Leaf { glm::vec2 Base; float Angle; float Length; float Width; float Shade; };
            std::vector<Leaf> leaves;
            Rng rng(77u);
            // Sap: (0.5, 0.02) -> (0.5, 0.92), yapraklar iki yana sırayla, uca doğru küçülür
            const int count = 9;
            for (int i = 0; i < count; ++i)
            {
                const float t = 0.10f + 0.80f * (float)i / (float)(count - 1);
                const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                const float angle = side * glm::radians(rng.Range(38.0f, 58.0f)) * (1.0f - 0.45f * t);
                const float len = glm::mix(0.40f, 0.26f, t) * rng.Range(0.9f, 1.1f);
                leaves.push_back({ glm::vec2(0.5f, t * 0.86f + 0.02f), angle, len, len * 0.40f, rng.Range(0.82f, 1.12f) });
            }
            leaves.push_back({ glm::vec2(0.5f, 0.86f), 0.0f, 0.18f, 0.075f, 1.0f });   // uçtaki yaprak
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                {
                    const glm::vec2 p((x + 0.5f) / N, (y + 0.5f) / N);
                    glm::vec4& out = img[(size_t)y * N + x];
                    // Sap
                    const float stem = std::fabs(p.x - 0.5f);
                    if (stem < 0.008f && p.y > 0.0f && p.y < 0.9f)
                        out = glm::vec4(0.24f, 0.30f, 0.10f, 1.0f);
                    for (const Leaf& lf : leaves)
                    {
                        // Yaprak ekseni: tabandan uca, sapın yönünden açıyla dönmüş
                        const glm::vec2 dir(std::sin(lf.Angle), std::cos(lf.Angle));
                        const glm::vec2 nrm(dir.y, -dir.x);
                        const glm::vec2 d = p - lf.Base;
                        const float along = glm::dot(d, dir) / lf.Length;    // 0 taban .. 1 uç
                        const float across = glm::dot(d, nrm) / (lf.Width * 0.5f);
                        if (along < 0.0f || along > 1.0f)
                            continue;
                        // Yaprak biçimi: tabanda dar, ortada geniş, uçta sivri
                        const float profile = std::sin(std::pow(along, 0.8f) * kPi) * (1.0f - 0.25f * along);
                        const float edge = profile - std::fabs(across);
                        if (edge <= 0.0f)
                            continue;
                        const float aa = glm::clamp(edge * lf.Width * 0.5f * N * 0.9f, 0.0f, 1.0f);
                        const float vein = std::fabs(across) < 0.07f ? 0.82f : 1.0f;
                        const glm::vec3 base = glm::mix(glm::vec3(0.22f, 0.40f, 0.06f), glm::vec3(0.46f, 0.62f, 0.13f), along * 0.6f + 0.2f * (1.0f - std::fabs(across)));
                        const glm::vec3 col = glm::clamp(base * lf.Shade * vein, 0.0f, 1.0f);
                        out = glm::vec4(glm::mix(glm::vec3(out), col, out.a > 0.0f ? aa : 1.0f), std::max(out.a, aa));
                    }
                }
            std::vector<unsigned char> px((size_t)N * N * 4);
            for (size_t i = 0; i < img.size(); ++i)
            {
                px[i * 4 + 0] = (unsigned char)(img[i].b * 255.0f);
                px[i * 4 + 1] = (unsigned char)(img[i].g * 255.0f);
                px[i * 4 + 2] = (unsigned char)(img[i].r * 255.0f);
                px[i * 4 + 3] = (unsigned char)(img[i].a * 255.0f);
            }
            s_Tex = Texture2D::CreateFromRawBGRA(N, N, px.data());
            return s_Tex;
        }

        std::shared_ptr<Material> DefaultBark()
        {
            static std::shared_ptr<Material>& s_Mat = *new std::shared_ptr<Material>();
            if (!s_Mat)
            {
                s_Mat = Material::CreateDefault();
                s_Mat->Name = "Tree Bark";
                s_Mat->AlbedoMap = BarkTexture();
                s_Mat->Roughness = 0.92f;
            }
            return s_Mat;
        }

        std::shared_ptr<Material> DefaultLeaves()
        {
            static std::shared_ptr<Material>& s_Mat = *new std::shared_ptr<Material>();
            if (!s_Mat)
            {
                s_Mat = Material::CreateDefault();
                s_Mat->Name = "Tree Leaves";
                s_Mat->AlbedoMap = LeafTexture();
                s_Mat->Rendering = Material::RenderMode::Cutout;
                s_Mat->AlphaClip = 0.4f;
                s_Mat->Culling = Material::CullMode::Off;
                s_Mat->Roughness = 0.7f;
            }
            return s_Mat;
        }

        std::shared_ptr<Material> GroupMaterial(const TreeGroup& g)
        {
            if (!g.Material.empty())
                if (auto mat = MaterialAsset::Load(SceneSerializer::ResolveAssetPath(g.Material)))
                    return mat;
            return g.Type == TreeGroup::Kind::Leaf ? DefaultLeaves() : DefaultBark();
        }

        // ── Üretim ──

        struct Branch
        {
            int Group = 0;
            std::vector<glm::vec3> P, T, N;     // eksen noktaları (eşit aralıklı), teğet, normal (paralel taşıma)
            std::vector<float> R;               // yarıçap
            float Length = 1.0f;
            // Rüzgâr eğilme ağırlığı: dalın başında (üst dalın o noktadaki ağırlığı) ve ucunda (0 gövde dibi .. 1 en esnek uç)
            float BaseWeight = 0.0f, TipWeight = 0.0f;
            int Depth = 0;                      // 0 gövde, 1 dal, 2+ ince dal
        };

        float WeightAt(const Branch& b, float t)
        {
            t = glm::clamp(t, 0.0f, 1.0f);
            return glm::mix(b.BaseWeight, b.TipWeight, t * t);
        }

        // Derinliğe göre esneklik: gövde az, dallar orta, ince dallar çok eğilir; grubun Main Wind'i çarpar
        float Flex(int depth, float mainWind)
        {
            const float level = depth <= 0 ? 0.22f : depth == 1 ? 0.55f : 0.8f;
            return glm::clamp(level * std::max(mainWind, 0.0f), 0.0f, 1.0f);
        }

        void Sample(const Branch& b, float t, glm::vec3& p, glm::vec3& tan, glm::vec3& nor, float& r)
        {
            const float f = glm::clamp(t, 0.0f, 1.0f) * (float)(b.P.size() - 1);
            const size_t i = std::min((size_t)f, b.P.size() - 2);
            const float u = f - (float)i;
            p = glm::mix(b.P[i], b.P[i + 1], u);
            tan = glm::normalize(glm::mix(b.T[i], b.T[i + 1], u));
            glm::vec3 n = glm::mix(b.N[i], b.N[i + 1], u);
            n -= tan * glm::dot(n, tan);
            nor = glm::length(n) > 1e-5f ? glm::normalize(n) : b.N[i];
            r = glm::mix(b.R[i], b.R[i + 1], u);
        }

        glm::vec3 AnyPerpendicular(const glm::vec3& t)
        {
            const glm::vec3 ref = std::fabs(t.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            return glm::normalize(glm::cross(ref, t));
        }

        Branch Grow(const TreeGroup& g, int groupIndex, glm::vec3 start, glm::vec3 dir, glm::vec3 normal, float length, float radius, float lod, Rng& rng)
        {
            Branch b;
            b.Group = groupIndex;
            b.Length = std::max(length, 0.01f);
            const float stepLen = glm::mix(1.1f, 0.22f, glm::clamp(lod, 0.0f, 1.0f));
            const int segs = std::clamp((int)std::round(b.Length / stepLen), 3, 40);
            const float step = b.Length / (float)segs;
            dir = glm::normalize(dir);
            normal -= dir * glm::dot(normal, dir);
            normal = glm::length(normal) > 1e-4f ? glm::normalize(normal) : AnyPerpendicular(dir);
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 p = start;
            for (int j = 0; j <= segs; ++j)
            {
                const float s = (float)j / (float)segs;
                if (j > 0)
                {
                    // Kıvrım (rastgele) + güneşe yönelme / sarkma, metre başına
                    const glm::vec3 bend = rng.Unit() * (g.Crinkliness * 0.9f) + up * (g.SeekSun * 0.45f);
                    glm::vec3 nd = dir + bend * step;
                    if (glm::length(nd) > 1e-4f)
                        dir = glm::normalize(nd);
                    p += dir * step;
                    normal -= dir * glm::dot(normal, dir);
                    normal = glm::length(normal) > 1e-4f ? glm::normalize(normal) : AnyPerpendicular(dir);
                }
                float r = radius * glm::mix(1.0f, glm::clamp(g.CapTaper, 0.0f, 1.0f), s);
                if (g.FlareRadius > 0.0f)
                    r *= 1.0f + g.FlareRadius * std::exp(-s / std::max(g.FlareHeight, 0.005f) * 2.5f);
                b.P.push_back(p);
                b.T.push_back(dir);
                b.N.push_back(normal);
                b.R.push_back(std::max(r, 0.002f));
            }
            // Teğetler: komşu noktaların farkı (daha düzgün halkalar)
            for (size_t j = 0; j < b.P.size(); ++j)
            {
                const glm::vec3 a = b.P[j == 0 ? 0 : j - 1], c = b.P[std::min(j + 1, b.P.size() - 1)];
                if (glm::length(c - a) > 1e-5f)
                    b.T[j] = glm::normalize(c - a);
            }
            return b;
        }

        struct Builder
        {
            std::vector<Vertex> BarkV, LeafV;
            std::vector<uint32_t> BarkI, LeafI;
            // Grup başına ayrı ağ (malzemesi farklıysa); anahtar: malzeme
            std::unordered_map<const Material*, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> ByMaterial;
            std::vector<std::shared_ptr<Material>> MaterialOrder;

            std::pair<std::vector<Vertex>, std::vector<uint32_t>>& Target(const std::shared_ptr<Material>& mat)
            {
                auto it = ByMaterial.find(mat.get());
                if (it == ByMaterial.end())
                {
                    MaterialOrder.push_back(mat);
                    it = ByMaterial.emplace(mat.get(), std::pair<std::vector<Vertex>, std::vector<uint32_t>>{}).first;
                }
                return it->second;
            }
        };

        Vertex MakeVertex(const glm::vec3& p, const glm::vec3& n, const glm::vec3& t, const glm::vec2& uv)
        {
            Vertex v{};
            v.Position = p;
            v.Normal = n;
            v.Tangent = t;
            v.TexCoords = uv;
            v.TexCoords2 = glm::vec2(0.0f);
            return v;
        }

        // Dal tüpü: her eksen noktasında halka, uçta kapak. Gürültü yarıçapı halka boyunca yumuşakça bozar
        void EmitBranch(const Branch& b, const TreeGroup& g, float lod, uint32_t branchSeed, std::vector<Vertex>& V, std::vector<uint32_t>& I)
        {
            const float r0 = b.R.front();
            const int sides = std::clamp((int)std::round(glm::mix(4.0f, 14.0f, glm::clamp(lod, 0.0f, 1.0f)) * std::sqrt(std::max(r0, 0.004f) / 0.25f)), 3, 20);
            const float uRepeat = std::max(1.0f, std::round(2.0f * kPi * r0 / 0.7f));
            const float vScale = uRepeat / (2.0f * kPi * std::max(r0, 0.01f));   // dokular kare kalsın
            const uint32_t base = (uint32_t)V.size();
            float along = 0.0f;
            for (size_t j = 0; j < b.P.size(); ++j)
            {
                if (j > 0)
                    along += glm::length(b.P[j] - b.P[j - 1]);
                const glm::vec3 T = b.T[j];
                glm::vec3 N = b.N[j] - T * glm::dot(b.N[j], T);
                N = glm::length(N) > 1e-5f ? glm::normalize(N) : AnyPerpendicular(T);
                const glm::vec3 B = glm::cross(T, N);
                for (int k = 0; k <= sides; ++k)
                {
                    const float a = 2.0f * kPi * (float)(k % sides) / (float)sides;
                    const glm::vec3 radial = N * std::cos(a) + B * std::sin(a);
                    // Halka boyunca periyodik (dikiş yeri düzgün), boy boyunca metre başına 1.6 hücre
                    const int period = std::max(2, sides / 2);
                    const float n = g.Noise > 0.0f ? (PeriodicNoise((float)(k % sides) * (float)period / (float)sides, along * 1.6f, period, 1 << 20, branchSeed) - 0.5f) : 0.0f;
                    const float r = b.R[j] * (1.0f + g.Noise * 0.55f * n);
                    const glm::vec3 tangent = -N * std::sin(a) + B * std::cos(a);
                    Vertex vx = MakeVertex(b.P[j] + radial * r, radial, tangent, glm::vec2((float)k / (float)sides * uRepeat, along * vScale));
                    // Rüzgâr: x eğilme ağırlığı, y ince dal ucu titremesi (Main Turbulence)
                    const float s01 = (float)j / (float)(b.P.size() - 1);
                    vx.TexCoords2 = glm::vec2(WeightAt(b, s01), b.Depth >= 2 ? s01 * 0.35f * std::max(g.MainTurbulence, 0.0f) : 0.0f);
                    V.push_back(vx);
                }
            }
            const uint32_t ring = (uint32_t)sides + 1;
            for (uint32_t j = 0; j + 1 < (uint32_t)b.P.size(); ++j)
                for (uint32_t k = 0; k < (uint32_t)sides; ++k)
                {
                    const uint32_t a = base + j * ring + k, bb = a + 1, c = a + ring + 1, d = a + ring;
                    I.insert(I.end(), { a, bb, c, a, c, d });
                }
            // Uç kapağı
            const glm::vec3 tip = b.P.back(), tipT = b.T.back();
            const uint32_t center = (uint32_t)V.size();
            Vertex cap = MakeVertex(tip + tipT * b.R.back() * 0.5f, tipT, AnyPerpendicular(tipT), glm::vec2(0.5f * uRepeat, along * vScale));
            cap.TexCoords2 = glm::vec2(b.TipWeight, b.Depth >= 2 ? 0.35f * std::max(g.MainTurbulence, 0.0f) : 0.0f);
            V.push_back(cap);
            const uint32_t last = base + (uint32_t)(b.P.size() - 1) * ring;
            for (uint32_t k = 0; k < (uint32_t)sides; ++k)
                I.insert(I.end(), { last + k, last + k + 1, center });
        }

        // Yaprak kartı: taban dal üzerinde, uzunluk ekseni L, yüz normali n. Köşe normalleri taç merkezinden dışa doğru
        // bükülür (yumuşak, top gibi ışık); çift yüzlü çizilir
        void EmitLeafQuad(const glm::vec3& basePos, const glm::vec3& L, const glm::vec3& n, float size, const glm::vec3& crownCenter,
                          float bendWeight, float flutter, std::vector<Vertex>& V, std::vector<uint32_t>& I)
        {
            const glm::vec3 W = glm::normalize(glm::cross(L, n));
            const glm::vec3 faceN = glm::normalize(glm::cross(W, L));
            const glm::vec3 corners[4] = { basePos - W * size * 0.5f, basePos + W * size * 0.5f,
                                           basePos + W * size * 0.5f + L * size, basePos - W * size * 0.5f + L * size };
            const glm::vec2 uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
            const uint32_t base = (uint32_t)V.size();
            for (int i = 0; i < 4; ++i)
            {
                glm::vec3 out = corners[i] - crownCenter;
                out = glm::length(out) > 1e-4f ? glm::normalize(out) : faceN;
                glm::vec3 nn = glm::normalize(glm::mix(faceN, out, 0.6f));
                Vertex vx = MakeVertex(corners[i], nn, W, uv[i]);
                // Rüzgâr: kart dalla birlikte eğilir; uç köşeler taban köşelerinden çok titrer
                vx.TexCoords2 = glm::vec2(bendWeight, flutter * (i >= 2 ? 1.0f : 0.3f));
                V.push_back(vx);
            }
            I.insert(I.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }

        void WriteVec2(YAML::Emitter& out, const char* key, const glm::vec2& v)
        {
            out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        }

        void ReadVec2(const YAML::Node& n, const char* key, glm::vec2& v)
        {
            if (auto s = n[key]; s && s.IsSequence() && s.size() >= 2)
                v = { s[0].as<float>(), s[1].as<float>() };
        }

        template<typename T>
        void Read(const YAML::Node& n, const char* key, T& v)
        {
            if (auto s = n[key])
                v = s.as<T>();
        }

        // Ağacı silinmiş Mesh Renderer'lardaki eski ağaç modeli temizlenir (sahne başına önceki karenin ağaç varlıkları)
        std::unordered_map<const Scene*, std::vector<uint32_t>>& LastTrees()
        {
            static std::unordered_map<const Scene*, std::vector<uint32_t>> s_Last;
            return s_Last;
        }
    }

    TreeComponent::TreeComponent()
    {
        TreeCreator::MakeDefault(*this);
    }

    namespace TreeCreator
    {
        TreeGroup MakeGroup(TreeGroup::Kind kind, int parent, const std::string& name)
        {
            TreeGroup g;
            g.Name = name;
            g.Type = kind;
            g.Parent = parent;
            if (kind == TreeGroup::Kind::Leaf)
            {
                g.Frequency = 10;
                g.Range = { 0.15f, 1.0f };
                g.GrowthScale = { 1.0f, 0.8f };
                g.GrowthAngle = { 45.0f, 30.0f };
                g.Size = { 0.7f, 1.0f };
            }
            else if (parent >= 0)
            {
                g.Frequency = 6;
                g.Range = { 0.3f, 0.95f };
                g.GrowthScale = { 1.0f, 0.5f };
                g.GrowthAngle = { 60.0f, 40.0f };
                g.Length = { 0.35f, 0.55f };
                g.RelativeLength = true;
                g.Radius = 0.08f;
                g.CapTaper = 0.15f;
                g.Crinkliness = 0.2f;
                g.SeekSun = 0.2f;
            }
            return g;
        }

        void MakeDefault(TreeComponent& tree)
        {
            tree.Groups.clear();
            TreeGroup trunk = MakeGroup(TreeGroup::Kind::Branch, -1, "Trunk");
            trunk.Seed = 3;
            trunk.Length = { 6.5f, 7.5f };
            trunk.Radius = 0.3f;
            trunk.CapTaper = 0.22f;
            trunk.Crinkliness = 0.08f;
            trunk.SeekSun = 0.08f;
            trunk.Noise = 0.18f;
            trunk.FlareRadius = 0.55f;
            trunk.FlareHeight = 0.07f;
            tree.Groups.push_back(trunk);

            TreeGroup branches = MakeGroup(TreeGroup::Kind::Branch, 0, "Branches");
            branches.Seed = 5;
            branches.Frequency = 12;
            branches.Range = { 0.32f, 0.95f };
            branches.GrowthScale = { 1.0f, 0.45f };
            branches.GrowthAngle = { 68.0f, 40.0f };
            branches.Length = { 0.42f, 0.58f };
            branches.Radius = 0.13f;
            branches.CapTaper = 0.12f;
            branches.Crinkliness = 0.22f;
            branches.SeekSun = 0.22f;
            branches.Noise = 0.12f;
            tree.Groups.push_back(branches);

            TreeGroup twigs = MakeGroup(TreeGroup::Kind::Branch, 1, "Twigs");
            twigs.Seed = 7;
            twigs.Frequency = 7;
            twigs.Range = { 0.25f, 1.0f };
            twigs.GrowthScale = { 1.0f, 0.6f };
            twigs.GrowthAngle = { 52.0f, 40.0f };
            twigs.Length = { 0.35f, 0.6f };
            twigs.Radius = 0.04f;
            twigs.CapTaper = 0.2f;
            twigs.Crinkliness = 0.3f;
            twigs.SeekSun = 0.15f;
            twigs.Noise = 0.0f;
            tree.Groups.push_back(twigs);

            TreeGroup leaves = MakeGroup(TreeGroup::Kind::Leaf, 2, "Leaves");
            leaves.Seed = 9;
            leaves.Frequency = 10;
            tree.Groups.push_back(leaves);
            tree.SelectedGroup = 0;
        }

        uint64_t Hash(const TreeComponent& tree)
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](const void* data, size_t size) {
                const auto* p = static_cast<const unsigned char*>(data);
                for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
            };
            auto f = [&](float v) { mix(&v, sizeof v); };
            auto i = [&](int v) { mix(&v, sizeof v); };
            auto v2 = [&](const glm::vec2& v) { f(v.x); f(v.y); };
            i(tree.Seed); f(tree.LODQuality); f(tree.AreaSpread); f(tree.GroundOffset); i((int)tree.Groups.size());
            for (const auto& g : tree.Groups)
            {
                i((int)g.Type); i(g.Parent); i(g.Seed); i(g.Frequency); i((int)g.Mode); i(g.Whorl); f(g.Twirl);
                v2(g.Range); v2(g.GrowthScale); v2(g.GrowthAngle); v2(g.Length); i(g.RelativeLength ? 1 : 0); f(g.Radius);
                f(g.CapTaper); f(g.Crinkliness); f(g.SeekSun); f(g.Noise); f(g.FlareRadius); f(g.FlareHeight); v2(g.Size);
                i((int)g.Shape); f(g.PerpendicularAlign); f(g.HorizontalAlign); f(g.MainWind); f(g.MainTurbulence); f(g.EdgeTurbulence);
                mix(g.Material.data(), g.Material.size()); i(0x5a5a);
            }
            return h == 0 ? 1 : h;
        }

        std::shared_ptr<Model> Generate(const TreeComponent& tree, uint32_t* vertexCount, uint32_t* triangleCount)
        {
            const float lod = glm::clamp(tree.LODQuality, 0.0f, 1.0f);
            std::vector<Branch> branches;
            struct LeafCard { glm::vec3 Base, L, N; float Size; int Group; float Weight; };
            std::vector<LeafCard> cards;
            const glm::vec3 up(0.0f, 1.0f, 0.0f);

            for (int gi = 0; gi < (int)tree.Groups.size(); ++gi)
            {
                const TreeGroup& g = tree.Groups[gi];
                Rng rng((uint32_t)tree.Seed * 7919u + (uint32_t)g.Seed * 104729u + (uint32_t)gi * 31u);
                const int freq = std::clamp(g.Frequency, 0, 512);
                if (g.Parent < 0 || g.Parent >= gi)
                {
                    // Kökten çıkan grup: gövde(ler). Birden çoksa Area Spread yarıçapında dağılır
                    if (g.Type == TreeGroup::Kind::Leaf)
                        continue;
                    for (int k = 0; k < freq; ++k)
                    {
                        glm::vec3 start(0.0f, -tree.GroundOffset, 0.0f);
                        if (tree.AreaSpread > 0.0f && (freq > 1 || k > 0))
                        {
                            const float a = rng.Range(0.0f, 2.0f * kPi), d = std::sqrt(rng.Float()) * tree.AreaSpread;
                            start += glm::vec3(std::cos(a) * d, 0.0f, std::sin(a) * d);
                        }
                        const float tilt = glm::radians(glm::mix(g.GrowthAngle.x, g.GrowthAngle.y, rng.Float()));
                        const float az = rng.Range(0.0f, 2.0f * kPi);
                        const glm::vec3 dir = glm::normalize(up * std::cos(tilt) + glm::vec3(std::cos(az), 0.0f, std::sin(az)) * std::sin(tilt));
                        const float scale = glm::mix(g.GrowthScale.x, g.GrowthScale.y, rng.Float());
                        const float length = rng.Range(std::min(g.Length.x, g.Length.y), std::max(g.Length.x, g.Length.y)) * scale;
                        Branch trunk = Grow(g, gi, start, dir, glm::vec3(1.0f, 0.0f, 0.0f), length, g.Radius * scale, lod, rng);
                        trunk.Depth = 0;
                        trunk.BaseWeight = 0.0f;
                        trunk.TipWeight = Flex(0, g.MainWind);
                        branches.push_back(std::move(trunk));
                    }
                    continue;
                }
                // Üst grubun her dalına
                const size_t parentCount = branches.size();
                for (size_t pi = 0; pi < parentCount; ++pi)
                {
                    if (branches[pi].Group != g.Parent)
                        continue;
                    // Not: branches büyüyebilir; üst dalı kopyalayarak kullan (referans geçersizleşmesin)
                    const Branch parent = branches[pi];
                    const int perRing = g.Mode == TreeGroup::Distribution::Whorled ? std::max(1, g.Whorl)
                                      : g.Mode == TreeGroup::Distribution::Opposite ? 2 : 1;
                    const int rings = std::max(1, (freq + perRing - 1) / perRing);
                    const float az0 = rng.Range(0.0f, 2.0f * kPi);
                    for (int k = 0; k < freq; ++k)
                    {
                        float t, az;
                        if (g.Mode == TreeGroup::Distribution::Random)
                        {
                            t = rng.Range(g.Range.x, g.Range.y);
                            az = rng.Range(0.0f, 2.0f * kPi);
                        }
                        else
                        {
                            const int ringIndex = k / perRing, inRing = k % perRing;
                            t = glm::mix(g.Range.x, g.Range.y, ((float)ringIndex + 0.5f) / (float)rings);
                            float step = g.Mode == TreeGroup::Distribution::Alternate ? kPi : 2.0f * kPi / (float)perRing;
                            az = az0 + (float)inRing * step;
                            // Alternate: her dal karşı yana; Opposite / Whorled: halkalar arası çeyrek / yarım adım
                            if (g.Mode == TreeGroup::Distribution::Alternate)
                                az = az0 + (float)k * kPi;
                            else
                                az += (float)ringIndex * (step * 0.5f);
                            az += glm::radians(g.Twirl) * (float)(g.Mode == TreeGroup::Distribution::Alternate ? k : ringIndex);
                        }
                        t = glm::clamp(t, 0.0f, 1.0f);
                        glm::vec3 pp, pt, pn;
                        float pr;
                        Sample(parent, t, pp, pt, pn, pr);
                        const glm::vec3 pb = glm::cross(pt, pn);
                        const glm::vec3 radial = pn * std::cos(az) + pb * std::sin(az);
                        const float scale = glm::mix(g.GrowthScale.x, g.GrowthScale.y, t);
                        const float angle = glm::radians(glm::mix(g.GrowthAngle.x, g.GrowthAngle.y, t));
                        if (g.Type == TreeGroup::Kind::Branch)
                        {
                            const glm::vec3 dir = glm::normalize(pt * std::cos(angle) + radial * std::sin(angle));
                            const float len = rng.Range(std::min(g.Length.x, g.Length.y), std::max(g.Length.x, g.Length.y)) *
                                              (g.RelativeLength ? parent.Length : 1.0f) * scale;
                            const float radius = std::min(g.Radius * scale, pr * 0.85f);
                            // Dal üst dalın içinden başlar: ek yeri boşluksuz
                            const glm::vec3 start = pp + radial * pr * 0.35f;
                            Branch child = Grow(g, gi, start, dir, pt, len, radius, lod, rng);
                            child.Depth = parent.Depth + 1;
                            child.BaseWeight = WeightAt(parent, t);
                            child.TipWeight = child.BaseWeight + (1.0f - child.BaseWeight) * Flex(child.Depth, g.MainWind);
                            branches.push_back(std::move(child));
                        }
                        else
                        {
                            // Yaprak: dışa doğru, üst dalın yönüne biraz eğik; yüzü L çevresinde rastgele, hizalamalarla
                            glm::vec3 L = glm::normalize(pt * std::cos(angle) + radial * std::sin(angle) + rng.Unit() * 0.25f);
                            glm::vec3 n = glm::normalize(glm::cross(L, AnyPerpendicular(L)));
                            const float spin = rng.Range(0.0f, 2.0f * kPi);
                            const glm::vec3 w0 = glm::normalize(glm::cross(L, n));
                            n = glm::normalize(n * std::cos(spin) + w0 * std::sin(spin));
                            if (g.HorizontalAlign > 0.0f)
                            {
                                n = glm::mix(n, up, glm::clamp(g.HorizontalAlign, 0.0f, 1.0f));
                                L = glm::mix(L, glm::normalize(L - up * glm::dot(L, up) + glm::vec3(1e-4f, 0.0f, 0.0f)), glm::clamp(g.HorizontalAlign, 0.0f, 1.0f));
                            }
                            if (g.PerpendicularAlign > 0.0f)
                                n = glm::mix(n, pt, glm::clamp(g.PerpendicularAlign, 0.0f, 1.0f));
                            L = glm::normalize(L);
                            n -= L * glm::dot(n, L);
                            n = glm::length(n) > 1e-4f ? glm::normalize(n) : AnyPerpendicular(L);
                            const float size = rng.Range(std::min(g.Size.x, g.Size.y), std::max(g.Size.x, g.Size.y)) * scale;
                            cards.push_back({ pp + radial * pr, L, n, size, gi, WeightAt(parent, t) });
                        }
                    }
                }
            }

            // Taç merkezi (yaprak normallerini dışa bükmek için)
            glm::vec3 crown(0.0f);
            if (!cards.empty())
            {
                for (const auto& c : cards) crown += c.Base + c.L * c.Size * 0.5f;
                crown /= (float)cards.size();
            }

            Builder build;
            for (size_t bi = 0; bi < branches.size(); ++bi)
            {
                const TreeGroup& g = tree.Groups[branches[bi].Group];
                auto& target = build.Target(GroupMaterial(g));
                EmitBranch(branches[bi], g, lod, HashInt((uint32_t)bi * 2654435761u + (uint32_t)tree.Seed), target.first, target.second);
            }
            for (const auto& c : cards)
            {
                const TreeGroup& g = tree.Groups[c.Group];
                auto& target = build.Target(GroupMaterial(g));
                const int planes = g.Shape == TreeGroup::LeafShape::TriCross ? 3 : g.Shape == TreeGroup::LeafShape::Cross ? 2 : 1;
                for (int p = 0; p < planes; ++p)
                {
                    const float a = kPi * (float)p / (float)planes;
                    const glm::vec3 w = glm::normalize(glm::cross(c.L, c.N));
                    const glm::vec3 n = glm::normalize(c.N * std::cos(a) + w * std::sin(a));
                    EmitLeafQuad(c.Base, c.L, n, c.Size, crown, c.Weight, std::max(g.EdgeTurbulence, 0.0f), target.first, target.second);
                }
            }

            auto model = std::make_shared<Model>();
            model->Directory = kTreeModelTag;
            uint32_t verts = 0, tris = 0;
            for (const auto& mat : build.MaterialOrder)
            {
                auto& data = build.ByMaterial[mat.get()];
                if (data.second.empty())
                    continue;
                verts += (uint32_t)data.first.size();
                tris += (uint32_t)data.second.size() / 3;
                model->Meshes.push_back(std::make_shared<Mesh>(data.first, data.second));
                model->Materials.push_back(mat);
            }
            if (vertexCount) *vertexCount = verts;
            if (triangleCount) *triangleCount = tris;
            return model;
        }

        void Update(Scene& scene)
        {
            auto& reg = scene.m_Registry;
            auto& last = LastTrees()[&scene];
            std::vector<uint32_t> now;
            for (auto h : reg.view<TreeComponent>())
            {
                auto& tree = reg.get<TreeComponent>(h);
                const uint64_t hash = Hash(tree);
                if (hash != tree.BuiltHash || !tree.Generated)
                {
                    tree.Generated = Generate(tree, &tree.VertexCount, &tree.TriangleCount);
                    tree.BuiltHash = hash;
                    // Ağacın boyu: rüzgâr salınımının ölçeği
                    float lo = 1e30f, hi = -1e30f;
                    for (const auto& mesh : tree.Generated->Meshes)
                        for (const auto& v : mesh->GetVertices())
                        {
                            lo = std::min(lo, v.Position.y);
                            hi = std::max(hi, v.Position.y);
                        }
                    tree.Height = hi > lo ? hi - lo : 1.0f;
                }
                Entity e{ h, &scene };
                auto& mr = e.HasComponent<MeshRendererComponent>() ? e.GetComponent<MeshRendererComponent>() : e.AddComponent<MeshRendererComponent>();
                if (mr.ModelPath.empty() && mr.ModelData != tree.Generated)
                    mr.ModelData = tree.Generated;
                now.push_back((uint32_t)h);
            }
            // Ağaç bileşeni kaldırılmış varlıklar: Mesh Renderer'daki üretilmiş ağaç bırakılır (eski ağaç kalmasın)
            for (uint32_t id : last)
            {
                const auto h = (entt::entity)id;
                if (!reg.valid(h) || reg.all_of<TreeComponent>(h) || !reg.all_of<MeshRendererComponent>(h))
                    continue;
                auto& mr = reg.get<MeshRendererComponent>(h);
                if (mr.ModelPath.empty() && mr.ModelData && mr.ModelData->Directory == kTreeModelTag)
                    mr.ModelData.reset();
            }
            last.swap(now);
        }

        void RemoveGroup(TreeComponent& tree, int index)
        {
            if (index < 0 || index >= (int)tree.Groups.size())
                return;
            // Silinecekler: grup ve tüm alt grupları (üst her zaman önce geldiği için tek geçiş yeter)
            std::vector<bool> remove(tree.Groups.size(), false);
            remove[index] = true;
            for (size_t i = (size_t)index + 1; i < tree.Groups.size(); ++i)
                if (tree.Groups[i].Parent >= 0 && remove[(size_t)tree.Groups[i].Parent])
                    remove[i] = true;
            std::vector<int> remap(tree.Groups.size(), -1);
            std::vector<TreeGroup> kept;
            for (size_t i = 0; i < tree.Groups.size(); ++i)
                if (!remove[i])
                {
                    remap[i] = (int)kept.size();
                    kept.push_back(tree.Groups[i]);
                }
            for (auto& g : kept)
                g.Parent = g.Parent >= 0 ? remap[(size_t)g.Parent] : -1;
            tree.Groups = std::move(kept);
            tree.SelectedGroup = std::clamp(tree.SelectedGroup, 0, std::max(0, (int)tree.Groups.size() - 1));
        }

        bool WindFor(Scene& scene, entt::entity entity, glm::vec4& wind, glm::vec2& params)
        {
            wind = glm::vec4(0.0f);
            params = glm::vec2(0.0f);
            auto& reg = scene.m_Registry;
            const auto* tree = reg.try_get<TreeComponent>(entity);
            const auto* mr = reg.try_get<MeshRendererComponent>(entity);
            const auto* tc = reg.try_get<TransformComponent>(entity);
            if (!tree || !mr || !tc || !tree->Generated || mr->ModelData != tree->Generated)
                return false;
            glm::vec3 main(0.0f);
            float turbulence = 0.0f;
            GameplaySystems::SampleWindParts(glm::vec3(tc->WorldTransform[3]), main, turbulence);
            if (glm::dot(main, main) < 1e-8f && turbulence < 1e-4f)
                return false;
            // Ağacın dünya boyu (ölçekli nesne daha çok salınır)
            const float scale = glm::length(glm::vec3(tc->WorldTransform[1]));
            wind = glm::vec4(main, turbulence);
            params = glm::vec2(1.0f, std::max(tree->Height * scale, 0.5f));
            return true;
        }

        int DuplicateGroup(TreeComponent& tree, int index)
        {
            if (index < 0 || index >= (int)tree.Groups.size())
                return -1;
            TreeGroup copy = tree.Groups[index];
            copy.Name += " Copy";
            copy.Seed += 1;
            tree.Groups.push_back(copy);
            return (int)tree.Groups.size() - 1;
        }
    }

    template<> void Scene::OnComponentAdded<TreeComponent>(Entity, TreeComponent&) {}

    void RegisterTreeComponents()
    {
        using C = TreeComponent;
        ComponentDef<C>("Tree", "TreeComponent", "Rendering", ICON_FA_TREE)
            .Describe("Procedural tree (Unity Tree Creator): branch and leaf groups grown from a seed.\n"
                      "The generated bark and leaves are drawn by this object's Mesh Renderer.")
            .Requires("MeshRendererComponent")
            .Field("Seed", "Seed", &C::Seed).Range(0.0f, 999999.0f, 1.0f)
            .Field("LODQuality", "LOD Quality", &C::LODQuality).Range(0.0f, 1.0f, 0.01f)
                .Tooltip("Rings around and segments along every branch.")
            .Field("AreaSpread", "Area Spread", &C::AreaSpread).Range(0.0f, 100.0f, 0.05f).Format("%.2f m")
                .Tooltip("Radius over which several trunks are spread.")
            .Field("GroundOffset", "Ground Offset", &C::GroundOffset).Range(-10.0f, 10.0f, 0.01f).Format("%.2f m")
                .Tooltip("How far the trunk starts below the object's origin.")
            .CustomData(
                [](YAML::Emitter& out, C& t) {
                    out << YAML::Key << "Groups" << YAML::Value << YAML::BeginSeq;
                    for (const auto& g : t.Groups)
                    {
                        out << YAML::BeginMap;
                        out << YAML::Key << "Name" << YAML::Value << g.Name;
                        out << YAML::Key << "Type" << YAML::Value << (int)g.Type;
                        out << YAML::Key << "Parent" << YAML::Value << g.Parent;
                        out << YAML::Key << "Seed" << YAML::Value << g.Seed;
                        out << YAML::Key << "Frequency" << YAML::Value << g.Frequency;
                        out << YAML::Key << "Distribution" << YAML::Value << (int)g.Mode;
                        out << YAML::Key << "Whorl" << YAML::Value << g.Whorl;
                        out << YAML::Key << "Twirl" << YAML::Value << g.Twirl;
                        WriteVec2(out, "Range", g.Range);
                        WriteVec2(out, "GrowthScale", g.GrowthScale);
                        WriteVec2(out, "GrowthAngle", g.GrowthAngle);
                        if (g.Type == TreeGroup::Kind::Branch)
                        {
                            WriteVec2(out, "Length", g.Length);
                            out << YAML::Key << "RelativeLength" << YAML::Value << g.RelativeLength;
                            out << YAML::Key << "Radius" << YAML::Value << g.Radius;
                            out << YAML::Key << "CapTaper" << YAML::Value << g.CapTaper;
                            out << YAML::Key << "Crinkliness" << YAML::Value << g.Crinkliness;
                            out << YAML::Key << "SeekSun" << YAML::Value << g.SeekSun;
                            out << YAML::Key << "Noise" << YAML::Value << g.Noise;
                            out << YAML::Key << "FlareRadius" << YAML::Value << g.FlareRadius;
                            out << YAML::Key << "FlareHeight" << YAML::Value << g.FlareHeight;
                        }
                        else
                        {
                            WriteVec2(out, "Size", g.Size);
                            out << YAML::Key << "Shape" << YAML::Value << (int)g.Shape;
                            out << YAML::Key << "PerpendicularAlign" << YAML::Value << g.PerpendicularAlign;
                            out << YAML::Key << "HorizontalAlign" << YAML::Value << g.HorizontalAlign;
                        }
                        if (!g.Material.empty())
                            out << YAML::Key << "Material" << YAML::Value << g.Material;
                        out << YAML::Key << "MainWind" << YAML::Value << g.MainWind;
                        out << YAML::Key << "MainTurbulence" << YAML::Value << g.MainTurbulence;
                        out << YAML::Key << "EdgeTurbulence" << YAML::Value << g.EdgeTurbulence;
                        out << YAML::EndMap;
                    }
                    out << YAML::EndSeq;
                },
                [](const YAML::Node& n, C& t) {
                    auto list = n["Groups"];
                    if (!list || !list.IsSequence())
                        return;
                    t.Groups.clear();
                    for (const auto& gn : list)
                    {
                        TreeGroup g;
                        int type = 0, mode = 0, shape = 0;
                        Read(gn, "Name", g.Name);
                        Read(gn, "Type", type);
                        g.Type = (TreeGroup::Kind)std::clamp(type, 0, 1);
                        Read(gn, "Parent", g.Parent);
                        Read(gn, "Seed", g.Seed);
                        Read(gn, "Frequency", g.Frequency);
                        Read(gn, "Distribution", mode);
                        g.Mode = (TreeGroup::Distribution)std::clamp(mode, 0, 3);
                        Read(gn, "Whorl", g.Whorl);
                        Read(gn, "Twirl", g.Twirl);
                        ReadVec2(gn, "Range", g.Range);
                        ReadVec2(gn, "GrowthScale", g.GrowthScale);
                        ReadVec2(gn, "GrowthAngle", g.GrowthAngle);
                        ReadVec2(gn, "Length", g.Length);
                        Read(gn, "RelativeLength", g.RelativeLength);
                        Read(gn, "Radius", g.Radius);
                        Read(gn, "CapTaper", g.CapTaper);
                        Read(gn, "Crinkliness", g.Crinkliness);
                        Read(gn, "SeekSun", g.SeekSun);
                        Read(gn, "Noise", g.Noise);
                        Read(gn, "FlareRadius", g.FlareRadius);
                        Read(gn, "FlareHeight", g.FlareHeight);
                        ReadVec2(gn, "Size", g.Size);
                        Read(gn, "Shape", shape);
                        g.Shape = (TreeGroup::LeafShape)std::clamp(shape, 0, 2);
                        Read(gn, "PerpendicularAlign", g.PerpendicularAlign);
                        Read(gn, "HorizontalAlign", g.HorizontalAlign);
                        Read(gn, "Material", g.Material);
                        Read(gn, "MainWind", g.MainWind);
                        Read(gn, "MainTurbulence", g.MainTurbulence);
                        Read(gn, "EdgeTurbulence", g.EdgeTurbulence);
                        // Üst grup her zaman önce gelir
                        if (g.Parent >= (int)t.Groups.size())
                            g.Parent = -1;
                        t.Groups.push_back(g);
                    }
                    t.SelectedGroup = std::clamp(t.SelectedGroup, 0, std::max(0, (int)t.Groups.size() - 1));
                    t.BuiltHash = 0;
                });
    }
}
