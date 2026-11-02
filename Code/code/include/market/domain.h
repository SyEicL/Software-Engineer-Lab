#ifndef MARKET_DOMAIN_H_
#define MARKET_DOMAIN_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace market {
using Id = std::uint64_t;
using Money = std::int64_t;

enum class ErrorCode {
  kValidation,
  kUnauthenticated,
  kForbidden,
  kNotFound,
  kConflict,
  kStorage
};
class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}
  ErrorCode code() const { return code_; }

 private:
  ErrorCode code_;
};
enum class AccountStatus { kNormal, kFrozen };
enum class SellerStatus { kNone, kPending, kApproved, kRejected };
enum class ProductStatus {
  kDraft,
  kPending,
  kPublished,
  kRejected,
  kOffline,
  kDeleted
};
enum class ReservationStatus { kPending, kConfirmed, kCancelled, kCompleted };
enum class OrderStatus {
  kUnpaid,
  kPaid,
  kShipped,
  kCompleted,
  kClosed,
  kRefundRequested,
  kRefunded
};

struct User {
  Id id = 0;
  std::string phone;
  std::string nickname;
  std::string salt;
  std::string digest;
  bool admin = false;
  AccountStatus status = AccountStatus::kNormal;
  SellerStatus seller = SellerStatus::kNone;
};
struct UserView {
  Id id;
  std::string nickname;
  bool admin;
  AccountStatus status;
  SellerStatus seller;
};
struct Product {
  Id id = 0;
  Id seller_id = 0;
  std::string title;
  std::string category;
  std::string description;
  Money price = 0;
  ProductStatus status = ProductStatus::kDraft;
  std::uint64_t version = 1;
  Id hold_id = 0;
};
struct Reservation {
  Id id = 0;
  Id buyer_id = 0;
  Id product_id = 0;
  std::string request_key;
  ReservationStatus status = ReservationStatus::kPending;
  std::uint64_t version = 1;
};
struct Order {
  Id id = 0;
  Id reservation_id = 0;
  Id buyer_id = 0;
  Id seller_id = 0;
  Id product_id = 0;
  Money amount = 0;
  std::string title_snapshot;
  std::string address_snapshot;
  std::string tracking_number;
  std::string payment_key;
  OrderStatus status = OrderStatus::kUnpaid;
  OrderStatus before_refund = OrderStatus::kPaid;
  bool settlement_held = false;
};
struct Audit {
  Id id = 0;
  Id actor_id = 0;
  std::int64_t timestamp = 0;
  std::string action;
  std::string detail;
};
// One physical item per user/product pair; adding does not reserve stock.
struct SavedItem {
  Id user_id = 0;
  Id product_id = 0;
  std::string title_snapshot;
  Money price_snapshot = 0;
};
struct State {
  Id next_id = 1;
  std::vector<User> users;
  std::vector<Product> products;
  std::vector<Reservation> reservations;
  std::vector<Order> orders;
  std::vector<Audit> audits;
  std::vector<SavedItem> favorites;
  std::vector<SavedItem> cart;
};
struct Search {
  std::string keyword;
  std::string category;
  Money min_price = 0;
  Money max_price = 100000000;
};
struct Statistics {
  std::size_t products = 0;
  std::size_t completed_orders = 0;
  Money completed_amount = 0;
};
inline void Require(bool valid, ErrorCode code, const std::string& message) {
  if (!valid) throw Error(code, message);
}
}  // namespace market
#endif  // MARKET_DOMAIN_H_
