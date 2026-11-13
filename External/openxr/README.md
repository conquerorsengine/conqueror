# OpenXR headers

`include/openxr/openxr.h`, `openxr_platform.h` and `openxr_platform_defines.h` from the Khronos OpenXR registry (API 1.1.47),
Copyright 2017-2025 The Khronos Group Inc., SPDX-License-Identifier: Apache-2.0 OR MIT (used under the MIT terms).

Only the headers are bundled. The engine loads the OpenXR loader at run time (`libopenxr_loader.so.1` / `openxr_loader.dll`),
which then loads the active runtime (Monado, SteamVR, Meta Quest Link, WMR ...).
