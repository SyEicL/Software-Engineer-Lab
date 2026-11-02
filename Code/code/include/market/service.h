#ifndef MARKET_SERVICE_H_
#define MARKET_SERVICE_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "market/domain.h"
#include "market/repository.h"
#include "market/security.h"

namespace market {
// Application boundary. The CLI serializes all calls; no networking is exposed.
// Callers receive value objects, never references into mutable storage.
class Marketplace {
 public:
  Marketplace(IRepository& repository, IPasswordHasher& hasher);
  Id Register(const std::string& phone, const std::string& password,
              const std::string& nickname);
  Id BootstrapAdmin(const std::string& phone, const std::string& password);
  std::string Login(const std::string& phone, const std::string& password);
  void Logout(const std::string& session);
  UserView Me(const std::string& session) const;
  std::vector<UserView> Users(const std::string& session);
  void ApplySeller(const std::string& session);
  void ReviewSeller(const std::string& session, Id user_id, bool approved);
  void Freeze(const std::string& session, Id user_id, bool frozen);
  Id PublishDraft(const std::string& session, const std::string& title,
                  const std::string& category, Money price,
                  const std::string& description);
  void EditProduct(const std::string& session, Id product_id,
                   std::uint64_t version, const std::string& title,
                   const std::string& category, Money price,
                   const std::string& description);
  void SubmitProduct(const std::string& session, Id product_id);
  void ReviewProduct(const std::string& session, Id product_id, bool approved);
  void TakeDown(const std::string& session, Id product_id);
  void DeleteProduct(const std::string& session, Id product_id);
  std::vector<Product> SearchProducts(const Search& search) const;
  std::vector<Product> MyProducts(const std::string& session) const;
  void AddFavorite(const std::string& session, Id product_id);
  void RemoveFavorite(const std::string& session, Id product_id);
  std::vector<Product> Favorites(const std::string& session) const;
  void AddToCart(const std::string& session, Id product_id);
  void RemoveFromCart(const std::string& session, Id product_id);
  void ClearCart(const std::string& session);
  std::vector<Product> Cart(const std::string& session) const;
  Id Reserve(const std::string& session, Id product_id, std::uint64_t version,
             const std::string& request_key);
  void Confirm(const std::string& session, Id reservation_id,
               std::uint64_t version);
  void Cancel(const std::string& session, Id reservation_id,
              std::uint64_t version);
  std::vector<Reservation> Reservations(const std::string& session) const;
  Id CreateOrder(const std::string& session, Id reservation_id,
                 const std::string& address);
  void Pay(const std::string& session, Id order_id,
           const std::string& request_key);
  void CancelOrder(const std::string& session, Id order_id);
  void Ship(const std::string& session, Id order_id,
            const std::string& tracking_number);
  void Receive(const std::string& session, Id order_id);
  void RequestRefund(const std::string& session, Id order_id);
  void ReviewRefund(const std::string& session, Id order_id, bool approved);
  std::vector<Order> Orders(const std::string& session) const;
  Statistics Stats(const std::string& session);
  std::vector<Audit> Audits(const std::string& session);

 private:
  using Mutation = std::function<void(State&, const User&)>;
  void Update(const std::string& session, const std::string& action,
              const Mutation& mutation);
  void Persist(State candidate);
  void AddSaved(const std::string& session, Id product_id, bool cart);
  void RemoveSaved(const std::string& session, Id product_id, bool cart);
  std::vector<Product> Saved(const std::string& session, bool cart) const;
  const User& Actor(const std::string& session) const;
  static UserView View(const User& user);
  static void Normal(const User& user);
  static void Seller(const User& user);
  static void Admin(const User& user);
  static void Log(State* state, Id actor_id, const std::string& action,
                  const std::string& detail);
  IRepository& repository_;
  IPasswordHasher& hasher_;
  State state_;
  std::map<std::string, Id> sessions_;
};
}  // namespace market
#endif  // MARKET_SERVICE_H_
