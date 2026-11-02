#include "market/service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace market {
namespace {
template <typename T>
T& Find(std::vector<T>& values, Id id) {
  for (auto& value : values)
    if (value.id == id) return value;
  throw Error(ErrorCode::kNotFound, "Record not found");
}
template <typename T>
const T& Find(const std::vector<T>& values, Id id) {
  for (const auto& value : values)
    if (value.id == id) return value;
  throw Error(ErrorCode::kNotFound, "Record not found");
}
void Text(const std::string& value, std::size_t max = 200) {
  Require(
      !value.empty() && value.size() <= max &&
          std::none_of(value.begin(), value.end(),
                       [](unsigned char c) { return c < 32 || c == 127; }) &&
          std::any_of(value.begin(), value.end(),
                      [](unsigned char c) { return !std::isspace(c); }),
      ErrorCode::kValidation, "Text is empty, too long or contains controls");
}
void Credentials(const std::string& phone, const std::string& password) {
  Require(phone.size() == 11 &&
              std::all_of(phone.begin(), phone.end(),
                          [](unsigned char c) { return c >= '0' && c <= '9'; }),
          ErrorCode::kValidation, "Phone must contain 11 digits");
  bool letter = false, digit = false;
  for (unsigned char c : password) {
    letter = letter || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    digit = digit || (c >= '0' && c <= '9');
    Require(c >= 33 && c <= 126, ErrorCode::kValidation,
            "MVP passwords use visible ASCII characters");
  }
  Require(password.size() >= 8 && password.size() <= 20 && letter && digit,
          ErrorCode::kValidation,
          "Password needs 8-20 chars, letters and digits");
}
void ProductFields(const std::string& title, const std::string& category,
                   Money price, const std::string& description) {
  Text(title, 120);
  Text(category, 60);
  Text(description, 1000);
  Require(price > 0 && price <= 100000000, ErrorCode::kValidation,
          "Price must be between 1 and 100000000 cents");
}
void Version(std::uint64_t actual, std::uint64_t expected) {
  Require(actual == expected, ErrorCode::kConflict,
          "State has changed; reload and retry");
}
bool HasOrder(const State& s, Id reservation) {
  return std::any_of(s.orders.begin(), s.orders.end(), [&](const Order& o) {
    return o.reservation_id == reservation;
  });
}
void RemoveCartEntry(State* state, Id buyer, Id product) {
  auto& cart = state->cart;
  cart.erase(std::remove_if(cart.begin(), cart.end(),
                            [&](const SavedItem& item) {
                              return item.user_id == buyer &&
                                     item.product_id == product;
                            }),
             cart.end());
}
}  // namespace

Marketplace::Marketplace(IRepository& repository, IPasswordHasher& hasher)
    : repository_(repository), hasher_(hasher), state_(repository.Load()) {}

