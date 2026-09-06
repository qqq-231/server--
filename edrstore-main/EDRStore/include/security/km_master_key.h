#ifndef EDRSTORE_SECURITY_KM_MASTER_KEY_H
#define EDRSTORE_SECURITY_KM_MASTER_KEY_H

#include <cstdint>
#include <string>

namespace edrstore::security {

class KMMasterKey {
  public:
    static constexpr size_t kSecretSize = 32;

    KMMasterKey();
    ~KMMasterKey();

    KMMasterKey(const KMMasterKey&) = delete;
    KMMasterKey& operator=(const KMMasterKey&) = delete;

    // Creation is deliberately separate from normal KeyManager startup.
    bool CreateNew(const std::string& path, std::string& error);
    bool LoadExisting(const std::string& path, std::string& error);

    const uint8_t* GlobalSecret() const;
    const uint8_t* PaddingSecret() const;
    bool IsLoaded() const;

  private:
    bool LockSecrets();
    void ClearSecrets();

    uint8_t global_secret_[kSecretSize];
    uint8_t padding_secret_[kSecretSize];
    bool loaded_;
    bool global_locked_;
    bool padding_locked_;
};

}  // namespace edrstore::security

#endif
