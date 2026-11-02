#ifndef MARKET_GUI_SUPPORT_H_
#define MARKET_GUI_SUPPORT_H_

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "market/domain.h"

namespace market {
namespace desktop {
std::wstring Wide(const std::string& value);
std::string Utf8(const std::wstring& value);
std::wstring WindowText(HWND window);
std::wstring Amount(Money value);
Money ParseAmount(const std::wstring& value);
std::wstring ErrorText(const std::exception& error);
std::wstring ProductState(ProductStatus status);
std::wstring ReservationState(ReservationStatus status);
std::wstring OrderState(OrderStatus status);
std::wstring SellerState(SellerStatus status);
std::wstring LocalTime(std::int64_t timestamp);
int Scale(int value);
HFONT MakeFont(int pixels, bool bold = false);
HWND Control(HWND parent, const wchar_t* type, const std::wstring& text, int id,
             DWORD style, HFONT font, DWORD extended = 0);
void Place(HWND control, int x, int y, int width, int height);

struct Field {
  std::wstring label;
  std::wstring value;
  bool password = false;
  int limit = 200;
};
using SubmitForm = std::function<void(const std::vector<std::wstring>&)>;
// The form remains open on a validation/service error. Cancel does not submit.
bool ShowForm(HWND parent, const std::wstring& title,
              const std::wstring& description, const std::vector<Field>& fields,
              const SubmitForm& submit);
bool Confirm(HWND parent, const std::wstring& question);
}  // namespace desktop
}  // namespace market

#endif  // MARKET_GUI_SUPPORT_H_
