// Shell declarations depend on Windows types.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <string>
#include <vector>

#include "market/gui.h"
#include "market/gui_support.h"
#include "market/repository.h"
#include "market/security.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();
  try {
    std::vector<wchar_t> executable(32768);
    const DWORD size = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    market::Require(size > 0 && size < executable.size(),
                    market::ErrorCode::kStorage, "Cannot locate executable");
    std::wstring base(executable.data(), size);
    base.resize(base.find_last_of(L"/\\"));
    std::wstring data = base + L"/../data/market.db";
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    market::Require(raw != nullptr, market::ErrorCode::kValidation,
                    "Cannot decode command line");
    std::vector<std::wstring> args;
    for (int i = 1; i < count; ++i) args.emplace_back(raw[i]);
    LocalFree(raw);
    market::Require(args.empty() || (args.size() == 2 && args[0] == L"--data"),
                    market::ErrorCode::kValidation,
                    "Usage: market_gui [--data PATH]");
    if (!args.empty()) data = args[1];
    market::FileRepository repository(market::desktop::Utf8(data));
    const bool needs_setup = repository.Load().users.empty();
    market::Pbkdf2Hasher hasher;
    market::Marketplace app(repository, hasher);
    return market::RunDesktop(&app, needs_setup);
  } catch (const std::exception& error) {
    MessageBoxW(nullptr, market::desktop::ErrorText(error).c_str(),
                L"二手交易平台 · 启动失败", MB_OK | MB_ICONERROR);
    return 1;
  }
}
