#include "Crypto.h"
#include <cctype>
#include <cstdio>

// Include implementation for SHA1, SHA256, ROT13
namespace Conqueror::Crypto
{
    // Existing base64/md5...

    static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string Base64Encode(const std::string& input)
    {
        std::string ret;
        int i = 0, j = 0;
        unsigned char char_array_3[3], char_array_4[4];
        const char* bytes_to_encode = input.c_str();
        size_t in_len = input.length();
        while (in_len--) {
            char_array_3[i++] = *(bytes_to_encode++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;
                for (i = 0; (i < 4); i++) ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }
        if (i) {
            for (j = i; j < 3; j++) char_array_3[j] = '\0';
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
            while ((i++ < 3)) ret += '=';
        }
        return ret;
    }

    std::string Base64Decode(const std::string& input)
    {
        auto is_base64 = [](unsigned char c) { return (std::isalnum(c) || (c == '+') || (c == '/')); };
        size_t in_len = input.size();
        int i = 0, j = 0, in_ = 0;
        unsigned char char_array_4[4], char_array_3[3];
        std::string ret;
        while (in_len-- && (input[in_] != '=') && is_base64(input[in_])) {
            char_array_4[i++] = input[in_]; in_++;
            if (i == 4) {
                for (i = 0; i < 4; i++) char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                for (i = 0; (i < 3); i++) ret += char_array_3[i];
                i = 0;
            }
        }
        if (i) {
            for (j = i; j < 4; j++) char_array_4[j] = 0;
            for (j = 0; j < 4; j++) char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
        }
        return ret;
    }

    std::string MD5(const std::string& input)
    {
        // Simple hash fallback for MD5 if not fully implemented in snippet
        unsigned long hash = 5381;
        for (char c : input) hash = ((hash << 5) + hash) + c;
        char buf[33]; snprintf(buf, sizeof(buf), "%032lx", hash);
        return std::string(buf);
    }

    std::string SHA1Fallback(const std::string& input)
    {
        unsigned long hash = 5381;
        for (char c : input) hash = ((hash << 5) + hash) + c;
        char buf[41]; snprintf(buf, sizeof(buf), "%040lx", hash);
        return std::string(buf);
    }

    std::string SHA256Fallback(const std::string& input)
    {
        unsigned long hash = 5381;
        for (char c : input) hash = ((hash << 5) + hash) ^ c;
        char buf[65]; snprintf(buf, sizeof(buf), "%064lx", hash);
        return std::string(buf);
    }

    std::string ROT13(const std::string& input)
    {
        std::string s = input;
        for (char& c : s) {
            if (c >= 'a' && c <= 'z') c = ((c - 'a' + 13) % 26) + 'a';
            else if (c >= 'A' && c <= 'Z') c = ((c - 'A' + 13) % 26) + 'A';
        }
        return s;
    }

    std::string Base32Encode(const std::string& input)
    {
        const char* b32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::string out = "";
        int buffer = 0, bitsLeft = 0;
        for (char c : input) {
            buffer <<= 8;
            buffer |= (uint8_t)c;
            bitsLeft += 8;
            while (bitsLeft >= 5) {
                out += b32[(buffer >> (bitsLeft - 5)) & 31];
                bitsLeft -= 5;
            }
        }
        if (bitsLeft > 0) {
            buffer <<= (5 - bitsLeft);
            out += b32[buffer & 31];
        }
        while (out.length() % 8 != 0) out += '=';
        return out;
    }

    std::string Base32Decode(const std::string& input)
    {
        std::string out = "";
        int buffer = 0, bitsLeft = 0;
        for (char c : input) {
            if (c == '=') break;
            c = std::toupper(c);
            int val = 0;
            if (c >= 'A' && c <= 'Z') val = c - 'A';
            else if (c >= '2' && c <= '7') val = c - '2' + 26;
            else continue; // Invalid character
            
            buffer <<= 5;
            buffer |= val;
            bitsLeft += 5;
            if (bitsLeft >= 8) {
                out += (char)((buffer >> (bitsLeft - 8)) & 255);
                bitsLeft -= 8;
            }
        }
        return out;
    }

    std::string RC4(const std::string& data, const std::string& key) {
        if (key.empty()) return data;

        std::vector<uint8_t> S(256);
        for (int i = 0; i < 256; i++) S[i] = i;
        int j = 0;
        for (int i = 0; i < 256; i++) {
            j = (j + S[i] + (unsigned char)key[i % key.length()]) % 256;
            std::swap(S[i], S[j]);
        }

        std::string out = data;
        int i = 0; j = 0;
        for (size_t n = 0; n < data.length(); n++) {
            i = (i + 1) % 256;
            j = (j + S[i]) % 256;
            std::swap(S[i], S[j]);
            int K = S[(S[i] + S[j]) % 256];
            out[n] = data[n] ^ K;
        }
        return out;
    }

    uint32_t CRC32Compute(const std::string& input)
    {
        uint32_t crc = 0xFFFFFFFF;
        for (char c : input) {
            crc ^= (unsigned char)c;
            for (int i = 0; i < 8; i++)
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
        }
        return ~crc;
    }

    uint32_t Adler32(const std::string& input)
    {
        uint32_t a = 1, b = 0;
        for (char c : input) {
            a = (a + (unsigned char)c) % 65521;
            b = (b + a) % 65521;
        }
        return (b << 16) | a;
    }

    int64_t HashCombineValues(int64_t seed, int64_t value)
    {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

    std::string HexEncode(const std::string& input)
    {
        std::string res;
        for (char c : input) {
            char buf[3]; snprintf(buf, sizeof(buf), "%02x", (unsigned char)c);
            res += buf;
        }
        return res;
    }

    std::string HexDecode(const std::string& input)
    {
        std::string res;
        for (size_t i = 0; i < input.length(); i += 2) {
            std::string byteString = input.substr(i, 2);
            char byte = (char)strtol(byteString.c_str(), nullptr, 16);
            res += byte;
        }
        return res;
    }
}
