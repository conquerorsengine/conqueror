SUMMARY = "Conqueror Engine game player (OpenGL ES 3 on Wayland)"
DESCRIPTION = "Runs games exported with the Conqueror Engine editor. Built runtime-only (no editor, no launcher) \
with OpenGL ES 3.2 as the default graphics profile, for Automotive Grade Linux and other Yocto based images."
HOMEPAGE = "https://github.com/conquerorsengine/conqueror"
LICENSE = "MPL-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b13f17a000ad5c76262a43c1ee67e19a"

UNPACKDIR ??= "${WORKDIR}"
SRC_URI = "gitsm://github.com/conquerorsengine/conqueror.git;protocol=https;branch=main;destsuffix=git"
SRCREV ?= "${AUTOREV}"
PV = "1.0+git"
S = "${UNPACKDIR}/git"

inherit cmake pkgconfig

# GLFW Wayland arka ucu (wayland-scanner, xkbcommon) + EGL / GLES; glslang derleme bilgisi için python3-native
DEPENDS = "wayland wayland-native wayland-protocols libxkbcommon virtual/egl virtual/libgles2 python3-native"

EXTRA_OECMAKE = " \
    -DCQ_RUNTIME_ONLY=ON \
    -DCQ_DEFAULT_GL_PROFILE=es3.2 \
    -DCQ_ENABLE_VULKAN=OFF \
    -DCQ_LINUX_ENGINE_CORE_STATIC=ON \
    -DGLFW_BUILD_X11=OFF \
    -DGLFW_BUILD_WAYLAND=ON \
"

OECMAKE_TARGET_COMPILE = "ConquerorRuntime"

do_install() {
    install -d ${D}${libexecdir}/conqueror
    install -m 0755 ${B}/bin/ConquerorRuntime ${D}${libexecdir}/conqueror/conqueror-player
}

FILES:${PN} = "${libexecdir}/conqueror"
# GLFW Wayland / EGL kitaplıklarını dlopen ile açar: paket bağımlılığı otomatik bulunmaz
RDEPENDS:${PN} += "wayland libxkbcommon"
