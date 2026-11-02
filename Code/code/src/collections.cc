#include <algorithm>
#include <string>
#include <vector>

#include "market/service.h"

namespace market {
namespace {
std::vector<SavedItem>& Entries(State* state, bool cart) {
  return cart ? state->cart : state->favorites;
}
const Product& ProductById(const State& state, Id id) {
  for (const auto& product : state.products)
    if (product.id == id) return product;
  throw Error(ErrorCode::kNotFound, "Record not found");
}
bool Public(const State& state, const Product& product) {
  if (product.status != ProductStatus::kPublished) return false;
  for (const auto& seller : state.users) {
    if (seller.id == product.seller_id)
      return seller.status == AccountStatus::kNormal &&
             seller.seller == SellerStatus::kApproved;
  }
  return false;
}
}  // namespace

void Marketplace::AddSaved(const std::string& session, Id id, bool cart) {
  Update(session, cart ? "add_cart" : "add_favorite",
         [&](State& state, const User& actor) {
           Normal(actor);
           const auto& product = ProductById(state, id);
           auto& entries = Entries(&state, cart);
           for (const auto& item : entries)
             if (item.user_id == actor.id && item.product_id == id) return;
           Require(Public(state, product), ErrorCode::kConflict,
                   "Product is unavailable");
           if (cart) {
             Require(product.seller_id != actor.id, ErrorCode::kForbidden,
                     "Cannot buy yourself");
             Require(!product.hold_id, ErrorCode::kConflict,
                     "Product is unavailable");
           }
           entries.push_back({actor.id, id, product.title, product.price});
         });
}
void Marketplace::RemoveSaved(const std::string& session, Id id, bool cart) {
  Update(session, cart ? "remove_cart" : "remove_favorite",
         [&](State& state, const User& actor) {
           auto& entries = Entries(&state, cart);
           entries.erase(std::remove_if(entries.begin(), entries.end(),
                                        [&](const SavedItem& item) {
                                          return item.user_id == actor.id &&
                                                 item.product_id == id;
                                        }),
                         entries.end());
         });
}
std::vector<Product> Marketplace::Saved(const std::string& session,
                                        bool cart) const {
  const auto& actor = Actor(session);
  std::vector<Product> result;
  const auto& entries = cart ? state_.cart : state_.favorites;
  for (const auto& item : entries) {
    if (item.user_id != actor.id) continue;
    const auto& current = ProductById(state_, item.product_id);
    Product visible = current;
    if (!Public(state_, current)) {
      // Keep previously public data; never reveal a seller's later draft edits.
      visible.title = item.title_snapshot;
      visible.price = item.price_snapshot;
      visible.category.clear();
      visible.description = "该商品当前不可查看，请移除或等待重新发布。";
      visible.status = current.status == ProductStatus::kDeleted
                           ? ProductStatus::kDeleted
                           : ProductStatus::kOffline;
      visible.version = 0;
    }
    result.push_back(visible);
  }
  return result;
}
void Marketplace::AddFavorite(const std::string& session, Id id) {
  AddSaved(session, id, false);
}
void Marketplace::RemoveFavorite(const std::string& session, Id id) {
  RemoveSaved(session, id, false);
}
std::vector<Product> Marketplace::Favorites(const std::string& session) const {
  return Saved(session, false);
}
void Marketplace::AddToCart(const std::string& session, Id id) {
  AddSaved(session, id, true);
}
void Marketplace::RemoveFromCart(const std::string& session, Id id) {
  RemoveSaved(session, id, true);
}
void Marketplace::ClearCart(const std::string& session) {
  Update(session, "clear_cart", [](State& state, const User& actor) {
    auto& entries = state.cart;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const SavedItem& item) {
                                   return item.user_id == actor.id;
                                 }),
                  entries.end());
  });
}
std::vector<Product> Marketplace::Cart(const std::string& session) const {
  return Saved(session, true);
}
}  // namespace market
