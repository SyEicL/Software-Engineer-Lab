#ifndef MARKET_REPOSITORY_H_
#define MARKET_REPOSITORY_H_

#include <string>

#include "market/domain.h"

namespace market {
// Save must either replace the complete snapshot or throw without replacing it.
class IRepository {
 public:
  virtual ~IRepository() = default;
  virtual State Load() = 0;
  virtual void Save(const State& state) = 0;
};
class FileRepository final : public IRepository {
 public:
  explicit FileRepository(const std::string& path);
  ~FileRepository() override;
  FileRepository(const FileRepository&) = delete;
  FileRepository& operator=(const FileRepository&) = delete;
  State Load() override;
  void Save(const State& state) override;

 private:
  std::string path_;
  void* lock_ = nullptr;
};
}  // namespace market
#endif  // MARKET_REPOSITORY_H_
