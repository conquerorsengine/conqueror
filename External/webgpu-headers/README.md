# webgpu-headers

`include/webgpu/webgpu.h` from https://github.com/webgpu-native/webgpu-headers (revision a1dc77409bb2b8bd80540cbf4b8db219e123e909,
BSD 3-Clause, see LICENSE). The standard WebGPU C API that Dawn (Chrome) and wgpu-native implement.

Conqueror's desktop WebGPU backend (Engine/src/Renderer/GraphicsAPIs/WebGPU) compiles against this header with
`WGPU_SKIP_DECLARATIONS` and opens a WebGPU implementation at run time (libwebgpu_dawn / libwgpu_native), like the Vulkan
loader. The Web player uses Emscripten's `emdawnwebgpu` port header instead.
