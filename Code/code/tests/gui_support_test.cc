#include "market/gui_support.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

int main() {
  try {
    using market::desktop::ParseAmount;
    const std::vector<std::pair<std::wstring, market::Money>> valid = {
        {L"0", 0},       {L"0.01", 1},     {L"28", 2800},
        {L"28.5", 2850}, {L"28.05", 2805}, {L"1000000.00", 100000000}};
    for (const auto& entry : valid) {
      if (ParseAmount(entry.first) != entry.second ||
          ParseAmount(market::desktop::Amount(entry.second)) != entry.second)
        throw std::runtime_error("Incorrect money conversion");
    }
    const std::vector<std::wstring> invalid = {
        L"",      L"-1",         L"+1",           L"2.999", L"1e2", L"1,000",
        L"1.2.3", L"1000000.01", L"999999999999", L" 28",   L"28 ", L".5"};
    for (const auto& entry : invalid) {
      bool rejected = false;
      try {
        ParseAmount(entry);
      } catch (const market::Error& error) {
        rejected = error.code() == market::ErrorCode::kValidation;
      }
      if (!rejected) throw std::runtime_error("Invalid price accepted");
    }
    const std::string text = "中文路径 / 商品 C++";
    if (market::desktop::Utf8(market::desktop::Wide(text)) != text)
      throw std::runtime_error("UTF-8 conversion failed");
    std::cout << "PASS: exact-cent prices, invalid input rejection, UTF-8 "
                 "roundtrip\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
