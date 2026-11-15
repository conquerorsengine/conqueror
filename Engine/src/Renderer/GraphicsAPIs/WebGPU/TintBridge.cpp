// SPIR-V -> WGSL (Google Tint, Apache 2.0; Dawn kaynak ağacından derlenir, CQ_DAWN_DIR). Motorun geri kalanı Tint'in iç
// başlıklarını görmez: yalnız bu C arabirimi (TintBridge.h). Ayrı küçük kitaplıkta (CQTintBridge) Tint'in içerme yollarıyla derlenir.

#include "TintBridge.h"

#include "src/tint/lang/core/ir/module.h"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    char* Dup(const std::string& s)
    {
        char* p = static_cast<char*>(std::malloc(s.size() + 1));
        if (p)
            std::memcpy(p, s.c_str(), s.size() + 1);
        return p;
    }
}

extern "C" int cq_tint_spirv_to_wgsl(const uint32_t* words, size_t count, const uint32_t* combinedBindings, size_t combinedCount,
                                     uint32_t samplerOffset, char** wgsl, char** error)
{
    *wgsl = nullptr;
    *error = nullptr;
    const std::vector<uint32_t> spirv(words, words + count);
    tint::spirv::reader::Options readerOptions;
    for (size_t i = 0; i < combinedCount; ++i)
        readerOptions.sampler_mappings.insert({ tint::BindingPoint{ 0, combinedBindings[i] }, tint::BindingPoint{ 0, samplerOffset + combinedBindings[i] } });
    auto ir = tint::spirv::reader::ReadIR(spirv, readerOptions);
    if (ir != tint::Success)
    {
        std::stringstream ss;
        ss << ir.Failure();
        *error = Dup("SPIR-V: " + ss.str());
        return 0;
    }
    tint::wgsl::writer::Options writerOptions;
    // GLSL türev işlevlerini tekdüze olmayan akışta da kullanır (WGSL tekdüzelik denetimi reddederdi)
    writerOptions.allow_non_uniform_derivatives = true;
    auto out = tint::wgsl::writer::WgslFromIR(ir.Get(), writerOptions);
    if (out != tint::Success)
    {
        std::stringstream ss;
        ss << out.Failure();
        *error = Dup("WGSL: " + ss.str());
        return 0;
    }
    *wgsl = Dup(out->wgsl);
    return *wgsl != nullptr;
}

extern "C" void cq_tint_free(char* p)
{
    std::free(p);
}
