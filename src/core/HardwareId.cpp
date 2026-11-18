#include "core/HardwareId.h"

// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <winioctl.h>
// clang-format on

#include <algorithm>
#include <cctype>
#include <memory>

#include "core/Util.h"

static constexpr const char* kDownloadUrl = "https://stayunexposed.com/download";

namespace su::hwid {

namespace {

// Kept from the first release: changing it would give every PC a new referral ID.
constexpr const char* kSalt = "BreachWatch/hwid/v1";

std::string trimSerial(const char* s) {
  std::string out;
  for (; s && *s; ++s)
    if (!std::isspace(static_cast<unsigned char>(*s))) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(*s))));
  return out;
}

struct HandleCloser {
  void operator()(HANDLE h) const {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// Physical drive number that holds the Windows volume (falls back to 0).
DWORD systemDiskNumber() {
  wchar_t winDir[MAX_PATH] = {};
  GetWindowsDirectoryW(winDir, MAX_PATH);
  std::wstring volume = L"\\\\.\\" + std::wstring(winDir, 2);  // \\.\C:
  UniqueHandle h(CreateFileW(volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (h.get() == INVALID_HANDLE_VALUE) return 0;
  VOLUME_DISK_EXTENTS extents{};
  DWORD bytes = 0;
  if (DeviceIoControl(h.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &extents, sizeof(extents), &bytes,
                      nullptr) &&
      extents.NumberOfDiskExtents > 0)
    return extents.Extents[0].DiskNumber;
  return 0;
}

std::string diskSerial() {
  std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(systemDiskNumber());
  // Zero access rights are enough for IOCTL_STORAGE_QUERY_PROPERTY (no admin needed).
  UniqueHandle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (h.get() == INVALID_HANDLE_VALUE) return {};
  STORAGE_PROPERTY_QUERY query{};
  query.PropertyId = StorageDeviceProperty;
  query.QueryType = PropertyStandardQuery;
  std::vector<unsigned char> buf(4096);
  DWORD bytes = 0;
  if (!DeviceIoControl(h.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buf.data(),
                       static_cast<DWORD>(buf.size()), &bytes, nullptr))
    return {};
  auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
  if (desc->SerialNumberOffset == 0 || desc->SerialNumberOffset >= bytes) return {};
  return trimSerial(reinterpret_cast<const char*>(buf.data() + desc->SerialNumberOffset));
}

std::string volumeSerial() {
  wchar_t winDir[MAX_PATH] = {};
  GetWindowsDirectoryW(winDir, MAX_PATH);
  std::wstring root = std::wstring(winDir, 3);  // drive root, e.g. "C:" and a backslash
  DWORD serial = 0;
  if (!GetVolumeInformationW(root.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) return {};
  char buf[16];
  snprintf(buf, sizeof buf, "%08lX", serial);
  return buf;
}

bool looksVirtual(const std::wstring& description) {
  static const wchar_t* words[] = {L"virtual", L"vpn", L"hyper-v", L"vmware", L"virtualbox", L"loopback",
                                   L"bluetooth", L"tap-", L"tunnel", L"wan miniport", L"pseudo", L"npcap"};
  std::wstring lower = description;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
  for (auto w : words)
    if (lower.find(w) != std::wstring::npos) return true;
  return false;
}

std::string physicalMac() {
  ULONG size = 32 * 1024;
  std::vector<unsigned char> buf(size);
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;
  ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  if (rc == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  }
  if (rc != NO_ERROR) return {};

  std::vector<std::string> macs;
  for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
    if (a->PhysicalAddressLength != 6) continue;
    if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) continue;
    if (a->Description && looksVirtual(a->Description)) continue;
    if (a->PhysicalAddress[0] & 0x02) continue;  // locally administered (randomised / virtual) address
    char mac[18];
    snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X", a->PhysicalAddress[0], a->PhysicalAddress[1],
             a->PhysicalAddress[2], a->PhysicalAddress[3], a->PhysicalAddress[4], a->PhysicalAddress[5]);
    macs.emplace_back(mac);
  }
  if (macs.empty()) return {};
  // Lowest address: stable regardless of adapter order or which adapter is connected.
  return *std::min_element(macs.begin(), macs.end());
}

std::vector<unsigned char> sha256(const std::string& data) {
  std::vector<unsigned char> out(32);
  BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
             static_cast<ULONG>(data.size()), out.data(), static_cast<ULONG>(out.size()));
  return out;
}

}  // namespace

