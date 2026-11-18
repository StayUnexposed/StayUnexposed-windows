#include "core/DownloadStamp.h"

#include <windows.h>

#include <cstring>
#include <fstream>

#include "core/HardwareId.h"

namespace su::downloadstamp {

namespace {

uint32_t readU32LE(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::optional<std::string> parseStamp(const unsigned char* data, size_t size) {
  constexpr size_t kFooter = 8 + 4;  // magic + length
  if (!data || size < kFooter + 1) return std::nullopt;
  if (std::memcmp(data + size - 8, kMagic, 8) != 0) return std::nullopt;
  const uint32_t len = readU32LE(data + size - kFooter);
  if (len == 0 || len > kMaxIdLen || size < kFooter + len) return std::nullopt;
  const std::string raw(reinterpret_cast<const char*>(data + size - kFooter - len), len);
  const std::string id = hwid::normalizeId(raw);
  if (hwid::checkId(id) != hwid::IdCheck::Ok) return std::nullopt;
  return id;
}

std::vector<unsigned char> appendStamp(std::vector<unsigned char> exeBytes, const std::string& inviterId) {
  const std::string id = hwid::normalizeId(inviterId);
  if (hwid::checkId(id) != hwid::IdCheck::Ok) return exeBytes;
  exeBytes.insert(exeBytes.end(), id.begin(), id.end());
  const uint32_t len = static_cast<uint32_t>(id.size());
  for (int i = 0; i < 4; ++i) exeBytes.push_back(static_cast<unsigned char>((len >> (i * 8)) & 0xFF));
  exeBytes.insert(exeBytes.end(), kMagic, kMagic + 8);
  return exeBytes;
}

std::optional<std::string> readInviter() {
  wchar_t path[MAX_PATH * 2] = {};
  const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  if (n == 0 || n >= std::size(path)) return std::nullopt;

  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  f.seekg(0, std::ios::end);
  const std::streamoff fileSize = f.tellg();
  if (fileSize <= 0) return std::nullopt;
  const std::streamoff tail = std::min<std::streamoff>(fileSize, static_cast<std::streamoff>(8 + 4 + kMaxIdLen));
  f.seekg(fileSize - tail, std::ios::beg);
  std::vector<unsigned char> buf(static_cast<size_t>(tail));
  f.read(reinterpret_cast<char*>(buf.data()), tail);
  if (!f) return std::nullopt;
  return parseStamp(buf.data(), buf.size());
}

}  // namespace su::downloadstamp
