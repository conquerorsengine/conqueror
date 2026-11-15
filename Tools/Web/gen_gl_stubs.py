#!/usr/bin/env python3
"""glad başlığından (External/glad/include/glad/glad.h) WebGL'de olmayan GL işlevleri için yedekler üretir:
Engine/src/Renderer/GraphicsAPIs/OpenGL/GLMissingStubs.cpp

WebGL 2'de bulunmayan masaüstü işlevlerinin glad göstericileri boş kalır (glDispatchCompute, glPolygonMode, glGetTexImage ...);
WebAssembly boş göstericiyi çağırınca sayfa durur ("null function"). Her yedek işlevin imzası glad'daki türle birebir aynıdır
(WebAssembly dolaylı çağrıda imza denetler), ilk çağrıda bir kez uyarı yazar ve sıfır / boş döndürür.

  python3 Tools/Web/gen_gl_stubs.py      (glad yeniden üretildiğinde)
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GLAD = ROOT / "External" / "glad" / "include" / "glad" / "glad.h"
OUT = ROOT / "Engine" / "src" / "Renderer" / "GraphicsAPIs" / "OpenGL" / "GLMissingStubs.cpp"

TYPEDEF = re.compile(r"^typedef (.+?) \(APIENTRYP (PFNGL\w+PROC)\)\((.*)\);\s*$")
GLAPI = re.compile(r"^GLAPI (PFNGL\w+PROC) glad_(gl\w+);\s*$")


def main():
    text = GLAD.read_text(encoding="utf-8").splitlines()
    types = {}
    for line in text:
        m = TYPEDEF.match(line)
        if m:
            types[m.group(2)] = (m.group(1).strip(), m.group(3).strip())
    funcs = []
    seen = set()
    for line in text:
        m = GLAPI.match(line)
        if m and m.group(2) not in seen and m.group(1) in types:
            seen.add(m.group(2))
            funcs.append((m.group(2), m.group(1)) + types[m.group(1)])

    out = [
        "// ÜRETİLDİ: Tools/Web/gen_gl_stubs.py (elle düzenlemeyin). WebGL'de olmayan GL işlevleri için imzası aynı yedekler:",
        "// ilk çağrıda bir kez uyarı, sıfır / boş dönüş. Yalnız Web (Emscripten) derlemesinde.",
        "#include \"GLMissingStubs.h\"",
        "",
        "#if defined(__EMSCRIPTEN__)",
        "#include \"Core/Logging/Log.h\"",
        "#include <glad/glad.h>",
        "",
        "#if defined(__clang__)",
        "#pragma clang diagnostic ignored \"-Wunused-parameter\"",
        "#endif",
        "",
        "namespace Conqueror::GLProfile",
        "{",
        "    namespace",
        "    {",
        "        void Missing(const char* name) { CQ_CORE_WARN(\"{0} is not available in this graphics backend (WebGL 2 / WebGPU; call ignored)\", name); }",
        "",
    ]
    for name, _, ret, params in funcs:
        params = params if params and params != "void" else "void"
        body = f"static bool cqWarned = false; if (!cqWarned) {{ cqWarned = true; Missing(\"{name}\"); }}"
        if ret != "void":
            body += " return {};"
        out.append(f"        {ret} APIENTRY S_{name}({params}) {{ {body} }}")
    out += [
        "    }",
        "",
        "    int InstallMissingStubs()",
        "    {",
        "        int count = 0;",
    ]
    for name, _, _, _ in funcs:
        out.append(f"        if (!glad_{name}) {{ glad_{name} = &S_{name}; ++count; }}")
    out += [
        "        return count;",
        "    }",
        "}",
        "#else",
        "namespace Conqueror::GLProfile",
        "{",
        "    int InstallMissingStubs() { return 0; }",
        "}",
        "#endif",
        "",
    ]
    OUT.write_text("\n".join(out), encoding="utf-8")
    print(f"{OUT}: {len(funcs)} functions")


if __name__ == "__main__":
    main()