std::string sha256Hex(const std::string& data) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (unsigned char b : sha256(data)) {
    out.push_back(hex[b >> 4]);
    out.push_back(hex[b & 15]);
  }
  return out;
}

namespace {
constexpr const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford base32: no I, L, O, U
constexpr int kBase = 32;
constexpr size_t kDataChars = 10;              // base32 characters taken from the hash
constexpr size_t kIdLength = 2 + kDataChars + 1;  // "SU" + data + 1 check character = 13

int symbolValue(char c) {
  for (int i = 0; i < kBase; ++i)
    if (kAlphabet[i] == c) return i;
  return -1;
}

// Luhn mod N over `values`, starting with `factor` for the rightmost one.
int luhnSum(const std::vector<int>& values, int factor) {
  int sum = 0;
  for (auto it = values.rbegin(); it != values.rend(); ++it) {
    int addend = factor * *it;
    factor = factor == 2 ? 1 : 2;
    addend = addend / kBase + addend % kBase;
    sum += addend;
  }
  return sum;
}
}  // namespace

char checkChar(const std::string& code) {
  std::vector<int> v;
  for (char c : code) v.push_back(std::max(0, symbolValue(c)));
  const int remainder = luhnSum(v, 2) % kBase;
  return kAlphabet[(kBase - remainder) % kBase];
}

std::string withCheck(const std::string& code) {
  // "SU" + the data characters + one Luhn check character. No dashes.
  return "SU" + code + std::string(1, checkChar(code));
}

std::string normalizeId(const std::string& raw) {
  std::string s;
  for (char c : raw) {
    if (c == ' ' || c == '-' || c == '\t') continue;
    s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (s.rfind("SU", 0) != 0) return s;
  std::string code = s.substr(2);
  for (auto& c : code) {
    if (c == 'O') c = '0';
    else if (c == 'I' || c == 'L') c = '1';
  }
  return "SU" + code;  // canonical form: "SU" + 11 characters (10 data + 1 check), no dashes
}

IdCheck checkId(const std::string& id) {
  if (id.size() != kIdLength || id.rfind("SU", 0) != 0) return IdCheck::BadFormat;
  std::vector<int> v;
  for (size_t i = 2; i < id.size(); ++i) {
    const int value = symbolValue(id[i]);
    if (value < 0) return IdCheck::BadFormat;
    v.push_back(value);
  }
  return luhnSum(v, 1) % kBase == 0 ? IdCheck::Ok : IdCheck::BadChecksum;
}

std::string base32(const std::vector<unsigned char>& bytes, size_t chars) {
  const char* alphabet = kAlphabet;
  std::string out;
  unsigned int buffer = 0;
  int bits = 0;
  for (unsigned char b : bytes) {
    buffer = (buffer << 8) | b;
    bits += 8;
    while (bits >= 5 && out.size() < chars) {
      out.push_back(alphabet[(buffer >> (bits - 5)) & 31]);
      bits -= 5;
    }
    if (out.size() >= chars) break;
  }
  return out;
}

Facts collect() {
  Facts f;
  f.diskSerial = diskSerial();
  f.volumeSerial = volumeSerial();
  f.mac = physicalMac();
  return f;
}

std::string idFromFacts(const Facts& f) {
  // The disk serial is the primary identity; the volume serial stands in when it can't be read.
  std::string material = std::string(kSalt) + "|disk=" + (f.diskSerial.empty() ? "vol:" + f.volumeSerial : f.diskSerial) +
                         "|mac=" + f.mac;
  return withCheck(base32(sha256(material), kDataChars));
}

const std::string& userId() {
  static const std::string id = idFromFacts(collect());
  return id;
}

std::string referralUrl(const std::string& id) {
  std::string base = kDownloadUrl;
  return base + (base.find('?') == std::string::npos ? "?" : "&") + "ref=" + urlEncode(id);
}

}  // namespace su::hwid
