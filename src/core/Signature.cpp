#include "core/Signature.h"

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <vector>

#include "core/Util.h"

namespace su::signature {

namespace {

std::string thumbprintOf(PCCERT_CONTEXT cert) {
  BYTE hash[20] = {};
  DWORD size = sizeof(hash);
  if (!CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &size)) return {};
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (DWORD i = 0; i < size; ++i) {
    out += kHex[hash[i] >> 4];
    out += kHex[hash[i] & 0xF];
  }
  return out;
}

std::string subjectOf(PCCERT_CONTEXT cert) {
  const DWORD n = CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
  if (n <= 1) return {};
  std::wstring name(n, L'\0');
  CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name.data(), n);
  name.resize(n - 1);
  return narrow(name);
}

}  // namespace

std::optional<Signer> verifiedSigner(const std::filesystem::path& file) {
  const std::wstring path = file.wstring();
  WINTRUST_FILE_INFO fileInfo{sizeof(fileInfo)};
  fileInfo.pcwszFilePath = path.c_str();

  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  WINTRUST_DATA data{sizeof(data)};
  data.dwUIChoice = WTD_UI_NONE;
  data.fdwRevocationChecks = WTD_REVOKE_NONE;
  data.dwUnionChoice = WTD_CHOICE_FILE;
  data.pFile = &fileInfo;
  data.dwStateAction = WTD_STATEACTION_VERIFY;
  data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_DISABLE_MD2_MD4;

  std::optional<Signer> result;
  const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
  if (status == ERROR_SUCCESS) {
    if (CRYPT_PROVIDER_DATA* prov = WTHelperProvDataFromStateData(data.hWVTStateData)) {
      if (CRYPT_PROVIDER_SGNR* sgnr = WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0)) {
        if (CRYPT_PROVIDER_CERT* pc = WTHelperGetProvCertFromChain(sgnr, 0); pc && pc->pCert) {
          Signer s{subjectOf(pc->pCert), thumbprintOf(pc->pCert)};
          if (!s.subject.empty()) result = std::move(s);
        }
      }
    }
  }
  data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
  return result;
}

const std::optional<Signer>& ownSigner() {
  static const std::optional<Signer> own = [] {
    std::vector<wchar_t> buf(MAX_PATH * 2);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    return n ? verifiedSigner(std::filesystem::path(std::wstring(buf.data(), n))) : std::nullopt;
  }();
  return own;
}

bool acceptableUpdateSigner(const std::optional<Signer>& running, const std::optional<Signer>& update) {
  if (!running) return true;
  return update && update->subject == running->subject;
}

}  // namespace su::signature
