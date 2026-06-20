#pragma once
#include "Core/Base/Base.h"
#include <string>

namespace Conqueror::Crypto
{
    CQ_API std::string Base64Encode(const std::string& input);
    CQ_API std::string Base64Decode(const std::string& input);
    CQ_API std::string MD5(const std::string& input);
    CQ_API std::string SHA1Fallback(const std::string& input);
    CQ_API std::string SHA256Fallback(const std::string& input);
    CQ_API std::string ROT13(const std::string& input);
    CQ_API std::string Base32Encode(const std::string& input);
    CQ_API std::string Base32Decode(const std::string& input);
    CQ_API std::string RC4(const std::string& data, const std::string& key);

    // Hash / Checksum utilities
    CQ_API uint32_t CRC32Compute(const std::string& input);
    CQ_API uint32_t Adler32(const std::string& input);
    CQ_API int64_t HashCombineValues(int64_t seed, int64_t value);

    // Hex encoding
    CQ_API std::string HexEncode(const std::string& input);
    CQ_API std::string HexDecode(const std::string& input);
}
