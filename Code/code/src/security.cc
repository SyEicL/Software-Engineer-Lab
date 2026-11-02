#include "market/security.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
// BCrypt requires the Windows type definitions before its own declarations.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#include "market/domain.h"

namespace market {
namespace {
template <std::size_t N>
std::string Hex(const std::array<unsigned char, N>& data) {
  std::ostringstream stream;
  for (auto byte : data) {
    stream << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(byte);
  }
  return stream.str();
}
}  // namespace

std::string RandomToken() {
  std::array<unsigned char, 16> data{};
#ifdef _WIN32
  const auto status = BCryptGenRandom(nullptr, data.data(), data.size(),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  Require(status >= 0, ErrorCode::kStorage, "Secure random generator failed");
#else
  Require(RAND_bytes(data.data(), data.size()) == 1, ErrorCode::kStorage,
          "Secure random generator failed");
#endif
  return Hex(data);
}
std::string Pbkdf2Hasher::NewSalt() { return RandomToken(); }

std::string Pbkdf2Hasher::Hash(const std::string& password,
                               const std::string& salt) {
  std::array<unsigned char, 32> output{};
  constexpr int kIterations = 210000;
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  Require(
      BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  BCRYPT_ALG_HANDLE_HMAC_FLAG) >= 0,
      ErrorCode::kStorage, "Cannot initialize password hashing");
  const auto result = BCryptDeriveKeyPBKDF2(
      algorithm, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
      static_cast<ULONG>(password.size()),
      reinterpret_cast<PUCHAR>(const_cast<char*>(salt.data())),
      static_cast<ULONG>(salt.size()), kIterations, output.data(),
      static_cast<ULONG>(output.size()), 0);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  Require(result >= 0, ErrorCode::kStorage, "Password hashing failed");
#else
  Require(PKCS5_PBKDF2_HMAC(password.data(), password.size(),
                            reinterpret_cast<const unsigned char*>(salt.data()),
                            salt.size(), kIterations, EVP_sha256(),
                            output.size(), output.data()) == 1,
          ErrorCode::kStorage, "Password hashing failed");
#endif
  return Hex(output);
}
bool SecureEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char difference = 0;
  for (std::size_t i = 0; i < a.size(); ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}
}  // namespace market
