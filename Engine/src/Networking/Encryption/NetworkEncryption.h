#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace Conqueror
{
    /// Network encryption layer using mbedTLS.
    /// Provides AES-256-GCM symmetric encryption for packet data
    /// and RSA key exchange for initial handshake.
    class NetworkEncryption
    {
    public:
        NetworkEncryption();
        ~NetworkEncryption();

        NetworkEncryption(const NetworkEncryption&) = delete;
        NetworkEncryption& operator=(const NetworkEncryption&) = delete;

        /// Initialize encryption subsystem
        bool Initialize();
        void Shutdown();

        /// Generate a new random symmetric key (AES-256)
        bool GenerateSessionKey();

        /// Set symmetric key from raw bytes (32 bytes for AES-256)
        bool SetSessionKey(const uint8_t* key, size_t keyLen);

        /// Get the current session key (for sending to peer during handshake)
        const std::vector<uint8_t>& GetSessionKey() const { return m_SessionKey; }

        /// Encrypt data using AES-256-GCM
        /// Returns encrypted data with prepended IV (12 bytes) and appended tag (16 bytes)
        std::vector<uint8_t> Encrypt(const uint8_t* plaintext, size_t size);

        /// Decrypt data encrypted with Encrypt()
        /// Expects IV (12 bytes) + ciphertext + tag (16 bytes)
        std::vector<uint8_t> Decrypt(const uint8_t* ciphertext, size_t size);

        /// Generate an RSA-2048 keypair for key exchange
        bool GenerateRSAKeyPair();

        /// Get public key in DER format (for sending to peer)
        std::vector<uint8_t> GetPublicKeyDER() const;

        /// Encrypt session key with peer's RSA public key (for key exchange)
        std::vector<uint8_t> EncryptWithPublicKey(const uint8_t* pubKeyDER, size_t pubKeyLen,
                                                    const uint8_t* data, size_t dataLen);

        /// Decrypt data with our RSA private key (for receiving session key)
        std::vector<uint8_t> DecryptWithPrivateKey(const uint8_t* ciphertext, size_t size);

        bool IsInitialized() const { return m_Initialized; }
        bool HasSessionKey() const { return !m_SessionKey.empty(); }

    private:
        bool m_Initialized = false;
        std::vector<uint8_t> m_SessionKey;  // 32 bytes AES-256

        // mbedTLS opaque context pointers (forward declared to avoid header dependency)
        void* m_AESContext = nullptr;     // mbedtls_gcm_context*
        void* m_RSAContext = nullptr;     // mbedtls_pk_context*
        void* m_EntropyContext = nullptr; // mbedtls_entropy_context*
        void* m_CtrDrbgContext = nullptr; // mbedtls_ctr_drbg_context*
    };
}
