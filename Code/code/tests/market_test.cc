#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "market/service.h"

namespace {
int checks = 0;
void Check(bool condition, const std::string& name) {
  if (!condition) throw std::runtime_error("FAILED: " + name);
  ++checks;
}
template <typename F>
void Expect(market::ErrorCode code, F fn) {
  try {
    fn();
  } catch (const market::Error& error) {
    Check(error.code() == code, "expected business error code");
    return;
  }
  throw std::runtime_error("Expected error was not thrown");
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
std::string TempPath() {
  const auto temp = std::getenv("TEMP");
  return std::string(temp ? temp : "/tmp") + "/market-test-" +
         market::RandomToken() + ".db";
}
void BusinessTests() {
  using market::ErrorCode;
  MemoryRepository repo;
  market::Pbkdf2Hasher hash;
  market::Marketplace app(repo, hash);
  const auto admin_id = app.BootstrapAdmin("13800000001", "Admin12345");
  const auto admin = app.Login("13800000001", "Admin12345");
  const auto seller_id = app.Register("13800000002", "Seller123", "same name");
  const auto buyer_id = app.Register("13800000003", "Buyer1234", "same name");
  app.Register("13800000004", "Other1234", "other");
  const auto seller = app.Login("13800000002", "Seller123");
  const auto buyer = app.Login("13800000003", "Buyer1234");
  const auto other = app.Login("13800000004", "Other1234");
  Check(admin_id != seller_id && buyer_id != seller_id, "unique IDs");
  Expect(ErrorCode::kForbidden,
         [&] { app.BootstrapAdmin("13800000005", "Admin12345"); });
  Expect(ErrorCode::kConflict,
         [&] { app.Register("13800000003", "Buyer1234", "duplicate"); });
  Expect(ErrorCode::kValidation,
         [&] { app.Register("13800000005", "12345678", "bad"); });
  Expect(ErrorCode::kUnauthenticated,
         [&] { app.Login("13800000003", "wrong"); });
  Expect(ErrorCode::kForbidden,
         [&] { app.PublishDraft(buyer, "Book", "books", 2800, "Clean"); });
  app.ApplySeller(seller);
  app.ReviewSeller(admin, seller_id, true);
  auto new_product = [&] {
    auto id = app.PublishDraft(seller, "C++ Book", "books", 2800, "Clean");
    app.SubmitProduct(seller, id);
    app.ReviewProduct(admin, id, true);
    return id;
  };
  auto product = new_product();
  auto pv = app.SearchProducts({"C++", "books", 2000, 3000}).front();
  Check(pv.id == product, "combined search");
  Expect(ErrorCode::kValidation, [&] { app.SearchProducts({"", "", 30, 10}); });
  Expect(ErrorCode::kForbidden,
         [&] { app.Reserve(seller, product, pv.version, "self"); });
  Expect(ErrorCode::kConflict,
         [&] { app.Reserve(buyer, product, pv.version - 1, "old"); });
  auto reservation = app.Reserve(buyer, product, pv.version, "r1");
  Check(app.Reserve(buyer, product, pv.version, "r1") == reservation,
        "same-key reservation idempotency");
  Check(app.Reserve(buyer, product, pv.version, "r2") == reservation,
        "different-key active reservation deduplication");
  Expect(ErrorCode::kConflict,
         [&] { app.Reserve(other, product, pv.version, "competing"); });
  Expect(ErrorCode::kForbidden, [&] { app.Confirm(other, reservation, 1); });
  app.Confirm(seller, reservation, 1);
  auto order = app.CreateOrder(buyer, reservation, "Campus address");
  Check(app.CreateOrder(buyer, reservation, "ignored on retry") == order,
        "one order per reservation");
  Expect(ErrorCode::kConflict, [&] { app.Cancel(buyer, reservation, 2); });
  Expect(ErrorCode::kForbidden, [&] { app.Pay(other, order, "steal"); });
  app.Pay(buyer, order, "pay1");
  app.Pay(buyer, order, "pay1");
  Expect(ErrorCode::kConflict, [&] { app.Pay(buyer, order, "pay2"); });
  Expect(ErrorCode::kConflict, [&] { app.Receive(buyer, order); });
  app.Ship(seller, order, "TRACK-1");
  app.Freeze(admin, seller_id, true);
  Check(app.SearchProducts({}).empty(), "freeze removes public listings");
  app.Receive(buyer, order);
  Check(app.Orders(buyer).front().settlement_held,
        "shipped order continues but settlement is held");
  app.Freeze(admin, seller_id, false);
  Check(app.SearchProducts({}).empty(), "unfreeze does not republish");
  Expect(ErrorCode::kConflict, [&] { app.SubmitProduct(seller, product); });
  Check(app.Stats(seller).completed_amount == 2800, "seller statistics");
  Check(app.Orders(other).empty(), "order ownership filtering");
  product = new_product();
  pv = app.SearchProducts({}).front();
  reservation = app.Reserve(buyer, product, pv.version, "r3");
  app.Confirm(seller, reservation, 1);
  order = app.CreateOrder(buyer, reservation, "Address");
  app.Pay(buyer, order, "pay3");
  app.Freeze(admin, seller_id, true);
  app.RequestRefund(buyer, order);
  Expect(ErrorCode::kForbidden, [&] { app.ReviewRefund(seller, order, true); });
  app.ReviewRefund(admin, order, false);
  Check(app.Orders(buyer).back().status == market::OrderStatus::kPaid,
        "refund rejection restores original order phase");
  app.RequestRefund(buyer, order);
  app.ReviewRefund(admin, order, true);
  app.ReviewRefund(admin, order, true);
  Check(app.Orders(buyer).back().status == market::OrderStatus::kRefunded,
        "frozen seller refund routed to admin");
  Check(!app.Orders(buyer).back().settlement_held,
        "fully refunded order has no remaining settlement hold");
  app.Freeze(admin, seller_id, false);
  for (int i = 0; i < 5; ++i) {
    Expect(ErrorCode::kForbidden, [&] { app.Users(buyer); });
  }
  bool alert = false;
  for (const auto& a : app.Audits(admin)) {
    if (a.actor_id == buyer_id && a.action == "security_alert") alert = true;
  }
  Check(alert, "five denials generate security alert");
  product = new_product();
  pv = app.MyProducts(seller).back();
  Expect(ErrorCode::kUnauthenticated,
         [&] { app.Reserve("missing", product, pv.version, "guest"); });
  Expect(ErrorCode::kConflict,
         [&] { app.Reserve(buyer, product, pv.version, "r1"); });
  reservation = app.Reserve(buyer, product, pv.version, "cancel-test");
  app.Cancel(buyer, reservation, 1);
  pv = app.MyProducts(seller).back();
  Check(pv.hold_id == 0, "cancel releases product");
  reservation = app.Reserve(buyer, product, pv.version, "new-attempt");
  app.Freeze(admin, buyer_id, true);
  Check(app.Reservations(buyer).back().status ==
            market::ReservationStatus::kCancelled,
        "freezing buyer cancels pre-order reservation");
  Expect(ErrorCode::kForbidden,
         [&] { app.Reserve(buyer, product, pv.version, "frozen"); });
  app.Freeze(admin, buyer_id, false);
  pv = app.MyProducts(seller).back();
  reservation = app.Reserve(buyer, product, pv.version, "unpaid");
  app.Confirm(seller, reservation, 1);
  order = app.CreateOrder(buyer, reservation, "Unpaid address");
  app.CancelOrder(buyer, order);
  Check(app.Orders(buyer).back().status == market::OrderStatus::kClosed &&
            app.MyProducts(seller).back().hold_id == 0,
        "unpaid cancellation closes order and releases product");
  Expect(ErrorCode::kConflict, [&] { app.Pay(buyer, order, "closed"); });
  app.TakeDown(seller, product);
  pv = app.MyProducts(seller).back();
  app.EditProduct(seller, product, pv.version, "中文书籍", "图书", 3000,
                  "保存中文和带空格的描述 UTF-8");
  Check(app.MyProducts(seller).back().status == market::ProductStatus::kDraft,
        "editing requires review again");
  app.DeleteProduct(seller, product);
  Expect(ErrorCode::kConflict, [&] { app.SubmitProduct(seller, product); });
  const auto roundtrip_path = TempPath();
  {
    market::FileRepository disk(roundtrip_path);
    disk.Save(repo.state);
  }
  {
    market::FileRepository disk(roundtrip_path);
    market::Marketplace restored(disk, hash);
    auto restored_buyer = restored.Login("13800000003", "Buyer1234");
    Check(restored.Orders(restored_buyer).size() == 3,
          "all order records survive restart");
    auto restored_seller = restored.Login("13800000002", "Seller123");
    Check(restored.MyProducts(restored_seller).back().title == "中文书籍",
          "UTF-8 product survives restart");
    Expect(ErrorCode::kUnauthenticated, [&] { restored.Me(buyer); });
  }
  std::remove(roundtrip_path.c_str());
  std::remove((roundtrip_path + ".lock").c_str());
  const auto before = repo.state.products.size();
  repo.fail = true;
  Expect(ErrorCode::kStorage,
         [&] { app.PublishDraft(seller, "Rollback", "books", 500, "test"); });
  repo.fail = false;
  Check(repo.state.products.size() == before &&
            app.MyProducts(seller).size() == before,
        "save failure rolls back");
  app.Logout(buyer);
  Expect(ErrorCode::kUnauthenticated, [&] { app.Me(buyer); });
}
void PersistenceTests() {
  const auto path = TempPath();
  market::Pbkdf2Hasher hasher;
  {
    market::FileRepository repository(path);
    market::Marketplace app(repository, hasher);
    app.BootstrapAdmin("13900000001", "Secret1234");
    Expect(market::ErrorCode::kStorage,
           [&] { market::FileRepository second(path); });
  }
  {
    market::FileRepository repository(path);
    market::Marketplace app(repository, hasher);
    auto session = app.Login("13900000001", "Secret1234");
    Check(app.Me(session).admin, "reload and password verification");
  }
  std::ifstream in(path);
  const std::string data((std::istreambuf_iterator<char>(in)), {});
  Check(data.find("Secret1234") == std::string::npos, "no plaintext password");
  in.close();
  {
    std::ofstream corrupt(path);
    corrupt << "MARKET 999 broken\n";
  }
  {
    market::FileRepository repository(path);
    Expect(market::ErrorCode::kStorage, [&] { repository.Load(); });
  }
  std::remove(path.c_str());
  std::remove((path + ".lock").c_str());
}
}  // namespace
int main() {
  try {
    BusinessTests();
    PersistenceTests();
    std::cout << "PASS: " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
