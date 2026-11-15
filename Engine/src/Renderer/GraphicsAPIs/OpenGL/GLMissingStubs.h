#pragma once

namespace Conqueror::GLProfile
{
    // Web (WebGL 2): boş kalan glad göstericilerine imzası aynı yedekler koyar (Tools/Web/gen_gl_stubs.py üretir); kaç işlev
    // yedeklendiğini döndürür. Diğer platformlarda hiçbir şey yapmaz.
    int InstallMissingStubs();
}
