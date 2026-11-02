#include "market/gui_support.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace market {
namespace desktop {
namespace {
constexpr wchar_t kFormClass[] = L"MarketFormWindow";
struct Form {
  HWND window = nullptr;
  HWND error_label = nullptr;
  std::vector<HWND> edits;
  SubmitForm submit;
  bool done = false;
  bool accepted = false;
};
LRESULT CALLBACK FormProc(HWND window, UINT message, WPARAM wparam,
                          LPARAM lparam) {
  auto* form =
      reinterpret_cast<Form*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    form = static_cast<Form*>(
        reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(form));
  }
  if (form && message == WM_COMMAND && LOWORD(wparam) == IDOK) {
    try {
      std::vector<std::wstring> values;
      for (HWND edit : form->edits) values.push_back(WindowText(edit));
      form->submit(values);
      for (HWND edit : form->edits) {
        if (GetWindowLongPtrW(edit, GWL_STYLE) & ES_PASSWORD)
          SetWindowTextW(edit, L"");
      }
      form->accepted = true;
      form->done = true;
    } catch (const std::exception& error) {
      SetWindowTextW(form->error_label, ErrorText(error).c_str());
      MessageBeep(MB_ICONWARNING);
    }
    return 0;
  }
  if (form && (message == WM_CLOSE ||
               (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL))) {
    form->done = true;
    return 0;
  }
  if (message == WM_CTLCOLORSTATIC) {
    auto dc = reinterpret_cast<HDC>(wparam);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, reinterpret_cast<HWND>(lparam) == form->error_label
                         ? RGB(175, 44, 44)
                         : RGB(42, 48, 57));
    return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace

std::wstring Wide(const std::string& value) {
  if (value.empty()) return {};
  const int count =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  Require(count > 0, ErrorCode::kValidation, "Invalid UTF-8 text");
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      &result[0], count);
  return result;
}
std::string Utf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int count = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  Require(count > 0, ErrorCode::kValidation, "Invalid UTF-8 text");
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      &result[0], count, nullptr, nullptr);
  return result;
}
std::wstring WindowText(HWND window) {
  std::wstring result(GetWindowTextLengthW(window) + 1, L'\0');
  const int length =
      GetWindowTextW(window, &result[0], static_cast<int>(result.size()));
  result.resize(length);
  return result;
}
std::wstring Amount(Money value) {
  std::wostringstream out;
  out << value / 100 << L'.' << std::setw(2) << std::setfill(L'0')
      << value % 100;
  return out.str();
}
Money ParseAmount(const std::wstring& value) {
  Require(!value.empty() && value.size() <= 12, ErrorCode::kValidation,
          "Invalid price");
  Money whole = 0, fraction = 0;
  int decimals = -1;
  bool digit = false;
  for (wchar_t c : value) {
    if (c == L'.' && decimals == -1 && digit) {
      decimals = 0;
      continue;
    }
    Require(c >= L'0' && c <= L'9', ErrorCode::kValidation, "Invalid price");
    digit = true;
    if (decimals == -1) {
      whole = whole * 10 + c - L'0';
      Require(whole <= 1000000, ErrorCode::kValidation, "Invalid price");
    } else {
      Require(decimals < 2, ErrorCode::kValidation, "Invalid price");
      fraction = fraction * 10 + c - L'0';
      ++decimals;
    }
  }
  if (decimals == 1) fraction *= 10;
  const Money result = whole * 100 + fraction;
  Require(result <= 100000000, ErrorCode::kValidation, "Invalid price");
  return result;
}
std::wstring ErrorText(const std::exception& error) {
  static const std::map<std::string, std::wstring> messages = {
      {"Invalid credentials", L"手机号或密码不正确，请重新输入。"},
      {"Phone must contain 11 digits", L"请输入 11 位数字手机号。"},
      {"Phone is already registered", L"该手机号已注册，请直接登录。"},
      {"Password needs 8-20 chars, letters and digits",
       L"密码需为 8～20 位，并同时包含字母和数字。"},
      {"MVP passwords use visible ASCII characters",
       L"密码请使用英文字母、数字或可见英文符号，不含空格。"},
      {"Passwords differ", L"两次输入的密码不一致。"},
      {"Text is empty, too long or contains controls",
       L"请填写完整内容，并检查长度；不要使用换行或制表符。"},
      {"Invalid price", L"金额请输入 0～1000000 元，最多两位小数。"},
      {"Invalid price range", L"最低价不能大于最高价。"},
      {"Price must be between 1 and 100000000 cents",
       L"商品价格需在 0.01～1000000 元之间。"},
      {"Please log in", L"请先登录后再进行此操作。"},
      {"Seller approval required", L"请先申请卖家资格，并等待管理员审核。"},
      {"Frozen account: new transactions are disabled",
       L"账号已冻结，暂不能发起新交易。历史订单仍可查看。"},
      {"State has changed; reload and retry",
       L"数据已更新，请刷新列表后重试。"},
      {"Product is unavailable", L"商品已被预约、下架或售出，请刷新查看。"},
      {"Cannot buy yourself", L"不能预约自己发布的商品。"},
      {"Select a record", L"请先在列表中选择一条记录。"},
      {"Bootstrap requires an empty database",
       L"当前数据库已有账号，不能再次创建初始管理员。"},
      {"Database is already open, or directory is not writable",
       L"数据正在被另一个程序使用，或目录不可写。请关闭命令行/"
       L"其他窗口后重试。"}};
  const auto it = messages.find(error.what());
  if (it != messages.end()) return it->second;
  const auto* business = dynamic_cast<const Error*>(&error);
  if (!business) return L"程序遇到异常，请关闭窗口后重新启动。";
  switch (business->code()) {
    case ErrorCode::kValidation:
      return L"输入不符合要求，请检查内容和长度。";
    case ErrorCode::kUnauthenticated:
      return L"登录状态无效，请重新登录。";
    case ErrorCode::kForbidden:
      return L"当前账号无权执行该操作。";
    case ErrorCode::kNotFound:
      return L"记录不存在，请刷新列表。";
    case ErrorCode::kConflict:
      return L"当前状态不支持此操作，请刷新列表后检查状态。";
    case ErrorCode::kStorage:
      return L"数据保存或读取失败，请检查目录权限和磁盘空间。";
  }
  return L"操作失败，请稍后重试。";
}
std::wstring ProductState(ProductStatus status) {
  const wchar_t* names[] = {L"草稿",   L"待审核", L"已发布",
                            L"已驳回", L"已下架", L"已删除"};
  return names[static_cast<int>(status)];
}
std::wstring ReservationState(ReservationStatus status) {
  const wchar_t* names[] = {L"待确认", L"已确认", L"已取消", L"已完成"};
  return names[static_cast<int>(status)];
}
std::wstring OrderState(OrderStatus status) {
  const wchar_t* names[] = {L"待付款", L"待发货",     L"已发货", L"已完成",
                            L"已关闭", L"退款申请中", L"已退款"};
  return names[static_cast<int>(status)];
}
std::wstring SellerState(SellerStatus status) {
  const wchar_t* names[] = {L"未申请", L"待审核", L"已通过", L"已驳回"};
  return names[static_cast<int>(status)];
}
std::wstring LocalTime(std::int64_t timestamp) {
  const std::time_t time = static_cast<std::time_t>(timestamp);
  const auto* local = std::localtime(&time);
  if (!local) return L"时间不可用";
  std::wostringstream out;
  out << std::put_time(local, L"%Y-%m-%d %H:%M:%S");
  return out.str();
}
int Scale(int value) {
  static const int dpi = [] {
    HDC dc = GetDC(nullptr);
    const int result = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return result;
  }();
  return MulDiv(value, dpi, 96);
}
HFONT MakeFont(int pixels, bool bold) {
  return CreateFontW(-Scale(pixels), 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                     L"Microsoft YaHei UI");
}
HWND Control(HWND parent, const wchar_t* type, const std::wstring& text, int id,
             DWORD style, HFONT font, DWORD extended) {
  HWND result = CreateWindowExW(
      extended, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  Require(result != nullptr, ErrorCode::kStorage, "Cannot create UI control");
  SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return result;
}
void Place(HWND control, int x, int y, int width, int height) {
  MoveWindow(control, Scale(x), Scale(y), Scale(width), Scale(height), TRUE);
}
bool Confirm(HWND parent, const std::wstring& question) {
  return MessageBoxW(parent, question.c_str(), L"确认操作",
                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
bool ShowForm(HWND parent, const std::wstring& title,
              const std::wstring& description, const std::vector<Field>& fields,
              const SubmitForm& submit) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = FormProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kFormClass;
  RegisterClassW(&wc);
  Form form;
  form.submit = submit;
  HFONT font = MakeFont(15);
  const int height = 190 + static_cast<int>(fields.size()) * 78;
  RECT bounds{0, 0, Scale(520), Scale(height)};
  AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU, FALSE,
                     WS_EX_DLGMODALFRAME);
  RECT owner{};
  GetWindowRect(parent, &owner);
  form.window = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kFormClass, title.c_str(), WS_CAPTION | WS_SYSMENU,
      owner.left + (owner.right - owner.left - bounds.right + bounds.left) / 2,
      owner.top + (owner.bottom - owner.top - bounds.bottom + bounds.top) / 2,
      bounds.right - bounds.left, bounds.bottom - bounds.top, parent, nullptr,
      wc.hInstance, &form);
  if (!form.window) {
    DeleteObject(font);
    return false;
  }
  HWND hint = Control(form.window, L"STATIC", description, 10, 0, font);
  Place(hint, 28, 22, 464, 46);
  int y = 78;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    HWND label = Control(form.window, L"STATIC", fields[i].label,
                         20 + static_cast<int>(i), 0, font);
    Place(label, 28, y, 464, 24);
    HWND edit = Control(
        form.window, L"EDIT", fields[i].value, 100 + static_cast<int>(i),
        WS_TABSTOP | ES_AUTOHSCROLL | (fields[i].password ? ES_PASSWORD : 0),
        font, WS_EX_CLIENTEDGE);
    SendMessageW(edit, EM_SETLIMITTEXT, fields[i].limit, 0);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(8, 8));
    Place(edit, 28, y + 27, 464, 34);
    form.edits.push_back(edit);
    y += 78;
  }
  form.error_label = Control(form.window, L"STATIC", L"", 11, 0, font);
  Place(form.error_label, 28, y + 2, 464, 43);
  HWND ok = Control(form.window, L"BUTTON", L"确认", IDOK,
                    WS_TABSTOP | BS_DEFPUSHBUTTON, font);
  HWND cancel = Control(form.window, L"BUTTON", L"取消", IDCANCEL,
                        WS_TABSTOP | BS_PUSHBUTTON, font);
  Place(ok, 272, height - 56, 104, 34);
  Place(cancel, 388, height - 56, 104, 34);
  EnableWindow(parent, FALSE);
  ShowWindow(form.window, SW_SHOW);
  if (!form.edits.empty()) SetFocus(form.edits.front());
  MSG message{};
  while (!form.done) {
    const int result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
      break;
    }
    if (!IsDialogMessageW(form.window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  EnableWindow(parent, TRUE);
  DestroyWindow(form.window);
  DeleteObject(font);
  SetForegroundWindow(parent);
  return form.accepted;
}
}  // namespace desktop
}  // namespace market
