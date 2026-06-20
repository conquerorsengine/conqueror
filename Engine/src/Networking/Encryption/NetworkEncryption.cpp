#include "NetworkEncryption.h"
#include "Core/Logging/Log.h"

#include <mbedtls/gcm.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <cstring>

namespace Conqueror
{
    static constexpr int AES_KEY_SIZE = 32;     // 256 bits
    static constexpr int GCM_IV_SIZE  = 12;     // 96 bits (recommended for GCM)
    static constexpr int GCM_TAG_SIZE = 16;     // 128 bits

    NetworkEncryption::NetworkEncryption() = default;

    NetworkEncryption::~NetworkEncryption()
    {
        Shutdown();
    }

    bool NetworkEncryption::Initialize()
    {
        if (m_Initialized) return true;

        // Entropy + CSPRNG
        auto* entropy = new mbedtls_entropy_context;
        auto* ctrDrbg = new mbedtls_ctr_drbg_context;
        mbedtls_entropy_init(entropy);
        mbedtls_ctr_drbg_init(ctrDrbg);

        const char* pers = "conqueror_network";
        int ret = mbedtls_ctr_drbg_seed(ctrDrbg, mbedtls_entropy_func, entropy,
                                         reinterpret_cast<const unsigned char*>(pers), strlen(pers));
        if (ret != 0)
        {
            char errBuf[128];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            CQ_CORE_ERROR("[Encryption] CSPRNG seed failed: {0}", errBuf);
            delete entropy;
            delete ctrDrbg;
            return false;
        }

        m_EntropyContext = entropy;
        m_CtrDrbgContext = ctrDrbg;

        // AES-GCM context
        auto* gcm = new mbedtls_gcm_context;
        mbedtls_gcm_init(gcm);
        m_AESContext = gcm;

        // RSA context
        auto* pk = new mbedtls_pk_context;
        mbedtls_pk_init(pk);
        m_RSAContext = pk;

        m_Initialized = true;
        CQ_CORE_INFO("[Encryption] Initialized.");
        return true;
    }

