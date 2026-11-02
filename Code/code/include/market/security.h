#ifndef MARKET_SECURITY_H_
#define MARKET_SECURITY_H_

#include <string>

namespace market {
class IPasswordHasher {
 public:
  virtual ~IPasswordHasher() = default;
  virtual std::string NewSalt() = 0;
  virtual std::string Hash(const std::string& password,
                           const std::string& salt) = 0;
};
class Pbkdf2Hasher final : public IPasswordHasher {
 public:
  std::string NewSalt() override;
  std::string Hash(const std::string& password,
                   const std::string& salt) override;
};
std::string RandomToken();
bool SecureEqual(const std::string& a, const std::string& b);
}  // namespace market
#endif  // MARKET_SECURITY_H_
