#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "market/service.h"

namespace {
class Args {
 public:
  explicit Args(const std::string& line) : input_(line) {}
  template <typename T>
  T Get() {
    std::string value;
    market::Require(static_cast<bool>(input_ >> std::quoted(value)),
                    market::ErrorCode::kValidation, "Missing argument");
    if constexpr (std::is_same<T, std::string>::value) {
      return value;
    } else {
      T number{};
      auto parsed =
          std::from_chars(value.data(), value.data() + value.size(), number);
      market::Require(
          parsed.ec == std::errc() && parsed.ptr == value.data() + value.size(),
          market::ErrorCode::kValidation, "Invalid numeric argument");
      return number;
    }
  }
  bool Decision() {
    auto value = Get<std::string>();
    market::Require(value == "approve" || value == "reject",
                    market::ErrorCode::kValidation, "Use approve or reject");
    return value == "approve";
  }
  void End() {
    std::string extra;
    market::Require(!(input_ >> extra), market::ErrorCode::kValidation,
                    "Too many arguments");
  }

 private:
  std::istringstream input_;
};
std::string Password() {
  std::cout << "Password (not echoed on terminal): " << std::flush;
#ifdef _WIN32
  HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  const bool console = GetConsoleMode(handle, &mode) != 0;
  if (console) SetConsoleMode(handle, mode & ~ENABLE_ECHO_INPUT);
#endif
  std::string password;
  std::getline(std::cin, password);
#ifdef _WIN32
  if (console) {
    SetConsoleMode(handle, mode);
    std::cout << '\n';
  }
#endif
  return password;
}
void Help() {
  std::cout << R"(二手交易平台 / MVP
Commands (quote text containing spaces; amounts are integer cents):
  register PHONE NICKNAME           (then enter password)
  login PHONE                      (then enter password)
  logout | me | users
  apply-seller
  review-seller USER_ID approve|reject
  freeze USER_ID | unfreeze USER_ID
  draft TITLE CATEGORY CENTS DESCRIPTION
  edit PRODUCT_ID VERSION TITLE CATEGORY CENTS DESCRIPTION
  submit PRODUCT_ID
  review-product PRODUCT_ID approve|reject
  down PRODUCT_ID | delete PRODUCT_ID
  search KEYWORD CATEGORY MIN_CENTS MAX_CENTS  (use "" for no filter)
  products
  favorites | cart
  favorite PRODUCT_ID | unfavorite PRODUCT_ID
  add-cart PRODUCT_ID | remove-cart PRODUCT_ID | clear-cart
  reserve PRODUCT_ID VERSION REQUEST_KEY
  reservations
  confirm RESERVATION_ID VERSION
  cancel RESERVATION_ID VERSION
  order RESERVATION_ID ADDRESS
  pay ORDER_ID REQUEST_KEY          (SIMULATED: no real money)
  cancel-order ORDER_ID
  ship ORDER_ID TRACKING_NUMBER
  receive ORDER_ID
  refund ORDER_ID
  review-refund ORDER_ID approve|reject (SIMULATED)
  orders | stats | audits | help | quit
)";
}
void PrintProducts(const std::vector<market::Product>& products) {
  const char* names[] = {"draft",    "pending", "published",
                         "rejected", "offline", "deleted"};
  for (const auto& p : products) {
    std::cout << p.id << " | " << p.title << " | " << p.category << " | "
              << p.price << " cents | " << names[static_cast<int>(p.status)]
              << " | version=" << p.version
              << " | occupied=" << (p.hold_id != 0) << '\n';
  }
  if (products.empty()) std::cout << "No products.\n";
}
bool Command(const std::string& line, market::Marketplace* app,
             std::string* session) {
  Args args(line);
  const auto cmd = args.Get<std::string>();
  using market::Id;
  using market::Money;
  if (cmd == "quit") {
    args.End();
    return false;
  }
  if (cmd == "help") {
    args.End();
    Help();
  } else if (cmd == "register") {
    const auto phone = args.Get<std::string>();
    const auto nickname = args.Get<std::string>();
    args.End();
    std::cout << "User ID: " << app->Register(phone, Password(), nickname)
              << '\n';
  } else if (cmd == "login") {
    auto phone = args.Get<std::string>();
    args.End();
    const auto token = app->Login(phone, Password());
    if (!session->empty()) app->Logout(*session);
    *session = token;
    std::cout << "Logged in.\n";
  } else if (cmd == "logout") {
    args.End();
    app->Logout(*session);
    session->clear();
  } else if (cmd == "me" || cmd == "users") {
    args.End();
    const auto users = cmd == "me"
                           ? std::vector<market::UserView>{app->Me(*session)}
                           : app->Users(*session);
    for (const auto& u : users) {
      std::cout << u.id << " | " << u.nickname << " | admin=" << u.admin
                << " | frozen=" << (u.status == market::AccountStatus::kFrozen)
                << " | seller=" << static_cast<int>(u.seller) << '\n';
    }
  } else if (cmd == "favorites" || cmd == "cart") {
    args.End();
    PrintProducts(cmd == "favorites" ? app->Favorites(*session)
                                     : app->Cart(*session));
  } else if (cmd == "favorite" || cmd == "unfavorite" || cmd == "add-cart" ||
             cmd == "remove-cart") {
    const auto id = args.Get<Id>();
    args.End();
    if (cmd == "favorite") app->AddFavorite(*session, id);
    if (cmd == "unfavorite") app->RemoveFavorite(*session, id);
    if (cmd == "add-cart") app->AddToCart(*session, id);
    if (cmd == "remove-cart") app->RemoveFromCart(*session, id);
  } else if (cmd == "clear-cart") {
    args.End();
    app->ClearCart(*session);
  } else if (cmd == "apply-seller") {
    args.End();
    app->ApplySeller(*session);
  } else if (cmd == "review-seller" || cmd == "review-product" ||
             cmd == "review-refund") {
    auto id = args.Get<Id>();
    bool decision = args.Decision();
    args.End();
    if (cmd == "review-seller") app->ReviewSeller(*session, id, decision);
    if (cmd == "review-product") app->ReviewProduct(*session, id, decision);
    if (cmd == "review-refund") app->ReviewRefund(*session, id, decision);
  } else if (cmd == "draft" || cmd == "edit") {
    Id id = 0;
    std::uint64_t version = 0;
    if (cmd == "edit") {
      id = args.Get<Id>();
      version = args.Get<Id>();
    }
    const auto title = args.Get<std::string>();
    const auto category = args.Get<std::string>();
    auto price = args.Get<Money>();
    const auto description = args.Get<std::string>();
    args.End();
    if (cmd == "draft") {
      std::cout << "Product ID: "
                << app->PublishDraft(*session, title, category, price,
                                     description)
                << '\n';
    } else {
      app->EditProduct(*session, id, version, title, category, price,
                       description);
    }
  } else if (cmd == "search") {
    market::Search search;
    search.keyword = args.Get<std::string>();
    search.category = args.Get<std::string>();
    search.min_price = args.Get<Money>();
    search.max_price = args.Get<Money>();
    args.End();
    PrintProducts(app->SearchProducts(search));
  } else if (cmd == "products") {
    args.End();
    PrintProducts(app->MyProducts(*session));
  } else if (cmd == "reserve") {
    auto id = args.Get<Id>();
    auto version = args.Get<Id>();
    const auto key = args.Get<std::string>();
    args.End();
    std::cout << "Reservation ID: " << app->Reserve(*session, id, version, key)
              << '\n';
  } else if (cmd == "confirm" || cmd == "cancel") {
    auto id = args.Get<Id>();
    auto version = args.Get<Id>();
    args.End();
    if (cmd == "confirm")
      app->Confirm(*session, id, version);
    else
      app->Cancel(*session, id, version);
  } else if (cmd == "order") {
    auto id = args.Get<Id>();
    auto address = args.Get<std::string>();
    args.End();
    std::cout << "Order ID: " << app->CreateOrder(*session, id, address)
              << '\n';
  } else if (cmd == "pay" || cmd == "ship") {
    auto id = args.Get<Id>();
    auto value = args.Get<std::string>();
    args.End();
    if (cmd == "pay")
      app->Pay(*session, id, value);
    else
      app->Ship(*session, id, value);
  } else if (cmd == "reservations") {
    args.End();
    const char* names[] = {"pending", "confirmed", "cancelled", "completed"};
    for (const auto& r : app->Reservations(*session)) {
      std::cout << r.id << " | product=" << r.product_id
                << " | buyer=" << r.buyer_id << " | "
                << names[static_cast<int>(r.status)]
                << " | version=" << r.version << '\n';
    }
  } else if (cmd == "orders") {
    args.End();
    const char* names[] = {"unpaid",    "paid",   "shipped",
                           "completed", "closed", "refund_requested",
                           "refunded"};
    for (const auto& o : app->Orders(*session)) {
      std::cout << o.id << " | " << o.title_snapshot << " | " << o.amount
                << " cents | " << names[static_cast<int>(o.status)]
                << " | settlement_held=" << o.settlement_held << '\n';
    }
  } else if (cmd == "stats") {
    args.End();
    const auto stats = app->Stats(*session);
    std::cout << "Products: " << stats.products
              << " | completed: " << stats.completed_orders
              << " | cents: " << stats.completed_amount << '\n';
  } else if (cmd == "audits") {
    args.End();
    for (const auto& a : app->Audits(*session)) {
      std::cout << a.id << " | " << a.timestamp << " | actor=" << a.actor_id
                << " | " << a.action << " | " << a.detail << '\n';
    }
  } else {
    const auto id = args.Get<Id>();
    args.End();
    if (cmd == "submit")
      app->SubmitProduct(*session, id);
    else if (cmd == "down")
      app->TakeDown(*session, id);
    else if (cmd == "delete")
      app->DeleteProduct(*session, id);
    else if (cmd == "freeze")
      app->Freeze(*session, id, true);
    else if (cmd == "unfreeze")
      app->Freeze(*session, id, false);
    else if (cmd == "cancel-order")
      app->CancelOrder(*session, id);
    else if (cmd == "receive")
      app->Receive(*session, id);
    else if (cmd == "refund")
      app->RequestRefund(*session, id);
    else
      throw market::Error(market::ErrorCode::kValidation, "Unknown command");
  }
  std::cout << "OK\n";
  return true;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
#ifdef _WIN32
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
#endif
  try {
    const auto argument = [&](int index) {
#ifdef _WIN32
      const int size = WideCharToMultiByte(CP_UTF8, 0, argv[index], -1, nullptr,
                                           0, nullptr, nullptr);
      market::Require(size > 0, market::ErrorCode::kValidation,
                      "Cannot decode command line");
      std::string value(size, '\0');
      WideCharToMultiByte(CP_UTF8, 0, argv[index], -1, &value[0], size, nullptr,
                          nullptr);
      value.resize(size - 1);
      return value;
#else
      return std::string(argv[index]);
#endif
    };
    std::string data = "data/market.db", admin_phone;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argument(i);
      market::Require(
          i + 1 < argc && (arg == "--data" || arg == "--init-admin"),
          market::ErrorCode::kValidation,
          "Usage: market_cli [--data PATH] [--init-admin PHONE]");
      if (arg == "--data")
        data = argument(++i);
      else
        admin_phone = argument(++i);
    }
    market::FileRepository repository(data);
    market::Pbkdf2Hasher hasher;
    market::Marketplace app(repository, hasher);
    if (!admin_phone.empty()) {
      const auto id = app.BootstrapAdmin(admin_phone, Password());
      std::cout << "Administrator created: " << id << '\n';
    }
    std::string session, line;
    Help();
    while (std::cout << "market> " && std::getline(std::cin, line)) {
      if (line.empty()) continue;
      try {
        if (!Command(line, &app, &session)) break;
      } catch (const market::Error& error) {
        std::cout << "ERROR " << static_cast<int>(error.code()) << ": "
                  << error.what() << '\n';
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Startup/storage failure: " << error.what() << '\n';
    return 1;
  }
}