    void NetworkEncryption::Shutdown()
    {
        if (!m_Initialized) return;

        if (m_AESContext)
        {
            mbedtls_gcm_free(static_cast<mbedtls_gcm_context*>(m_AESContext));
            delete static_cast<mbedtls_gcm_context*>(m_AESContext);
            m_AESContext = nullptr;
        }
        if (m_RSAContext)
        {
            mbedtls_pk_free(static_cast<mbedtls_pk_context*>(m_RSAContext));
            delete static_cast<mbedtls_pk_context*>(m_RSAContext);
            m_RSAContext = nullptr;
        }
        if (m_CtrDrbgContext)
        {
            mbedtls_ctr_drbg_free(static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext));
            delete static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext);
            m_CtrDrbgContext = nullptr;
        }
        if (m_EntropyContext)
        {
            mbedtls_entropy_free(static_cast<mbedtls_entropy_context*>(m_EntropyContext));
            delete static_cast<mbedtls_entropy_context*>(m_EntropyContext);
            m_EntropyContext = nullptr;
        }

        m_SessionKey.clear();
        m_Initialized = false;
        CQ_CORE_INFO("[Encryption] Shutdown.");
    }

    bool NetworkEncryption::GenerateSessionKey()
    {
        if (!m_Initialized) return false;

        m_SessionKey.resize(AES_KEY_SIZE);
        auto* ctrDrbg = static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext);
        int ret = mbedtls_ctr_drbg_random(ctrDrbg, m_SessionKey.data(), AES_KEY_SIZE);
        if (ret != 0)
        {
            CQ_CORE_ERROR("[Encryption] Failed to generate session key!");
            m_SessionKey.clear();
            return false;
        }

        // Set the key on the GCM context
        auto* gcm = static_cast<mbedtls_gcm_context*>(m_AESContext);
        ret = mbedtls_gcm_setkey(gcm, MBEDTLS_CIPHER_ID_AES, m_SessionKey.data(), 256);
        if (ret != 0)
        {
            CQ_CORE_ERROR("[Encryption] Failed to set AES key!");
            return false;
        }

        CQ_CORE_INFO("[Encryption] Session key generated.");
        return true;
    }

    bool NetworkEncryption::SetSessionKey(const uint8_t* key, size_t keyLen)
    {
        if (!m_Initialized || keyLen != AES_KEY_SIZE) return false;

        m_SessionKey.assign(key, key + keyLen);

        auto* gcm = static_cast<mbedtls_gcm_context*>(m_AESContext);
        int ret = mbedtls_gcm_setkey(gcm, MBEDTLS_CIPHER_ID_AES, m_SessionKey.data(), 256);
        if (ret != 0)
        {
            CQ_CORE_ERROR("[Encryption] Failed to set AES key from external data!");
            return false;
        }

        CQ_CORE_INFO("[Encryption] Session key set from external source.");
        return true;
    }

    std::vector<uint8_t> NetworkEncryption::Encrypt(const uint8_t* plaintext, size_t size)
    {
        if (!m_Initialized || m_SessionKey.empty()) return {};

        auto* gcm = static_cast<mbedtls_gcm_context*>(m_AESContext);
        auto* ctrDrbg = static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext);

        // Generate random IV
        uint8_t iv[GCM_IV_SIZE];
        mbedtls_ctr_drbg_random(ctrDrbg, iv, GCM_IV_SIZE);

        // Output: [IV (12)] [Ciphertext (N)] [Tag (16)]
        std::vector<uint8_t> output(GCM_IV_SIZE + size + GCM_TAG_SIZE);
        std::memcpy(output.data(), iv, GCM_IV_SIZE);

        uint8_t tag[GCM_TAG_SIZE];

        int ret = mbedtls_gcm_crypt_and_tag(
            gcm, MBEDTLS_GCM_ENCRYPT,
            size,
            iv, GCM_IV_SIZE,
            nullptr, 0,              // No additional data
            plaintext, output.data() + GCM_IV_SIZE,
            GCM_TAG_SIZE, tag
        );

        if (ret != 0)
        {
            CQ_CORE_ERROR("[Encryption] AES-GCM encrypt failed!");
            return {};
        }

        std::memcpy(output.data() + GCM_IV_SIZE + size, tag, GCM_TAG_SIZE);
        return output;
    }

    std::vector<uint8_t> NetworkEncryption::Decrypt(const uint8_t* ciphertext, size_t size)
    {
        if (!m_Initialized || m_SessionKey.empty()) return {};
        if (size < GCM_IV_SIZE + GCM_TAG_SIZE) return {};

        auto* gcm = static_cast<mbedtls_gcm_context*>(m_AESContext);

        const uint8_t* iv   = ciphertext;
        size_t dataLen = size - GCM_IV_SIZE - GCM_TAG_SIZE;
        const uint8_t* data = ciphertext + GCM_IV_SIZE;
        const uint8_t* tag  = ciphertext + GCM_IV_SIZE + dataLen;

        std::vector<uint8_t> plaintext(dataLen);

        int ret = mbedtls_gcm_auth_decrypt(
            gcm,
            dataLen,
            iv, GCM_IV_SIZE,
            nullptr, 0,
            tag, GCM_TAG_SIZE,
            data, plaintext.data()
        );

        if (ret != 0)
        {
            CQ_CORE_ERROR("[Encryption] AES-GCM decrypt failed (tampered data?)");
            return {};
        }

        return plaintext;
    }

    bool NetworkEncryption::GenerateRSAKeyPair()
    {
        if (!m_Initialized) return false;

        auto* pk = static_cast<mbedtls_pk_context*>(m_RSAContext);
        auto* ctrDrbg = static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext);

        int ret = mbedtls_pk_setup(pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
        if (ret != 0) { CQ_CORE_ERROR("[Encryption] RSA setup failed!"); return false; }

        ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(*pk), mbedtls_ctr_drbg_random, ctrDrbg, 2048, 65537);
        if (ret != 0) { CQ_CORE_ERROR("[Encryption] RSA keygen failed!"); return false; }

        CQ_CORE_INFO("[Encryption] RSA-2048 key pair generated.");
        return true;
    }

    std::vector<uint8_t> NetworkEncryption::GetPublicKeyDER() const
    {
        if (!m_Initialized) return {};

        auto* pk = static_cast<mbedtls_pk_context*>(m_RSAContext);
        uint8_t buf[4096];

        int len = mbedtls_pk_write_pubkey_der(pk, buf, sizeof(buf));
        if (len < 0) return {};

        // DER is written at the end of the buffer
        return std::vector<uint8_t>(buf + sizeof(buf) - len, buf + sizeof(buf));
    }

    std::vector<uint8_t> NetworkEncryption::EncryptWithPublicKey(
        const uint8_t* pubKeyDER, size_t pubKeyLen,
        const uint8_t* data, size_t dataLen)
    {
        if (!m_Initialized) return {};

        mbedtls_pk_context peerPk;
        mbedtls_pk_init(&peerPk);

        int ret = mbedtls_pk_parse_public_key(&peerPk, pubKeyDER, pubKeyLen);
        if (ret != 0)
        {
            mbedtls_pk_free(&peerPk);
            CQ_CORE_ERROR("[Encryption] Failed to parse peer's public key!");
            return {};
        }

        auto* ctrDrbg = static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext);
        uint8_t output[512];
        size_t olen = 0;

        ret = mbedtls_pk_encrypt(&peerPk, data, dataLen, output, &olen, sizeof(output),
                                  mbedtls_ctr_drbg_random, ctrDrbg);
        mbedtls_pk_free(&peerPk);

        if (ret != 0) { CQ_CORE_ERROR("[Encryption] RSA encrypt failed!"); return {}; }

        return std::vector<uint8_t>(output, output + olen);
    }

    std::vector<uint8_t> NetworkEncryption::DecryptWithPrivateKey(const uint8_t* ciphertext, size_t size)
    {
        if (!m_Initialized) return {};

        auto* pk = static_cast<mbedtls_pk_context*>(m_RSAContext);
        auto* ctrDrbg = static_cast<mbedtls_ctr_drbg_context*>(m_CtrDrbgContext);

        uint8_t output[512];
        size_t olen = 0;

        int ret = mbedtls_pk_decrypt(pk, ciphertext, size, output, &olen, sizeof(output),
                                      mbedtls_ctr_drbg_random, ctrDrbg);
        if (ret != 0) { CQ_CORE_ERROR("[Encryption] RSA decrypt failed!"); return {}; }

        return std::vector<uint8_t>(output, output + olen);
    }
}
