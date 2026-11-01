# Rendering

## Backends

All rendering code is written against OpenGL 4.5. Vulkan, Direct3D 11, Direct3D 12 and Metal are translation layers
that implement that API on top of the native one, so every feature works the same everywhere. Details and selection
rules are in the [README](../README.md#graphics-backends).

| Backend | Default on | Select with |
|---------|-----------|-------------|
| OpenGL 4.6 (4.5 / 4.1 / 3.3, GL ES 3.x profiles) | Linux, Android (ES), automotive (ES) | `--opengl`, `--gl=es3.0` ... |
| Vulkan 1.2 / 1.3 | — | `--vulkan` |
| Direct3D 11 | Windows | `--d3d11` |
| Direct3D 12 | — | `--d3d12` |
| Metal | macOS | `--metal` |

`CQ_RENDERER=<name>` and Project Settings > Player > Graphics API do the same; the command line wins.

### How the translation layers work

```
engine + ImGui (GL calls) -> glad -> GLEmu front end (GL state machine, objects, uniforms)
                                        |-> VkGL  (Vulkan)     shaders: GLSL -> SPIR-V
                                        |-> DxGL  (D3D11)      shaders: GLSL -> SPIR-V -> HLSL -> DXBC
                                        |-> D12GL (D3D12)      shaders: same DXBC, one root signature
                                        '-> MtlGL (Metal)      shaders: GLSL -> SPIR-V -> MSL
```

Shared pieces live in `Engine/src/Renderer/GraphicsAPIs/GLEmu`: the front end (`GLFrontEnd.inl`), shader reflection
(`GLReflect`: uniforms, samplers, blocks, inputs; register assignment) and texel conversion (`GLTexel`). Images are
kept in GL orientation (row 0 = bottom) and flipped once when presented, so `gl_FragCoord`, viewports, read-backs and
render-to-texture behave exactly like OpenGL.

## Lighting

- Physically based shading (metallic / roughness), with Standard, Subsurface, Hair and Cloth shading models.
- Directional, point, spot and area lights; 2D lights with shadow casters for sprites.
- Shadows: cascaded shadow maps for the directional light (1–4 cascades, atlas up to 4096). Quality: Hard,
  Soft (16-tap Poisson PCF) or Very Soft (PCSS-style, variable penumbra). Bias, softness, colour and distance are in the
  Lighting panel.
- Image-based lighting from the skybox; reflection probes; baked lighting (Lighting > Generate).
- Screen-space global illumination (Lumen-style, `r.SSGI`), screen-space ambient occlusion.

## Sky and atmosphere

Sky mode: **Auto** (skybox if set, procedural sky for 3D content), skybox / HDR, or **Atmosphere** (physical sky:
Rayleigh + Mie + ozone). Volumetric clouds (ray-marched, wind-driven; coverage, density, height, thickness, scale) and
volumetric fog (height falloff, anisotropy, sun scattering) are in the Lighting panel.

## Post-processing

Configured per camera / Post Process Volume; each effect has a console variable for quick toggling (see
[Console variables](Reference/ConsoleVariables.md)).

| Effect | Notes |
|--------|-------|
| Tonemapping | Neutral (Khronos PBR Neutral, default), ACES, AgX, Reinhard; exposure in EV |
| Colour grading | contrast, saturation, temperature, tint, lift / gamma / gain |
| Auto exposure | eye adaptation with min / max EV and speed up / down |
| Bloom | threshold, knee, intensity, scatter, tint |
| Ambient occlusion | SSAO (radius, intensity, power) |
| Screen-space reflections | max distance, thickness, roughness cut-off |
| Screen-space GI | half resolution, rays per pixel, radius, temporal history |
| Anti-aliasing | FXAA (default), SMAA or TAA (projection jitter + history) |
| Depth of field | bokeh, focus distance, aperture, focal length |
| Motion blur | camera motion from depth re-projection |
| Vignette, chromatic aberration, film grain, sharpen | |
| Lens flares and halos | occlusion-tested per light |

## Geometry

- Meshes via Assimp (glTF, FBX, OBJ ...), automatic LOD chains (quadric simplification), Nanite-style clustered meshes
  (`VirtualGeometry` component), GPU instancing (MultiMesh), CSG shapes, terrain with sculpt / paint tools, vertex paint,
  decals, billboards, particles, splines.
- 2D: sprites, tilemaps (auto-tiling), 2D lights, pixel-perfect camera, 2D skeletons.

## Visibility and draw order

- **Frustum culling.** Every mesh, model and skinned character is tested against the camera before it is drawn.
  Static meshes use their bounding box (transformed exactly, also with non-uniform scale). Skinned characters use one
  bind-pose box per bone, moved by the current bone matrices, so animated poses (raised arms, root motion) are never cut
  off. Each shadow cascade culls casters against its own light volume, so a cascade only draws what can shadow its area.
  The Stats overlay shows how many objects were skipped (`Culled (off screen)`) and the shadow draw count.
- **Opaque sorting.** Opaque meshes and models are queued and drawn nearest first, so hidden surfaces fail the depth test
  before the expensive lighting runs. The sort distance is the distance to the object's center plus its radius, so big
  floors, walls and ceilings come after the props standing on them. Alpha-tested (cutout) materials such as leaves are
  drawn after the opaque ones, and MultiMesh batches and terrain after those. Transparent objects are drawn last,
  farthest first.
- **World text** (Text component) uses one glyph atlas per font: a whole label is a single draw call.
- In the editor a Game tab that is hidden behind another tab is not rendered while editing (the game keeps running in
  Play mode).
- `r.FrustumCulling 0` and `r.OpaqueSort 0` turn these off for debugging or comparisons.

## Materials

A material is a `.cqmat` file (right-click in the Content Browser > Material). It holds everything about the surface:
base map and color, metallic and roughness (with their own maps or one packed glTF ORM map), normal map, occlusion,
emission, tiling and offset, world-aligned UVs, surface type (opaque, cutout, transparent), render face and shading
model (lit, unlit, subsurface, hair, cloth). Every object that uses the same file shares it, so editing the material
changes all of them, and edits made outside the editor are picked up while it runs.

- **Mesh Renderer.** The component has a mesh, a Material slot and lighting options. Drag a `.cqmat` onto the slot, onto
  the object in the Scene view or onto its row in the Hierarchy, or pick one with the target button. **New** saves the
  current look as `Assets/Materials/<object>.cqmat`. Without a material the renderer uses its built-in material, whose
  values are stored in the scene (this is what the sample templates use).
- **Material Variant.** Right-click a material > Create Material Variant (Unity Material Variant, Unreal Material
  Instance). The variant file stores only the values you change; everything else comes from its parent and follows it.
- **Shader Graph.** Right-click > Shader Graph creates a material driven by a node graph and opens it in the Shader
  Graph editor. Double-clicking any `.cqmat` opens it there too. Inputs wired in the graph override the matching
  values in the Inspector. The graph is compiled when the material loads.
- **From a texture.** Right-click an image > Create Material makes a material with that image as its base map.
- **At runtime.** Changing an object's color from a script or a tween while playing gives that object its own copy of
  the material (like Unity's `renderer.material`); the file and the other objects keep the original.

Renaming or moving a material in the Content Browser updates the scenes, prefabs and variants that refer to it.

## Render targets

A camera with a RenderTarget component renders into a texture that any material can use as `rt://Name` (security
cameras, mirrors, minimaps). A Render Texture asset (right-click > Render Texture, `.cqrt`) stores the size; drop it on
the Render Target component's Target Texture slot and on any texture slot to use it.

## Debugging

| Variable / flag | Effect |
|-----------------|--------|
| `CQ_VK_VALIDATION=1` | Vulkan validation layer |
| `CQ_D3D_DEBUG=1` | D3D11 debug device / D3D12 debug layer |
| `CQ_VKGL_DUMP`, `CQ_DXGL_DUMP`, `CQ_MTLGL_DUMP` `=<dir>` | write the translated shaders |
| Profiler (Ctrl+7) | CPU and GPU timings per pass, draw calls, triangles, texture memory |
| Viewport > Stats | FPS, frame time, draw calls, culled objects, shadow draws |
| `r.FrustumCulling 0` | draw everything (no camera or shadow cascade culling) |
| `r.OpaqueSort 0` | draw opaque meshes in submission order instead of nearest first |
