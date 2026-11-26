#include "vault/PasswordVault.h"

#include "util/Utf8.h"

#include <windows.h>

#include <wincred.h>

namespace winssh::vault
{

void Wipe(std::string &secret)
{
    if (!secret.empty())
        ::SecureZeroMemory(secret.data(), secret.size());
    secret.clear();
}

bool PasswordVault::Save(const std::string &profileId, const std::string &user, const std::string &password,
                         std::string *error) const
{
    std::wstring target = util::Utf8ToWide(prefix_ + profileId);
    std::wstring wuser = util::Utf8ToWide(user);
    CREDENTIALW entry{};
    entry.Type = CRED_TYPE_GENERIC;
    entry.TargetName = target.data();
    entry.UserName = wuser.data();
    entry.CredentialBlobSize = static_cast<DWORD>(password.size()); // UTF-8 bytes
    entry.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(password.data()));
    entry.Persist = CRED_PERSIST_LOCAL_MACHINE; // this user on this machine, not roamed
    if (::CredWriteW(&entry, 0))
        return true;
    if (error)
        *error = "CredWrite failed (error " + std::to_string(::GetLastError()) + ")";
    return false;
}

std::optional<std::string> PasswordVault::Load(const std::string &profileId) const
{
    const std::wstring target = util::Utf8ToWide(prefix_ + profileId);
    PCREDENTIALW entry = nullptr;
    if (!::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &entry))
        return std::nullopt;
    std::string password(reinterpret_cast<const char *>(entry->CredentialBlob), entry->CredentialBlobSize);
    ::SecureZeroMemory(entry->CredentialBlob, entry->CredentialBlobSize);
    ::CredFree(entry);
    return password;
}

bool PasswordVault::Exists(const std::string &profileId) const
{
    const std::wstring target = util::Utf8ToWide(prefix_ + profileId);
    PCREDENTIALW entry = nullptr;
    if (!::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &entry))
        return false;
    ::SecureZeroMemory(entry->CredentialBlob, entry->CredentialBlobSize);
    ::CredFree(entry);
    return true;
}

bool PasswordVault::Remove(const std::string &profileId) const
{
    const std::wstring target = util::Utf8ToWide(prefix_ + profileId);
    return ::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
}

} // namespace winssh::vault
