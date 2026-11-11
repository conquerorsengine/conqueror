// Ağaç rüzgârı (Tree Creator + Wind Zone). Ağacın köşe ağırlıkları ikinci UV kanalında: x dal eğilmesi (gövde dibinde 0,
// dal uçlarında 1), y yaprak / ince dal titremesi. PBR, hareket vektörü (TAA) ve gölge geçişleri aynı işlevi kullanır:
// görüntü, gölge ve hareket vektörü birlikte salınır (TAA'da iz kalmaz)
uniform vec4 u_TreeWind = vec4(0.0);         // xyz: rüzgâr (dünya yönü * şiddet), w: türbülans
uniform vec4 u_TreeWindParams = vec4(0.0);   // x: zaman (s), y: 1 = açık, z: yaprak titremesi çarpanı, w: ağaç boyu (m)

vec3 TreeWindOffset(vec3 worldPos, vec3 origin, vec2 weights, float t)
{
    vec3 wind = u_TreeWind.xyz;
    float strength = length(wind);
    vec3 dir = strength > 1e-4 ? wind / strength : vec3(1.0, 0.0, 0.0);
    float turb = u_TreeWind.w;
    vec3 side = normalize(cross(dir, vec3(0.0, 1.0, 0.0)) + vec3(1e-4, 0.0, 0.0));
    // Ana salınım: ağaç rüzgâr yönünde eğilir, rüzgâr güçlenip zayıfladıkça gidip gelir (her ağaçta farklı faz)
    float phase = dot(origin, vec3(0.131, 0.0, 0.173));
    float gust = 0.7 + 0.3 * sin(t * 0.83 + phase) * sin(t * 0.37 + phase * 1.7);
    // Türbülans: dallar kendi başına sallanır (konuma göre faz)
    float sway = sin(t * 1.9 + phase + dot(worldPos, vec3(0.071, 0.023, 0.053)));
    float bend = weights.x;
    float height = max(u_TreeWindParams.w, 0.5);
    vec3 offset = (dir * (strength * gust + turb * sway * 0.25) + side * (turb * sway * 0.2)) * bend * height * 0.035;
    // Eğilen dal biraz aşağı iner (boyu korunur gibi)
    offset.y -= dot(offset.xz, offset.xz) / height;
    // Yaprak kenarı titremesi: hızlı, küçük, her yaprakta farklı
    float flutter = sin(t * 7.3 + dot(worldPos, vec3(2.31, 1.73, 2.93))) * weights.y * u_TreeWindParams.z * (0.35 * strength + 0.65 * turb) * 0.025;
    offset += vec3(0.0, flutter, 0.0) + side * (flutter * 0.6);
    return offset;
}
