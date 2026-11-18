#pragma once
// Compile-time obfuscation of embedded secrets.
// The plain-text literal only exists during constant evaluation (consteval constructor),
// so only the masked bytes end up in the executable. This deters casual inspection
// (e.g. `strings StayUnexposed.exe`) but is NOT real protection: anyone running the
// program under a debugger can recover the values.
#include <array>
#include <cstddef>
#include <string>

namespace su::secrets {

constexpr unsigned char maskAt(std::size_t i) {
  return static_cast<unsigned char>(((i * 73u) + 0x2Bu) ^ 0xA5u ^ ((i >> 2) * 17u));
}

template <std::size_t N>
class Obfuscated {
 public:
  consteval Obfuscated(const char (&s)[N]) {
    for (std::size_t i = 0; i < N; ++i)
      data_[i] = static_cast<char>(static_cast<unsigned char>(s[i]) ^ maskAt(i));
  }
  std::string reveal() const {
    std::string out(N - 1, '\0');
    for (std::size_t i = 0; i + 1 < N; ++i)
      out[i] = static_cast<char>(static_cast<unsigned char>(data_[i]) ^ maskAt(i));
    return out;
  }

 private:
  std::array<char, N> data_{};
};

}  // namespace su::secrets
