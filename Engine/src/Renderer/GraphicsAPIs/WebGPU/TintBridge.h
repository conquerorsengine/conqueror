#pragma once

#include <cstddef>
#include <cstdint>

// Tint köprüsü (TintBridge.cpp): SPIR-V sözcükleri -> WGSL metni. Başarıda 1 ve *wgsl (cq_tint_free ile bırakılır), hatada 0 ve
// *error. Birleşik doku + örnekleyiciler ayrılır: doku SPIR-V numarasında kalır, örnekleyici samplerOffset + numara olur
// (combinedBindings: küme 0'daki birleşik örnekleyici numaraları; köşe ve parça aşamaları aynı numarayı kullansın diye).
extern "C"
{
    int cq_tint_spirv_to_wgsl(const uint32_t* words, size_t count, const uint32_t* combinedBindings, size_t combinedCount,
                              uint32_t samplerOffset, char** wgsl, char** error);
    void cq_tint_free(char* p);
}
