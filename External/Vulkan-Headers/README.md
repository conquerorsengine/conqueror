# Vulkan-Headers (vendored)

C headers from the Khronos Vulkan-Headers project (version 1.4.341, Apache-2.0 / MIT, see LICENSE.md).
Only the C API headers are kept (the C++ `vulkan.hpp` bindings are not used).

Conqueror loads the Vulkan runtime dynamically (Engine/src/Renderer/GraphicsAPIs/Vulkan/VulkanLoader.*),
so no Vulkan SDK is needed to build the engine, and executables do not require `vulkan-1.dll` /
`libvulkan.so.1` to start: without a Vulkan driver the engine falls back to OpenGL (or Direct3D 11 on Windows).