void Marketplace::Persist(State candidate) {
  repository_.Save(candidate);
  state_ = std::move(candidate);
}
void Marketplace::Log(State* s, Id actor, const std::string& action,
                      const std::string& detail) {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  s->audits.push_back(
      {s->next_id++, actor,
       std::chrono::duration_cast<std::chrono::seconds>(now).count(), action,
       detail});
}
const User& Marketplace::Actor(const std::string& session) const {
  const auto it = sessions_.find(session);
  Require(it != sessions_.end(), ErrorCode::kUnauthenticated, "Please log in");
  return Find(state_.users, it->second);
}
UserView Marketplace::View(const User& u) {
  return {u.id, u.nickname, u.admin, u.status, u.seller};
}
void Marketplace::Normal(const User& u) {
  Require(u.status == AccountStatus::kNormal, ErrorCode::kForbidden,
          "Frozen account: new transactions are disabled");
}
void Marketplace::Seller(const User& u) {
  Normal(u);
  Require(u.seller == SellerStatus::kApproved, ErrorCode::kForbidden,
          "Seller approval required");
}
void Marketplace::Admin(const User& u) {
  Normal(u);
  Require(u.admin, ErrorCode::kForbidden, "Administrator permission required");
}
void Marketplace::Update(const std::string& session, const std::string& action,
                         const Mutation& mutation) {
  const User actor = Actor(session);
  State candidate = state_;
  try {
    mutation(candidate, actor);
    Log(&candidate, actor.id, action, "success");
    Persist(std::move(candidate));
  } catch (const Error& error) {
    if (error.code() == ErrorCode::kForbidden) {
      State rejected = state_;
      Log(&rejected, actor.id, "access_denied", action);
      int recent = 0;
      const auto now = rejected.audits.back().timestamp;
      for (const auto& a : rejected.audits) {
        if (a.actor_id == actor.id && a.action == "access_denied" &&
            a.timestamp > now - 60)
          ++recent;
      }
      if (recent == 5)
        Log(&rejected, actor.id, "security_alert", "5 denials/60s");
      Persist(std::move(rejected));
    }
    throw;
  }
}
Id Marketplace::Register(const std::string& phone, const std::string& password,
                         const std::string& nickname) {
  Credentials(phone, password);
  Text(nickname, 60);
  for (const auto& u : state_.users) {
    Require(u.phone != phone, ErrorCode::kConflict,
            "Phone is already registered");
  }
  State next = state_;
  User user;
  user.id = next.next_id++;
  user.phone = phone;
  user.nickname = nickname;
  user.salt = hasher_.NewSalt();
  user.digest = hasher_.Hash(password, user.salt);
  next.users.push_back(user);
  Log(&next, user.id, "register", "success");
  Persist(std::move(next));
  return user.id;
}
Id Marketplace::BootstrapAdmin(const std::string& phone,
                               const std::string& password) {
  Require(state_.users.empty(), ErrorCode::kForbidden,
          "Bootstrap requires an empty database");
  Credentials(phone, password);
  State next = state_;
  User u;
  u.id = next.next_id++;
  u.phone = phone;
  u.nickname = "Administrator";
  u.admin = true;
  u.salt = hasher_.NewSalt();
  u.digest = hasher_.Hash(password, u.salt);
  next.users.push_back(u);
  Log(&next, u.id, "bootstrap", "initial administrator");
  Persist(std::move(next));
  return u.id;
}
std::string Marketplace::Login(const std::string& phone,
                               const std::string& password) {
  for (const auto& u : state_.users) {
    if (u.phone == phone &&
        SecureEqual(hasher_.Hash(password, u.salt), u.digest)) {
      auto token = RandomToken();
      sessions_[token] = u.id;
      return token;
    }
  }
  throw Error(ErrorCode::kUnauthenticated, "Invalid credentials");
}
void Marketplace::Logout(const std::string& session) {
  sessions_.erase(session);
}
UserView Marketplace::Me(const std::string& session) const {
  return View(Actor(session));
}
std::vector<UserView> Marketplace::Users(const std::string& session) {
  std::vector<UserView> result;
  Update(session, "list_users", [&](State& s, const User& actor) {
    Admin(actor);
    for (const auto& u : s.users) result.push_back(View(u));
  });
  return result;
}
void Marketplace::ApplySeller(const std::string& session) {
  Update(session, "apply_seller", [](State& s, const User& actor) {
    Normal(actor);
    auto& u = Find(s.users, actor.id);
    Require(!u.admin && (u.seller == SellerStatus::kNone ||
                         u.seller == SellerStatus::kRejected),
            ErrorCode::kConflict, "Seller application is not allowed now");
    u.seller = SellerStatus::kPending;
  });
}
void Marketplace::ReviewSeller(const std::string& session, Id id,
                               bool approved) {
  Update(session, "review_seller", [&](State& s, const User& actor) {
    Admin(actor);
    auto& u = Find(s.users, id);
    Normal(u);
    Require(u.seller == SellerStatus::kPending, ErrorCode::kConflict,
            "No pending seller application");
    u.seller = approved ? SellerStatus::kApproved : SellerStatus::kRejected;
  });
}
void Marketplace::Freeze(const std::string& session, Id id, bool frozen) {
  Update(session, "freeze_account", [&](State& s, const User& actor) {
    Admin(actor);
    auto& u = Find(s.users, id);
    Require(!u.admin, ErrorCode::kForbidden, "Cannot freeze an administrator");
    u.status = frozen ? AccountStatus::kFrozen : AccountStatus::kNormal;
    for (auto& p : s.products) {
      if (frozen && p.seller_id == id &&
          p.status == ProductStatus::kPublished) {
        p.status = ProductStatus::kOffline;
        ++p.version;
      }
    }
    for (auto& r : s.reservations) {
      auto& p = Find(s.products, r.product_id);
      if (frozen && (p.seller_id == id || r.buyer_id == id) &&
          r.status != ReservationStatus::kCancelled &&
          r.status != ReservationStatus::kCompleted && !HasOrder(s, r.id)) {
        r.status = ReservationStatus::kCancelled;
        ++r.version;
        p.hold_id = 0;
        ++p.version;
      }
    }
    for (auto& o : s.orders) {
      if (o.seller_id != id) continue;
      o.settlement_held = frozen || o.status == OrderStatus::kRefundRequested;
      if (frozen && o.status == OrderStatus::kPaid) {
        Log(&s, actor.id, "buyer_notice", "order=" + std::to_string(o.id));
      }
    }
  });
}
Id Marketplace::PublishDraft(const std::string& session,
                             const std::string& title,
                             const std::string& category, Money price,
                             const std::string& description) {
  ProductFields(title, category, price, description);
  Id id = 0;
  Update(session, "create_product", [&](State& s, const User& actor) {
    Seller(actor);
    Product p;
    p.id = id = s.next_id++;
    p.seller_id = actor.id;
    p.title = title;
    p.category = category;
    p.price = price;
    p.description = description;
    s.products.push_back(p);
  });
  return id;
}
void Marketplace::EditProduct(const std::string& session, Id id,
                              std::uint64_t version, const std::string& title,
                              const std::string& category, Money price,
                              const std::string& description) {
  ProductFields(title, category, price, description);
  Update(session, "edit_product", [&](State& s, const User& actor) {
    Seller(actor);
    auto& p = Find(s.products, id);
    Require(p.seller_id == actor.id, ErrorCode::kForbidden, "Not your product");
    Version(p.version, version);
    Require(!p.hold_id && p.status != ProductStatus::kDeleted,
            ErrorCode::kConflict, "Product is occupied or deleted");
    p.title = title;
    p.category = category;
    p.price = price;
    p.description = description;
    p.status = ProductStatus::kDraft;
    ++p.version;
  });
}
void Marketplace::SubmitProduct(const std::string& session, Id id) {
  Update(session, "submit_product", [&](State& s, const User& actor) {
    Seller(actor);
    auto& p = Find(s.products, id);
    Require(p.seller_id == actor.id, ErrorCode::kForbidden, "Not your product");
    Require(!p.hold_id && (p.status == ProductStatus::kDraft ||
                           p.status == ProductStatus::kRejected ||
                           p.status == ProductStatus::kOffline),
            ErrorCode::kConflict, "Product cannot be submitted");
    p.status = ProductStatus::kPending;
    ++p.version;
  });
}
void Marketplace::ReviewProduct(const std::string& session, Id id,
                                bool approved) {
  Update(session, "review_product", [&](State& s, const User& actor) {
    Admin(actor);
    auto& p = Find(s.products, id);
    Seller(Find(s.users, p.seller_id));
    Require(p.status == ProductStatus::kPending, ErrorCode::kConflict,
            "Product is not pending review");
    p.status = approved ? ProductStatus::kPublished : ProductStatus::kRejected;
    ++p.version;
  });
}
void Marketplace::TakeDown(const std::string& session, Id id) {
  Update(session, "take_down", [&](State& s, const User& actor) {
    Normal(actor);
    auto& p = Find(s.products, id);
    Require(actor.admin || p.seller_id == actor.id, ErrorCode::kForbidden,
            "Not your product");
    Require(p.status == ProductStatus::kPublished, ErrorCode::kConflict,
            "Product is not published");
    p.status = ProductStatus::kOffline;
    ++p.version;
    // A listing change does not cancel an existing order.
    for (auto& r : s.reservations) {
      if (r.id == p.hold_id && !HasOrder(s, r.id)) {
        r.status = ReservationStatus::kCancelled;
        ++r.version;
        p.hold_id = 0;
      }
    }
  });
}
void Marketplace::DeleteProduct(const std::string& session, Id id) {
  Update(session, "delete_product", [&](State& s, const User& actor) {
    Seller(actor);
    auto& p = Find(s.products, id);
    Require(p.seller_id == actor.id, ErrorCode::kForbidden, "Not your product");
    Require(!p.hold_id && p.status != ProductStatus::kPublished &&
                p.status != ProductStatus::kPending,
            ErrorCode::kConflict, "Take down the unoccupied product first");
    p.status = ProductStatus::kDeleted;
    ++p.version;
  });
}
std::vector<Product> Marketplace::SearchProducts(const Search& search) const {
  Require(search.min_price >= 0 && search.max_price >= search.min_price,
          ErrorCode::kValidation, "Invalid price range");
  std::vector<Product> result;
  for (const auto& p : state_.products) {
    const auto& seller = Find(state_.users, p.seller_id);
    if (p.status == ProductStatus::kPublished &&
        seller.status == AccountStatus::kNormal &&
        seller.seller == SellerStatus::kApproved &&
        p.title.find(search.keyword) != std::string::npos &&
        (search.category.empty() || p.category == search.category) &&
        p.price >= search.min_price && p.price <= search.max_price) {
      result.push_back(p);
    }
  }
  return result;
}
std::vector<Product> Marketplace::MyProducts(const std::string& session) const {
  const auto& actor = Actor(session);
  std::vector<Product> result;
  for (const auto& p : state_.products) {
    if (actor.admin || p.seller_id == actor.id) result.push_back(p);
  }
  return result;
}
Id Marketplace::Reserve(const std::string& session, Id id,
                        std::uint64_t version, const std::string& key) {
  Text(key, 80);
  Id result = 0;
  Update(session, "reserve", [&](State& s, const User& actor) {
    // A retry may read its original result even after the account is frozen.
    for (const auto& r : s.reservations) {
      if (r.buyer_id == actor.id && r.request_key == key) {
        Require(r.product_id == id, ErrorCode::kConflict,
                "Request key belongs to another product");
        result = r.id;
        if ((r.status == ReservationStatus::kPending ||
             r.status == ReservationStatus::kConfirmed) &&
            Find(s.products, id).hold_id == r.id)
          RemoveCartEntry(&s, actor.id, id);
        return;
      }
    }
    Normal(actor);
    auto& p = Find(s.products, id);
    Require(p.seller_id != actor.id, ErrorCode::kForbidden,
            "Cannot buy yourself");
    Seller(Find(s.users, p.seller_id));
    for (const auto& r : s.reservations) {
      if (r.buyer_id == actor.id && r.id == p.hold_id &&
          (r.status == ReservationStatus::kPending ||
           r.status == ReservationStatus::kConfirmed)) {
        result = r.id;
        RemoveCartEntry(&s, actor.id, id);
        return;
      }
    }
    Version(p.version, version);
    Require(p.status == ProductStatus::kPublished && !p.hold_id,
            ErrorCode::kConflict, "Product is unavailable");
    Reservation r;
    r.id = result = s.next_id++;
    r.buyer_id = actor.id;
    r.product_id = id;
    r.request_key = key;
    s.reservations.push_back(r);
    p.hold_id = result;
    ++p.version;
    RemoveCartEntry(&s, actor.id, id);
  });
  return result;
}
void Marketplace::Confirm(const std::string& session, Id id,
                          std::uint64_t version) {
  Update(session, "confirm_reservation", [&](State& s, const User& actor) {
    Seller(actor);
    auto& r = Find(s.reservations, id);
    auto& p = Find(s.products, r.product_id);
    Require(p.seller_id == actor.id, ErrorCode::kForbidden, "Not your sale");
    Normal(Find(s.users, r.buyer_id));
    Version(r.version, version);
    Require(r.status == ReservationStatus::kPending && p.hold_id == id &&
                p.status == ProductStatus::kPublished,
            ErrorCode::kConflict, "Reservation cannot be confirmed");
    r.status = ReservationStatus::kConfirmed;
    ++r.version;
  });
}
void Marketplace::Cancel(const std::string& session, Id id,
                         std::uint64_t version) {
  Update(session, "cancel_reservation", [&](State& s, const User& actor) {
    auto& r = Find(s.reservations, id);
    auto& p = Find(s.products, r.product_id);
    Require(r.buyer_id == actor.id || p.seller_id == actor.id,
            ErrorCode::kForbidden, "Not your reservation");
    Version(r.version, version);
    Require(!HasOrder(s, id) && (r.status == ReservationStatus::kPending ||
                                 r.status == ReservationStatus::kConfirmed),
            ErrorCode::kConflict, "Use order cancellation or refund instead");
    r.status = ReservationStatus::kCancelled;
    ++r.version;
    if (p.hold_id == id) p.hold_id = 0;
    ++p.version;
  });
}
std::vector<Reservation> Marketplace::Reservations(
    const std::string& session) const {
  const auto& actor = Actor(session);
  std::vector<Reservation> result;
  for (const auto& r : state_.reservations) {
    if (r.buyer_id == actor.id || actor.admin ||
        Find(state_.products, r.product_id).seller_id == actor.id) {
      result.push_back(r);
    }
  }
  return result;
}
Id Marketplace::CreateOrder(const std::string& session, Id id,
                            const std::string& address) {
  Text(address, 300);
  Id result = 0;
  Update(session, "create_order", [&](State& s, const User& actor) {
    auto& r = Find(s.reservations, id);
    Require(r.buyer_id == actor.id, ErrorCode::kForbidden,
            "Not your reservation");
    for (const auto& o : s.orders) {
      if (o.reservation_id == id) {
        result = o.id;
        return;
      }
    }
    Normal(actor);
    const auto& p = Find(s.products, r.product_id);
    Seller(Find(s.users, p.seller_id));
    Require(r.status == ReservationStatus::kConfirmed && p.hold_id == id &&
                p.status == ProductStatus::kPublished,
            ErrorCode::kConflict, "Confirmed available reservation required");
    Order o;
    o.id = result = s.next_id++;
    o.reservation_id = id;
    o.buyer_id = actor.id;
    o.seller_id = p.seller_id;
    o.product_id = p.id;
    o.amount = p.price;
    o.title_snapshot = p.title;
    o.address_snapshot = address;
    s.orders.push_back(o);
  });
  return result;
}
void Marketplace::Pay(const std::string& session, Id id,
                      const std::string& key) {
  Text(key, 80);
  Update(session, "simulate_payment", [&](State& s, const User& actor) {
    auto& o = Find(s.orders, id);
    Require(o.buyer_id == actor.id, ErrorCode::kForbidden, "Not your order");
    if (o.payment_key == key) return;
    Normal(actor);
    Seller(Find(s.users, o.seller_id));
    Require(o.status == OrderStatus::kUnpaid && o.payment_key.empty(),
            ErrorCode::kConflict, "Order is already paid or closed");
    for (const auto& other : s.orders) {
      Require(other.buyer_id != actor.id || other.payment_key != key,
              ErrorCode::kConflict,
              "Payment key already used by another order");
    }
    o.payment_key = key;
    o.status = OrderStatus::kPaid;
  });
}
void Marketplace::CancelOrder(const std::string& session, Id id) {
  Update(session, "cancel_order", [&](State& s, const User& actor) {
    auto& o = Find(s.orders, id);
    Require(o.buyer_id == actor.id, ErrorCode::kForbidden, "Not your order");
    Require(o.status == OrderStatus::kUnpaid, ErrorCode::kConflict,
            "Only unpaid orders can be cancelled");
    o.status = OrderStatus::kClosed;
    auto& r = Find(s.reservations, o.reservation_id);
    r.status = ReservationStatus::kCancelled;
    ++r.version;
    auto& p = Find(s.products, o.product_id);
    p.hold_id = 0;
    ++p.version;
  });
}
void Marketplace::Ship(const std::string& session, Id id,
                       const std::string& tracking) {
  Text(tracking, 100);
  Update(session, "ship", [&](State& s, const User& actor) {
    Seller(actor);
    auto& o = Find(s.orders, id);
    Require(o.seller_id == actor.id, ErrorCode::kForbidden, "Not your sale");
    Require(o.status == OrderStatus::kPaid, ErrorCode::kConflict,
            "Order is not ready to ship");
    o.tracking_number = tracking;
    o.status = OrderStatus::kShipped;
  });
}
void Marketplace::Receive(const std::string& session, Id id) {
  Update(session, "receive", [&](State& s, const User& actor) {
    auto& o = Find(s.orders, id);
    Require(o.buyer_id == actor.id, ErrorCode::kForbidden, "Not your order");
    Require(o.status == OrderStatus::kShipped, ErrorCode::kConflict,
            "Order has not shipped");
    o.status = OrderStatus::kCompleted;
    auto& r = Find(s.reservations, o.reservation_id);
    r.status = ReservationStatus::kCompleted;
    ++r.version;
    auto& p = Find(s.products, o.product_id);
    p.status = ProductStatus::kOffline;
    ++p.version;  // Keep the completed hold to prevent resale.
  });
}
void Marketplace::RequestRefund(const std::string& session, Id id) {
  Update(session, "request_refund", [&](State& s, const User& actor) {
    auto& o = Find(s.orders, id);
    Require(o.buyer_id == actor.id, ErrorCode::kForbidden, "Not your order");
    Require(o.status == OrderStatus::kPaid || o.status == OrderStatus::kShipped,
            ErrorCode::kConflict, "Refund is not available in this state");
    o.before_refund = o.status;
    o.status = OrderStatus::kRefundRequested;
    o.settlement_held = true;
  });
}
void Marketplace::ReviewRefund(const std::string& session, Id id,
                               bool approve) {
  Update(session, "simulate_refund", [&](State& s, const User& actor) {
    Normal(actor);
    auto& o = Find(s.orders, id);
    const auto& seller = Find(s.users, o.seller_id);
    Require(actor.admin || (actor.id == seller.id &&
                            seller.status == AccountStatus::kNormal),
            ErrorCode::kForbidden, "Only seller or administrator may review");
    if (approve && o.status == OrderStatus::kRefunded) return;
    Require(o.status == OrderStatus::kRefundRequested, ErrorCode::kConflict,
            "No pending refund");
    o.status = approve ? OrderStatus::kRefunded : o.before_refund;
    o.settlement_held = !approve && seller.status == AccountStatus::kFrozen;
    if (approve) {
      auto& r = Find(s.reservations, o.reservation_id);
      r.status = ReservationStatus::kCancelled;
      ++r.version;
      auto& p = Find(s.products, o.product_id);
      p.hold_id = 0;
      p.status = ProductStatus::kOffline;
      ++p.version;
    }
  });
}
std::vector<Order> Marketplace::Orders(const std::string& session) const {
  const auto& actor = Actor(session);
  std::vector<Order> result;
  for (const auto& o : state_.orders) {
    if (actor.admin || actor.id == o.buyer_id || actor.id == o.seller_id) {
      result.push_back(o);
    }
  }
  return result;
}
Statistics Marketplace::Stats(const std::string& session) {
  Statistics result;
  Update(session, "statistics", [&](State& s, const User& actor) {
    Require(actor.admin || actor.seller == SellerStatus::kApproved,
            ErrorCode::kForbidden, "Seller or administrator required");
    for (const auto& p : s.products) {
      if (actor.admin || p.seller_id == actor.id) ++result.products;
    }
    for (const auto& o : s.orders) {
      if ((actor.admin || o.seller_id == actor.id) &&
          o.status == OrderStatus::kCompleted) {
        ++result.completed_orders;
        result.completed_amount += o.amount;
      }
    }
  });
  return result;
}
std::vector<Audit> Marketplace::Audits(const std::string& session) {
  std::vector<Audit> result;
  Update(session, "read_audits", [&](State& s, const User& actor) {
    Admin(actor);
    result = s.audits;
  });
  return result;
}
}  // namespace market
