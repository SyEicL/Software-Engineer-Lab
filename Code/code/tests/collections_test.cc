#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "market/service.h"

namespace {
int checks = 0;
void Check(bool valid, const std::string& name) {
  if (!valid) throw std::runtime_error(name);
  ++checks;
}
template <typename F>
void Reject(market::ErrorCode code, F call) {
  try {
    call();
  } catch (const market::Error& error) {
    Check(error.code() == code, "Wrong error code");
    return;
  }
  throw std::runtime_error("Expected rejection");
}
class MemoryRepository : public market::IRepository {
 public:
  market::State state;
  bool fail = false;
  market::State Load() override { return state; }
  void Save(const market::State& next) override {
    if (fail) throw market::Error(market::ErrorCode::kStorage, "Disk full");
    state = next;
  }
};
void Run() {
  using market::ErrorCode;
  MemoryRepository repo;
  market::Pbkdf2Hasher hasher;
  market::Marketplace app(repo, hasher);
  app.BootstrapAdmin("13800000001", "Admin1234");
  auto seller_id = app.Register("13800000002", "Seller123", "seller");
  auto buyer_id = app.Register("13800000003", "Buyer1234", "buyer");
  app.Register("13800000004", "Other1234", "other");
  const auto admin = app.Login("13800000001", "Admin1234");
  const auto seller = app.Login("13800000002", "Seller123");
  const auto buyer = app.Login("13800000003", "Buyer1234");
  const auto other = app.Login("13800000004", "Other1234");
  app.ApplySeller(seller);
  app.ReviewSeller(admin, seller_id, true);
  auto product = app.PublishDraft(seller, "公开书籍", "图书", 2800, "公开描述");
  Reject(ErrorCode::kConflict, [&] { app.AddFavorite(buyer, product); });
  app.SubmitProduct(seller, product);
  app.ReviewProduct(admin, product, true);
  Reject(ErrorCode::kUnauthenticated, [&] { app.Favorites(""); });
  Reject(ErrorCode::kUnauthenticated, [&] { app.AddToCart("", product); });
  Reject(ErrorCode::kNotFound, [&] { app.AddToCart(buyer, 999999); });
  Reject(ErrorCode::kForbidden, [&] { app.AddToCart(seller, product); });
  app.AddFavorite(buyer, product);
  app.AddFavorite(buyer, product);
  app.AddToCart(buyer, product);
  app.AddToCart(buyer, product);
  Check(app.Favorites(buyer).size() == 1 && app.Cart(buyer).size() == 1,
        "Deduplication");
  Check(app.Favorites(other).empty() && app.Cart(admin).empty(), "Ownership");
  app.AddFavorite(other, product);
  app.AddToCart(other, product);
  app.RemoveFavorite(other, product);
  app.ClearCart(other);
  Check(app.Favorites(buyer).size() == 1 && app.Cart(buyer).size() == 1,
        "Removing own collection does not affect another buyer");
  Check(app.MyProducts(seller).front().hold_id == 0, "Adding does not occupy");
  auto old = app.Cart(buyer).front();
  app.EditProduct(seller, product, old.version, "私密草稿", "私密分类", 9900,
                  "私密描述");
  auto hidden = app.Cart(buyer).front();
  Check(
      hidden.title == "公开书籍" && hidden.price == 2800 &&
          hidden.category.empty() &&
          hidden.description.find("私密") == std::string::npos,
      "Unpublished edits are private; collection retains old public snapshot");
  Reject(ErrorCode::kConflict,
         [&] { app.Reserve(buyer, product, hidden.version, "offline"); });
  Check(app.Cart(buyer).size() == 1, "Failed reservation retains cart item");
  app.SubmitProduct(seller, product);
  app.ReviewProduct(admin, product, true);
  Check(app.Cart(buyer).front().price == 9900, "Published price is current");
  Reject(ErrorCode::kConflict,
         [&] { app.Reserve(buyer, product, old.version, "stale"); });
  Check(app.Cart(buyer).size() == 1, "Version conflict retains cart item");
  old = app.Cart(buyer).front();
  repo.fail = true;
  Reject(ErrorCode::kStorage,
         [&] { app.Reserve(buyer, product, old.version, "rollback"); });
  repo.fail = false;
  Check(app.Cart(buyer).size() == 1 && app.Reservations(buyer).empty(),
        "Reservation and cart removal roll back together");
  auto reservation = app.Reserve(buyer, product, old.version, "reserve");
  Check(app.Cart(buyer).empty() && app.Favorites(buyer).size() == 1,
        "Successful reservation removes cart only");
  app.AddFavorite(other, product);
  Reject(ErrorCode::kConflict, [&] { app.AddToCart(other, product); });
  app.Cancel(buyer, reservation, 1);
  app.AddToCart(buyer, product);
  app.Reserve(buyer, product, old.version, "reserve");
  Check(app.Cart(buyer).size() == 1,
        "Retry of cancelled reservation retains cart");
  app.Freeze(admin, buyer_id, true);
  Check(app.Cart(buyer).size() == 1, "Frozen user can read own cart");
  Reject(ErrorCode::kForbidden, [&] { app.AddFavorite(buyer, product); });
  Reject(ErrorCode::kForbidden, [&] { app.AddToCart(buyer, product); });
  app.RemoveFavorite(buyer, product);
  app.RemoveFromCart(buyer, product);
  app.RemoveFromCart(buyer, product);
  Check(app.Cart(buyer).empty() && app.Favorites(buyer).empty(),
        "Frozen cleanup");
  app.Freeze(admin, buyer_id, false);
  app.AddFavorite(buyer, product);
  app.AddToCart(buyer, product);
  app.TakeDown(seller, product);
  app.DeleteProduct(seller, product);
  Check(app.Cart(buyer).front().status == market::ProductStatus::kDeleted,
        "Deleted products remain removable");
  const char* temp = std::getenv("TEMP");
  const std::string path = std::string(temp ? temp : "/tmp") +
                           "/market-collections-" + market::RandomToken() +
                           ".db";
  {
    market::FileRepository disk(path);
    disk.Save(repo.state);
    auto duplicate = repo.state;
    duplicate.cart.push_back(duplicate.cart.front());
    Reject(ErrorCode::kStorage, [&] { disk.Save(duplicate); });
  }
  {
    market::FileRepository disk(path);
    market::Marketplace restored(disk, hasher);
    auto token = restored.Login("13800000003", "Buyer1234");
    Check(restored.Favorites(token).size() == 1 &&
              restored.Cart(token).size() == 1,
          "Both collections survive restart");
    restored.ClearCart(token);
  }
  // The old schema has no collection sections. Preserve even its credentials.
  {
    std::ofstream legacy(path);
    const auto& user = repo.state.users.front();
    legacy << "MARKET 1 2\n1\n1 \"13800000001\" \"Administrator\" \""
           << user.salt << "\" \"" << user.digest
           << "\" 1 0 0\n0\n0\n0\n0\nEND\n";
  }
  {
    market::FileRepository disk(path);
    market::Marketplace restored(disk, hasher);
    auto token = restored.Login("13800000001", "Admin1234");
    Check(restored.Me(token).admin && restored.Cart(token).empty() &&
              restored.Favorites(token).empty(),
          "Legacy database loads without losing login");
    restored.Register("13800000009", "Buyer1234", "new");
  }
  {
    market::FileRepository disk(path);
    Check(disk.Load().users.size() == 2, "Legacy data upgrades on save");
  }
  std::remove(path.c_str());
  std::remove((path + ".lock").c_str());
}
}  // namespace
int main() {
  try {
    Run();
    std::cout << "PASS: " << checks << " collection checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
