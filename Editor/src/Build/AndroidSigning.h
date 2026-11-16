#pragma once

#include <filesystem>
#include <string>

// Android yayın anahtarı (47, Unity'nin Keystore Manager'ı): JDK keytool ile RSA 2048 anahtar deposu oluşturur. Parola diske ve
// komut satırına yazılmaz (keytool -storepass:env); derleme sırasında CQ_ANDROID_KEYSTORE_PASS ile verilir
namespace Conqueror::Editor::AndroidSigning
{
    bool CreateKeystore(const std::filesystem::path& file, const std::string& alias, const std::string& password, const std::string& owner,
                        int years, std::string& error);
    // android_keystore create <dosya> <takma ad> <parola> | use <dosya> <takma ad>
    void RegisterAutomation();
}
