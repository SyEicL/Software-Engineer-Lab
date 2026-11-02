#include "market/gui.h"

// Windows types must be declared before common-control declarations.
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "market/gui_support.h"

namespace market {
namespace desktop {
namespace {
constexpr COLORREF kBackground = RGB(245, 247, 249);
constexpr COLORREF kText = RGB(31, 39, 49);
constexpr COLORREF kMuted = RGB(108, 119, 131);
constexpr COLORREF kAccent = RGB(28, 105, 84);
constexpr wchar_t kWindowClass[] = L"SecondHandMarketWindow";
enum Command {
  kLogin = 12,
  kRegister,
  kLogout,
  kBrowse = 100,
  kReservations,
  kOrders,
  kProducts,
  kUsers,
  kStats,
  kAudits,
  kProfile,
  kFavorites,
  kCart,
  kRefresh = 200,
  kSearch,
  kReset,
  kDetails,
  kReserve,
  kDraft = 210,
  kEdit,
  kSubmit,
  kDown,
  kDelete,
  kApproveProduct,
  kRejectProduct,
  kConfirm = 220,
  kCancel,
  kCreateOrder,
  kPay = 230,
  kCancelOrder,
  kShip,
  kReceive,
  kRefund,
  kApproveRefund,
  kRejectRefund,
  kApproveSeller = 240,
  kRejectSeller,
  kFreeze,
  kUnfreeze,
  kApplySeller = 250,
  kFavorite = 260,
  kUnfavorite,
  kAddCart,
  kRemoveCart,
  kClearCart,
  kKeyword = 300,
  kCategory,
  kMinimum,
  kMaximum,
  kTable = 400,
  kEmpty
};
constexpr int kPages[] = {
    kBrowse,   kFavorites, kCart,  kReservations, kOrders,
    kProducts, kUsers,     kStats, kAudits,       kProfile,
};
bool ProductPage(int page) {
  return page == kBrowse || page == kProducts || page == kFavorites ||
         page == kCart;
}
const wchar_t* PageName(int page) {
  const wchar_t* names[] = {L"商品市场", L"我的预约", L"我的订单", L"商品管理",
                            L"用户管理", L"统计汇总", L"操作日志", L"个人中心",
                            L"收藏夹",   L"购物车"};
  return names[page - kBrowse];
}
const wchar_t* PageDescription(int page) {
  const wchar_t* descriptions[] = {
      L"查找需要的二手好物，选择商品后查看详情或发起预约。",
      L"查看预约进度；卖家确认后，买家即可创建订单。",
      L"跟进交易进度。付款与退款为模拟操作，不涉及真实资金。",
      L"管理商品草稿与发布状态；提交审核后由管理员处理。",
      L"审核卖家申请，管理账号状态；冻结后保留历史订单。",
      L"查看商品与交易汇总，金额以确认收货的订单为准。",
      L"查看关键操作与安全记录，时间按本机时区显示。",
      L"查看账号状态与卖家资格。",
      L"保存感兴趣的商品，可加入购物车或直接预约。收藏不占用商品。",
      L"单件商品不设数量。预约成功后移出购物车，卖家确认后再下单。"};
  return descriptions[page - kBrowse];
}
template <typename T>
const T& Record(const std::vector<T>& values, Id id) {
  for (const auto& value : values)
    if (value.id == id) return value;
  throw Error(ErrorCode::kNotFound, "Record not found");
}
std::wstring AuditAction(const std::string& action) {
  static const std::map<std::string, std::wstring> names = {
      {"register", L"用户注册"},
      {"bootstrap", L"初始化管理员"},
      {"list_users", L"查看用户"},
      {"apply_seller", L"申请卖家资格"},
      {"review_seller", L"审核卖家资格"},
      {"freeze_account", L"调整账号状态"},
      {"create_product", L"创建商品"},
      {"edit_product", L"修改商品"},
      {"submit_product", L"提交商品审核"},
      {"review_product", L"审核商品"},
      {"take_down", L"商品下架"},
      {"delete_product", L"删除商品"},
      {"reserve", L"预约商品"},
      {"confirm_reservation", L"确认预约"},
      {"cancel_reservation", L"取消预约"},
      {"create_order", L"创建订单"},
      {"simulate_payment", L"模拟付款"},
      {"cancel_order", L"取消订单"},
      {"ship", L"发货"},
      {"receive", L"确认收货"},
      {"request_refund", L"申请退款"},
      {"simulate_refund", L"处理退款"},
      {"statistics", L"查看统计"},
      {"read_audits", L"查询日志"},
      {"access_denied", L"权限拒绝"},
      {"security_alert", L"安全告警"},
      {"buyer_notice", L"买家通知"},
      {"add_favorite", L"收藏商品"},
      {"remove_favorite", L"取消收藏"},
      {"add_cart", L"加入购物车"},
      {"remove_cart", L"移出购物车"},
      {"clear_cart", L"清空购物车"}};
  const auto it = names.find(action);
  return it == names.end() ? Wide(action) : it->second;
}

class Desktop {
 public:
  Desktop(Marketplace* app, bool setup) : app_(app), needs_setup_(setup) {}
  ~Desktop() {
    if (row_images_) ImageList_Destroy(row_images_);
    DeleteObject(font_);
    DeleteObject(title_font_);
    DeleteObject(brand_font_);
    DeleteObject(background_);
  }
  int Run();

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  HWND Button(int id, const std::wstring& text, bool page = false);
  void Create();
  void Navigate(int page);
  void BuildPage();
  void Layout();
  void Refresh();
  void UpdateButtons();
  bool Allowed(int command) const;
  void Action(int command);
  void Auth(bool registration);
  void Setup();
  bool ProductForm(bool edit);
  void ShowDetails();
  void MakeTable(const std::vector<std::pair<std::wstring, int>>& columns);
  void AddRow(Id id, const std::vector<std::wstring>& values);
  Id Selection() const;
  void SetStatus(const std::wstring& text, bool error = false);
  void Paint(HDC supplied = nullptr);
  void DrawButton(DRAWITEMSTRUCT* item);
  void DrawTextAt(HDC dc, const std::wstring& text, int x, int y, int width,
                  int height, HFONT font, COLORREF color);
  void Card(HDC dc, int x, int y, int width, int height);
  Marketplace* app_;
  bool needs_setup_;
  HWND window_ = nullptr;
  HWND table_ = nullptr;
  HWND empty_ = nullptr;
  HWND login_ = nullptr;
  HWND registration_ = nullptr;
  HWND logout_ = nullptr;
  HFONT font_ = nullptr;
  HFONT title_font_ = nullptr;
  HFONT brand_font_ = nullptr;
  HBRUSH background_ = nullptr;
  HIMAGELIST row_images_ = nullptr;
  int page_ = kBrowse;
  int width_ = 1240;
  int height_ = 800;
  std::map<int, HWND> navigation_;
  std::vector<HWND> controls_;
  std::vector<std::pair<int, HWND>> actions_;
  std::vector<HWND> filters_;
  std::vector<Id> rows_;
  std::string session_;
  UserView user_{};
  Search search_;
  Statistics statistics_;
  std::vector<Product> products_;
  std::vector<Reservation> reservations_;
  std::vector<Order> orders_;
  std::vector<UserView> users_;
  std::wstring status_ = L"欢迎使用二手交易平台。";
  bool status_error_ = false;
};

int Desktop::Run() {
  INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&common);
  font_ = MakeFont(15);
  title_font_ = MakeFont(28, true);
  brand_font_ = MakeFont(20, true);
  background_ = CreateSolidBrush(kBackground);
  row_images_ = ImageList_Create(1, Scale(38), ILC_COLOR32, 1, 0);
  if (row_images_) ImageList_SetImageCount(row_images_, 1);
  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.lpszClassName = kWindowClass;
  RegisterClassW(&wc);
  window_ = CreateWindowExW(0, kWindowClass, L"二手交易平台",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, Scale(1240),
                            Scale(840), nullptr, nullptr, wc.hInstance, this);
  Require(window_ != nullptr, ErrorCode::kStorage, "Cannot create UI control");
  ShowWindow(window_, SW_SHOWNORMAL);
  UpdateWindow(window_);
  if (needs_setup_) PostMessageW(window_, WM_APP + 1, 0, 0);
  MSG message{};
  int result = 0;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    if (!IsDialogMessageW(window_, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return result < 0 ? 1 : static_cast<int>(message.wParam);
}
LRESULT CALLBACK Desktop::WindowProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam) {
  auto* app =
      reinterpret_cast<Desktop*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    app = static_cast<Desktop*>(
        reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    app->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  if (!app) return DefWindowProcW(window, message, wparam, lparam);
  try {
    return app->HandleMessage(message, wparam, lparam);
  } catch (const std::exception& error) {
    app->SetStatus(ErrorText(error), true);
    if (message == WM_CREATE) return -1;
    if (message == WM_COMMAND) {
      const auto* business = dynamic_cast<const Error*>(&error);
      if (business && business->code() == ErrorCode::kConflict) {
        try {
          app->Refresh();
        } catch (const std::exception&) {
        }
      }
      MessageBoxW(window, ErrorText(error).c_str(), L"操作未完成",
                  MB_OK | MB_ICONWARNING);
    }
    return 0;
  }
}
LRESULT Desktop::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      Create();
      return 0;
    case WM_APP + 1:
      Setup();
      return 0;
    case WM_SIZE:
      width_ = MulDiv(LOWORD(lparam), 96, Scale(96));
      height_ = MulDiv(HIWORD(lparam), 96, Scale(96));
      Layout();
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize = {Scale(1080), Scale(740)};
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(wparam) == BN_CLICKED) Action(LOWORD(wparam));
      return 0;
    case WM_NOTIFY: {
      const auto* notice = reinterpret_cast<NMHDR*>(lparam);
      if (notice->idFrom == kTable) {
        if (notice->code == LVN_ITEMCHANGED) UpdateButtons();
        if (notice->code == NM_DBLCLK && Selection() &&
            (ProductPage(page_) || page_ == kOrders))
          ShowDetails();
      }
      return 0;
    }
    case WM_DRAWITEM:
      DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
      return TRUE;
    case WM_CTLCOLORSTATIC: {
      auto dc = reinterpret_cast<HDC>(wparam);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, kMuted);
      return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lparam) == empty_
                                           ? GetStockObject(WHITE_BRUSH)
                                           : background_);
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      Paint();
      return 0;
    case WM_PRINTCLIENT:
      Paint(reinterpret_cast<HDC>(wparam));
      return 0;
    case WM_DESTROY:
      if (!session_.empty()) app_->Logout(session_);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window_, message, wparam, lparam);
  }
}
HWND Desktop::Button(int id, const std::wstring& text, bool page) {
  HWND button =
      Control(window_, L"BUTTON", text, id, WS_TABSTOP | BS_OWNERDRAW, font_);
  if (page) {
    controls_.push_back(button);
    actions_.push_back({id, button});
  }
  return button;
}
void Desktop::Create() {
  for (int page : kPages) navigation_[page] = Button(page, PageName(page));
  login_ = Button(kLogin, L"登录");
  registration_ = Button(kRegister, L"注册账号");
  logout_ = Button(kLogout, L"退出登录");
  BuildPage();
  Refresh();
}
void Desktop::Navigate(int page) {
  if (page != kBrowse && session_.empty()) {
    Auth(false);
    if (session_.empty()) return;
  }
  if ((page == kUsers || page == kAudits) && !user_.admin)
    throw Error(ErrorCode::kForbidden, "Administrator permission required");
  if ((page == kProducts || page == kStats) && !user_.admin &&
      user_.seller != SellerStatus::kApproved)
    throw Error(ErrorCode::kForbidden, "Seller approval required");
  page_ = page;
  BuildPage();
  Refresh();
  SetStatus(L"已更新列表。选择记录后可进行相应操作。");
}
void Desktop::MakeTable(
    const std::vector<std::pair<std::wstring, int>>& columns) {
  table_ = Control(window_, WC_LISTVIEWW, L"", kTable,
                   WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                   font_, WS_EX_CLIENTEDGE);
  controls_.push_back(table_);
  ListView_SetExtendedListViewStyle(table_,
                                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
  ListView_SetBkColor(table_, RGB(255, 255, 255));
  ListView_SetTextColor(table_, kText);
  ListView_SetImageList(table_, row_images_, LVSIL_SMALL);
  int index = 0;
  for (const auto& column : columns) {
    LVCOLUMNW value{};
    value.mask = LVCF_TEXT | LVCF_WIDTH;
    value.pszText = const_cast<wchar_t*>(column.first.c_str());
    value.cx = Scale(column.second);
    SendMessageW(table_, LVM_INSERTCOLUMNW, index++,
                 reinterpret_cast<LPARAM>(&value));
  }
  empty_ = Control(window_, L"STATIC",
                   L"暂无记录。可以刷新列表，或从左侧选择其他功能。", kEmpty,
                   SS_CENTER, font_);
  controls_.push_back(empty_);
}
void Desktop::BuildPage() {
  for (HWND control : controls_) DestroyWindow(control);
  controls_.clear();
  actions_.clear();
  filters_.clear();
  rows_.clear();
  table_ = nullptr;
  empty_ = nullptr;
  if (page_ == kBrowse) {
    const std::vector<std::wstring> labels = {L"关键词", L"分类",
                                              L"最低价 / 元", L"最高价 / 元"};
    const std::vector<std::wstring> values = {
        Wide(search_.keyword), Wide(search_.category),
        search_.min_price ? Amount(search_.min_price) : L"",
        search_.max_price == 100000000 ? L"" : Amount(search_.max_price)};
    for (int i = 0; i < 4; ++i) {
      HWND label = Control(window_, L"STATIC", labels[i], 310 + i, 0, font_);
      HWND edit = Control(window_, L"EDIT", values[i], kKeyword + i,
                          WS_TABSTOP | ES_AUTOHSCROLL, font_, WS_EX_CLIENTEDGE);
      SendMessageW(edit, EM_SETLIMITTEXT, i < 2 ? 120 : 12, 0);
      SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                   MAKELPARAM(7, 7));
      controls_.push_back(label);
      controls_.push_back(edit);
      filters_.push_back(label);
      filters_.push_back(edit);
    }
    Button(kSearch, L"搜索商品", true);
    Button(kReset, L"重置条件", true);
    Button(kDetails, L"查看详情", true);
    Button(kReserve, L"预约商品", true);
    Button(kFavorite, L"收藏商品", true);
    Button(kAddCart, L"加入购物车", true);
    MakeTable({{L"编号", 70},
               {L"商品名称", 260},
               {L"分类", 120},
               {L"价格 / 元", 120},
               {L"交易状态", 130}});
  } else if (page_ == kFavorites || page_ == kCart) {
    Button(kDetails, L"查看详情", true);
    Button(kReserve, page_ == kCart ? L"预约选中商品" : L"预约商品", true);
    if (page_ == kFavorites) {
      Button(kAddCart, L"加入购物车", true);
      Button(kUnfavorite, L"取消收藏", true);
    } else {
      Button(kFavorite, L"收藏商品", true);
      Button(kRemoveCart, L"移出购物车", true);
      Button(kClearCart, L"清空购物车", true);
    }
    Button(kRefresh, L"刷新列表", true);
    MakeTable({{L"编号", 70},
               {L"商品名称", 260},
               {L"分类", 100},
               {L"价格 / 元", 110},
               {L"交易状态", 170}});
  } else if (page_ == kProducts) {
    if (!user_.admin) {
      Button(kDraft, L"新建商品", true);
      Button(kEdit, L"编辑商品", true);
      Button(kSubmit, L"提交审核", true);
      Button(kDelete, L"删除商品", true);
    } else {
      Button(kApproveProduct, L"审核通过", true);
      Button(kRejectProduct, L"驳回审核", true);
    }
    Button(kDown, L"下架商品", true);
    Button(kDetails, L"查看详情", true);
    Button(kRefresh, L"刷新列表", true);
    MakeTable({{L"编号", 70},
               {L"商品名称", 230},
               {L"分类", 100},
               {L"价格 / 元", 100},
               {L"发布状态", 100},
               {L"占用情况", 100},
               {L"卖家编号", 90}});
  } else if (page_ == kReservations) {
    Button(kCreateOrder, L"创建订单", true);
    Button(kConfirm, L"确认预约", true);
    Button(kCancel, L"取消预约", true);
    Button(kRefresh, L"刷新列表", true);
    MakeTable({{L"预约编号", 100},
               {L"商品编号", 120},
               {L"买家编号", 120},
               {L"预约状态", 140},
               {L"版本", 100}});
  } else if (page_ == kOrders) {
    if (!user_.admin) {
      Button(kPay, L"模拟付款", true);
      Button(kCancelOrder, L"取消订单", true);
      Button(kReceive, L"确认收货", true);
      Button(kRefund, L"申请退款", true);
      if (user_.seller == SellerStatus::kApproved)
        Button(kShip, L"填写发货", true);
    }
    if (user_.admin || user_.seller == SellerStatus::kApproved) {
      Button(kApproveRefund, L"同意退款", true);
      Button(kRejectRefund, L"驳回退款", true);
    }
    Button(kDetails, L"订单详情", true);
    Button(kRefresh, L"刷新列表", true);
    MakeTable({{L"订单编号", 90},
               {L"商品名称", 230},
               {L"金额 / 元", 110},
               {L"订单状态", 130},
               {L"结算", 110},
               {L"买家", 80},
               {L"卖家", 80}});
  } else if (page_ == kUsers) {
    Button(kApproveSeller, L"通过卖家申请", true);
    Button(kRejectSeller, L"驳回卖家申请", true);
    Button(kFreeze, L"冻结账号", true);
    Button(kUnfreeze, L"解冻账号", true);
    Button(kRefresh, L"刷新列表", true);
    MakeTable({{L"用户编号", 100},
               {L"昵称", 200},
               {L"身份", 140},
               {L"账号状态", 140},
               {L"卖家资格", 140}});
  } else if (page_ == kAudits) {
    Button(kRefresh, L"刷新记录", true);
    MakeTable({{L"日志编号", 85},
               {L"操作时间", 200},
               {L"用户编号", 90},
               {L"操作", 160},
               {L"结果 / 说明", 310}});
  } else if (page_ == kStats) {
    Button(kRefresh, L"刷新统计", true);
  } else if (page_ == kProfile) {
    Button(kApplySeller, L"申请卖家资格", true);
    Button(kRefresh, L"刷新状态", true);
  }
  Layout();
  InvalidateRect(window_, nullptr, FALSE);
}
void Desktop::Layout() {
  if (!login_) return;
  int y = 112;
  for (const auto& entry : navigation_) {
    Place(entry.second, 16, y, 176, 46);
    y += 58;
  }
  Place(login_, width_ - 216, 20, 80, 36);
  Place(registration_, width_ - 124, 20, 100, 36);
  Place(logout_, width_ - 132, 20, 108, 36);
  const int available = width_ - 272;
  if (!filters_.empty()) {
    const int field_width = (available - 36) / 4;
    for (int i = 0; i < 4; ++i) {
      Place(filters_[i * 2], 240 + i * (field_width + 12), 178, field_width,
            22);
      Place(filters_[i * 2 + 1], 240 + i * (field_width + 12), 206, field_width,
            34);
    }
  }
  int x = 240;
  y = page_ == kBrowse ? 260 : 180;
  if (page_ == kProfile) y = 410;
  if (page_ == kStats) y = 356;
  for (const auto& action : actions_) {
    if (x + 120 > width_ - 32) {
      x = 240;
      y += 48;
    }
    Place(action.second, x, y, 120, 36);
    x += 130;
  }
  if (table_) {
    Place(table_, 240, y + 54, available, (std::max)(120, height_ - y - 118));
    Place(empty_, 260, y + 116, available - 40, 48);
  }
  UpdateButtons();
}
Id Desktop::Selection() const {
  if (!table_) return 0;
  const int index = ListView_GetNextItem(table_, -1, LVNI_SELECTED);
  return index >= 0 && index < static_cast<int>(rows_.size()) ? rows_[index]
                                                              : 0;
}
void Desktop::AddRow(Id id, const std::vector<std::wstring>& values) {
  const int row = static_cast<int>(rows_.size());
  rows_.push_back(id);
  LVITEMW item{};
  item.mask = LVIF_TEXT | LVIF_IMAGE;
  item.iImage = 0;
  item.iItem = row;
  item.pszText = const_cast<wchar_t*>(values.front().c_str());
  SendMessageW(table_, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
  for (int col = 1; col < static_cast<int>(values.size()); ++col) {
    item.iSubItem = col;
    item.pszText = const_cast<wchar_t*>(values[col].c_str());
    SendMessageW(table_, LVM_SETITEMTEXTW, row,
                 reinterpret_cast<LPARAM>(&item));
  }
}
void Desktop::Refresh() {
  if (!session_.empty()) user_ = app_->Me(session_);
  const Id selected = Selection();
  if (table_) {
    ListView_DeleteAllItems(table_);
    rows_.clear();
  }
  if (ProductPage(page_)) {
    if (page_ == kBrowse) products_ = app_->SearchProducts(search_);
    if (page_ == kProducts) products_ = app_->MyProducts(session_);
    if (page_ == kFavorites) products_ = app_->Favorites(session_);
    if (page_ == kCart) products_ = app_->Cart(session_);
    for (const auto& p : products_) {
      if (page_ != kProducts)
        AddRow(p.id,
               {std::to_wstring(p.id), Wide(p.title), Wide(p.category),
                Amount(p.price),
                p.status != ProductStatus::kPublished ? L"已失效（历史信息）"
                : p.hold_id                           ? L"已被占用"
                                                      : L"可预约"});
      else
        AddRow(p.id, {std::to_wstring(p.id), Wide(p.title), Wide(p.category),
                      Amount(p.price), ProductState(p.status),
                      p.hold_id ? L"已占用" : L"未占用",
                      std::to_wstring(p.seller_id)});
    }
  } else if (page_ == kReservations) {
    reservations_ = app_->Reservations(session_);
    orders_ = app_->Orders(session_);
    products_ = user_.admin || user_.seller == SellerStatus::kApproved
                    ? app_->MyProducts(session_)
                    : std::vector<Product>{};
    for (const auto& r : reservations_)
      AddRow(r.id, {std::to_wstring(r.id), std::to_wstring(r.product_id),
                    std::to_wstring(r.buyer_id), ReservationState(r.status),
                    std::to_wstring(r.version)});
  } else if (page_ == kOrders) {
    orders_ = app_->Orders(session_);
    for (const auto& o : orders_)
      AddRow(o.id, {std::to_wstring(o.id), Wide(o.title_snapshot),
                    Amount(o.amount), OrderState(o.status),
                    o.settlement_held ? L"暂缓结算" : L"无暂缓标记",
                    std::to_wstring(o.buyer_id), std::to_wstring(o.seller_id)});
  } else if (page_ == kUsers) {
    users_ = app_->Users(session_);
    for (const auto& u : users_)
      AddRow(u.id, {std::to_wstring(u.id), Wide(u.nickname),
                    u.admin                               ? L"管理员"
                    : u.seller == SellerStatus::kApproved ? L"买家 / 卖家"
                                                          : L"买家",
                    u.status == AccountStatus::kFrozen ? L"已冻结" : L"正常",
                    SellerState(u.seller)});
  } else if (page_ == kAudits) {
    for (const auto& a : app_->Audits(session_)) {
      const std::wstring detail =
          a.detail == "success"         ? L"成功"
          : a.detail == "5 denials/60s" ? L"一分钟内五次越权"
          : a.action == "access_denied" ? AuditAction(a.detail)
                                        : Wide(a.detail);
      AddRow(a.id,
             {std::to_wstring(a.id), LocalTime(a.timestamp),
              std::to_wstring(a.actor_id), AuditAction(a.action), detail});
    }
  } else if (page_ == kStats) {
    statistics_ = app_->Stats(session_);
  }
  if (table_) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i] == selected)
        ListView_SetItemState(table_, static_cast<int>(i),
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    ShowWindow(empty_, rows_.empty() ? SW_SHOW : SW_HIDE);
  }
  UpdateButtons();
  InvalidateRect(window_, nullptr, FALSE);
}
bool Desktop::Allowed(int command) const {
  if (command == kRefresh || command == kSearch || command == kReset)
    return true;
  if (command == kDetails) return Selection() != 0;
  if (session_.empty()) return false;
  const bool normal = user_.status == AccountStatus::kNormal;
  const bool seller = normal && user_.seller == SellerStatus::kApproved;
  if (command == kApplySeller)
    return normal && !user_.admin &&
           (user_.seller == SellerStatus::kNone ||
            user_.seller == SellerStatus::kRejected);
  if (command == kDraft) return seller && !user_.admin;
  if (command == kClearCart) return !products_.empty();
  const Id id = Selection();
  if (!id) return false;
  if (ProductPage(page_)) {
    const auto& p = Record(products_, id);
    const bool owner = p.seller_id == user_.id;
    if (command == kUnfavorite || command == kRemoveCart) return true;
    if (command == kFavorite)
      return normal && p.status == ProductStatus::kPublished;
    if (command == kReserve || command == kAddCart)
      return normal && !owner && !p.hold_id &&
             p.status == ProductStatus::kPublished;
    if (command == kEdit)
      return seller && owner && !p.hold_id &&
             p.status != ProductStatus::kDeleted;
    if (command == kSubmit)
      return seller && owner && !p.hold_id &&
             (p.status == ProductStatus::kDraft ||
              p.status == ProductStatus::kRejected ||
              p.status == ProductStatus::kOffline);
    if (command == kDown)
      return normal && (owner || user_.admin) &&
             p.status == ProductStatus::kPublished;
    if (command == kDelete)
      return seller && owner && !p.hold_id &&
             p.status != ProductStatus::kPublished &&
             p.status != ProductStatus::kPending &&
             p.status != ProductStatus::kDeleted;
    if (command == kApproveProduct || command == kRejectProduct)
      return user_.admin && normal && p.status == ProductStatus::kPending;
  } else if (page_ == kReservations) {
    const auto& r = Record(reservations_, id);
    const bool ordered =
        std::any_of(orders_.begin(), orders_.end(),
                    [&](const Order& o) { return o.reservation_id == id; });
    const bool owner =
        std::any_of(products_.begin(), products_.end(), [&](const Product& p) {
          return p.id == r.product_id && p.seller_id == user_.id;
        });
    if (command == kCreateOrder)
      return normal && r.buyer_id == user_.id && !ordered &&
             r.status == ReservationStatus::kConfirmed;
    if (command == kConfirm)
      return seller && owner && r.status == ReservationStatus::kPending;
    if (command == kCancel)
      return (owner || r.buyer_id == user_.id) && !ordered &&
             (r.status == ReservationStatus::kPending ||
              r.status == ReservationStatus::kConfirmed);
  } else if (page_ == kOrders) {
    const auto& o = Record(orders_, id);
    const bool buyer = o.buyer_id == user_.id;
    if (command == kPay)
      return normal && buyer && o.status == OrderStatus::kUnpaid;
    if (command == kCancelOrder)
      return buyer && o.status == OrderStatus::kUnpaid;
    if (command == kShip)
      return seller && o.seller_id == user_.id &&
             o.status == OrderStatus::kPaid;
    if (command == kReceive) return buyer && o.status == OrderStatus::kShipped;
    if (command == kRefund)
      return buyer && (o.status == OrderStatus::kPaid ||
                       o.status == OrderStatus::kShipped);
    if (command == kApproveRefund || command == kRejectRefund)
      return normal && (user_.admin || o.seller_id == user_.id) &&
             o.status == OrderStatus::kRefundRequested;
  } else if (page_ == kUsers) {
    const auto& u = Record(users_, id);
    if (command == kApproveSeller || command == kRejectSeller)
      return user_.admin && u.seller == SellerStatus::kPending &&
             u.status == AccountStatus::kNormal;
    if (command == kFreeze)
      return user_.admin && !u.admin && u.status == AccountStatus::kNormal;
    if (command == kUnfreeze)
      return user_.admin && !u.admin && u.status == AccountStatus::kFrozen;
  }
  return false;
}
void Desktop::UpdateButtons() {
  if (!login_) return;
  ShowWindow(login_, session_.empty() ? SW_SHOW : SW_HIDE);
  ShowWindow(registration_, session_.empty() ? SW_SHOW : SW_HIDE);
  ShowWindow(logout_, session_.empty() ? SW_HIDE : SW_SHOW);
  for (const auto& entry : navigation_) {
    const bool admin_only = entry.first == kUsers || entry.first == kAudits;
    const bool seller_only = entry.first == kProducts || entry.first == kStats;
    const bool visible =
        (!admin_only && !seller_only) ||
        (!session_.empty() &&
         (user_.admin ||
          (seller_only && user_.seller == SellerStatus::kApproved)));
    ShowWindow(entry.second, visible ? SW_SHOW : SW_HIDE);
    InvalidateRect(entry.second, nullptr, TRUE);
  }
  int visible_count = 0;
  for (const auto& entry : navigation_)
    if (GetWindowLongPtrW(entry.second, GWL_STYLE) & WS_VISIBLE)
      ++visible_count;
  const int step =
      (std::min)(58, (height_ - 180) / (std::max)(1, visible_count));
  int y = 112;
  for (int page : kPages)
    if (GetWindowLongPtrW(navigation_.at(page), GWL_STYLE) & WS_VISIBLE) {
      Place(navigation_.at(page), 16, y, 176, (std::min)(46, step - 6));
      y += step;
    }
  for (const auto& action : actions_)
    EnableWindow(action.second, Allowed(action.first));
}
void Desktop::Auth(bool registration) {
  std::vector<Field> fields = {{L"手机号", L"", false, 11},
                               {L"密码", L"", true, 20}};
  if (registration) {
    fields.push_back({L"昵称", L"", false, 60});
    fields.push_back({L"确认密码", L"", true, 20});
  }
  const bool accepted = ShowForm(
      window_, registration ? L"注册账号" : L"登录",
      registration ? L"密码为 8～20 位，需同时包含英文字母和数字。"
                   : L"请输入注册手机号和密码。冻结账号也可登录查看历史。",
      fields, [&](const std::vector<std::wstring>& values) {
        if (registration) {
          Require(values[1] == values[3], ErrorCode::kValidation,
                  "Passwords differ");
          app_->Register(Utf8(values[0]), Utf8(values[1]), Utf8(values[2]));
        }
        const auto token = app_->Login(Utf8(values[0]), Utf8(values[1]));
        if (!session_.empty()) app_->Logout(session_);
        session_ = token;
        user_ = app_->Me(session_);
      });
  if (accepted) {
    BuildPage();
    Refresh();
    SetStatus(registration ? L"注册成功，已自动登录。" : L"登录成功。");
  }
}
void Desktop::Setup() {
  const bool accepted =
      ShowForm(window_, L"首次使用 · 创建管理员",
               L"当前没有账号。请先设置管理员，之后由管理员审核卖家和商品。",
               {{L"管理员手机号", L"", false, 11},
                {L"管理员密码", L"", true, 20},
                {L"确认密码", L"", true, 20}},
               [&](const std::vector<std::wstring>& values) {
                 Require(values[1] == values[2], ErrorCode::kValidation,
                         "Passwords differ");
                 app_->BootstrapAdmin(Utf8(values[0]), Utf8(values[1]));
                 session_ = app_->Login(Utf8(values[0]), Utf8(values[1]));
                 user_ = app_->Me(session_);
               });
  if (!accepted) {
    DestroyWindow(window_);
    return;
  }
  needs_setup_ = false;
  Refresh();
  SetStatus(L"管理员创建成功。可注册普通账号并开始使用。");
}
bool Desktop::ProductForm(bool edit) {
  Product product;
  if (edit) product = Record(products_, Selection());
  return ShowForm(
      window_, edit ? L"编辑商品" : L"新建商品",
      L"保存为草稿，提交并通过审核后发布。价格单位为元。",
      {{L"商品名称", Wide(product.title), false, 120},
       {L"分类", Wide(product.category), false, 60},
       {L"价格 / 元", edit ? Amount(product.price) : L"", false, 12},
       {L"商品描述", Wide(product.description), false, 1000}},
      [&](const std::vector<std::wstring>& values) {
        const Money price = ParseAmount(values[2]);
        if (edit)
          app_->EditProduct(session_, product.id, product.version,
                            Utf8(values[0]), Utf8(values[1]), price,
                            Utf8(values[3]));
        else
          app_->PublishDraft(session_, Utf8(values[0]), Utf8(values[1]), price,
                             Utf8(values[3]));
      });
}
void Desktop::ShowDetails() {
  Require(Selection() != 0, ErrorCode::kValidation, "Select a record");
  std::wstring text;
  if (page_ == kOrders) {
    const auto& o = Record(orders_, Selection());
    text = L"订单编号：" + std::to_wstring(o.id) + L"\n商品：" +
           Wide(o.title_snapshot) + L"\n金额：¥ " + Amount(o.amount) +
           L"\n状态：" + OrderState(o.status) + L"\n收货地址：" +
           Wide(o.address_snapshot) + L"\n物流单号：" +
           (o.tracking_number.empty() ? L"尚未填写" : Wide(o.tracking_number)) +
           L"\n结算：" + (o.settlement_held ? L"暂缓结算" : L"无暂缓标记");
  } else {
    const auto& p = Record(products_, Selection());
    text = Wide(p.title) + L"\n\n价格：¥ " + Amount(p.price) + L"\n分类：" +
           Wide(p.category) + L"\n状态：" + ProductState(p.status) +
           L"\n预约：" + (p.hold_id ? L"已被占用" : L"未占用") +
           L"\n\n商品描述\n" + Wide(p.description);
  }
  MessageBoxW(window_, text.c_str(),
              page_ == kOrders ? L"订单详情" : L"商品详情", MB_OK);
}
void Desktop::Action(int command) {
  if (command >= kBrowse && command <= kCart) {
    Navigate(command);
    return;
  }
  if (command == kLogin || command == kRegister) {
    Auth(command == kRegister);
    return;
  }
  if (command == kLogout) {
    app_->Logout(session_);
    session_.clear();
    user_ = {};
    page_ = kBrowse;
    search_ = {};
    BuildPage();
    Refresh();
    SetStatus(L"已退出登录。");
    return;
  }
  if (command == IDOK || command == IDCANCEL) return;
  if (command == kRefresh) {
    Refresh();
    SetStatus(L"已刷新。");
    return;
  }
  if (command == kSearch) {
    Search next;
    next.keyword = Utf8(WindowText(filters_[1]));
    next.category = Utf8(WindowText(filters_[3]));
    const auto minimum = WindowText(filters_[5]);
    const auto maximum = WindowText(filters_[7]);
    next.min_price = minimum.empty() ? 0 : ParseAmount(minimum);
    next.max_price = maximum.empty() ? 100000000 : ParseAmount(maximum);
    app_->SearchProducts(next);
    search_ = next;
    Refresh();
    SetStatus(L"搜索完成，共 " + std::to_wstring(rows_.size()) + L" 件商品。");
    return;
  }
  if (command == kReset) {
    search_ = {};
    BuildPage();
    Refresh();
    SetStatus(L"搜索条件已重置。");
    return;
  }
  if (command == kDetails) {
    ShowDetails();
    return;
  }
  if (!Allowed(command)) return;
  const Id id = Selection();
  bool changed = true;
  if (command == kDraft || command == kEdit) {
    changed = ProductForm(command == kEdit);
  } else if (command == kFavorite) {
    app_->AddFavorite(session_, id);
  } else if (command == kUnfavorite) {
    app_->RemoveFavorite(session_, id);
  } else if (command == kAddCart) {
    app_->AddToCart(session_, id);
  } else if (command == kRemoveCart) {
    app_->RemoveFromCart(session_, id);
  } else if (command == kClearCart) {
    if (!Confirm(window_, L"确认清空自己的购物车？收藏夹和预约不会改变。"))
      return;
    app_->ClearCart(session_);
  } else if (command == kReserve) {
    const auto product = Record(products_, id);
    if (!Confirm(window_, L"预约“" + Wide(product.title) +
                              L"”？\n当前价格：¥ " + Amount(product.price) +
                              L"\n提交后将等待卖家确认。"))
      return;
    app_->Reserve(session_, id, product.version, RandomToken());
  } else if (command == kSubmit) {
    app_->SubmitProduct(session_, id);
  } else if (command == kDown || command == kDelete) {
    if (!Confirm(window_, command == kDown
                              ? L"确认下架该商品？未建单预约将取消。"
                              : L"确认删除该商品？删除后不可再次发布。"))
      return;
    if (command == kDown)
      app_->TakeDown(session_, id);
    else
      app_->DeleteProduct(session_, id);
  } else if (command == kApproveProduct || command == kRejectProduct) {
    app_->ReviewProduct(session_, id, command == kApproveProduct);
  } else if (command == kConfirm || command == kCancel) {
    const auto reservation = Record(reservations_, id);
    if (command == kCancel && !Confirm(window_, L"确认取消该预约？")) return;
    if (command == kConfirm)
      app_->Confirm(session_, id, reservation.version);
    else
      app_->Cancel(session_, id, reservation.version);
  } else if (command == kCreateOrder) {
    changed = ShowForm(window_, L"创建订单", L"请填写本次订单的收货地址。",
                       {{L"收货地址", L"", false, 300}},
                       [&](const std::vector<std::wstring>& values) {
                         app_->CreateOrder(session_, id, Utf8(values[0]));
                       });
  } else if (command == kPay) {
    const auto order = Record(orders_, id);
    if (!Confirm(window_, L"确认模拟付款 ¥ " + Amount(order.amount) +
                              L"？\n本操作不涉及真实资金。"))
      return;
    app_->Pay(session_, id, "desktop-payment-" + std::to_string(id));
  } else if (command == kCancelOrder || command == kReceive ||
             command == kRefund) {
    if (!Confirm(window_, command == kCancelOrder ? L"确认取消未付款订单？"
                          : command == kReceive   ? L"确认已经收到商品？"
                                                  : L"确认申请全额退款？"))
      return;
    if (command == kCancelOrder) app_->CancelOrder(session_, id);
    if (command == kReceive) app_->Receive(session_, id);
    if (command == kRefund) app_->RequestRefund(session_, id);
  } else if (command == kShip) {
    changed = ShowForm(window_, L"填写发货信息",
                       L"填写物流单号后，订单将变为已发货。",
                       {{L"物流单号", L"", false, 100}},
                       [&](const std::vector<std::wstring>& values) {
                         app_->Ship(session_, id, Utf8(values[0]));
                       });
  } else if (command == kApproveRefund || command == kRejectRefund) {
    if (!Confirm(window_, command == kApproveRefund
                              ? L"同意全额模拟退款？"
                              : L"驳回退款并恢复原订单阶段？"))
      return;
    app_->ReviewRefund(session_, id, command == kApproveRefund);
  } else if (command == kApproveSeller || command == kRejectSeller) {
    app_->ReviewSeller(session_, id, command == kApproveSeller);
  } else if (command == kFreeze || command == kUnfreeze) {
    if (!Confirm(
            window_,
            command == kFreeze
                ? L"确认冻结账号？\n其已发布商品将下架，尚未建单的预约将取消。"
                : L"确认解冻账号？商品不会自动恢复发布。"))
      return;
    app_->Freeze(session_, id, command == kFreeze);
  } else if (command == kApplySeller) {
    if (!Confirm(window_, L"提交卖家资格申请？审核通过后即可发布商品。"))
      return;
    app_->ApplySeller(session_);
  } else {
    changed = false;
  }
  Refresh();
  if (changed) SetStatus(L"操作成功，数据已保存。");
}
void Desktop::SetStatus(const std::wstring& text, bool error) {
  status_ = text;
  status_error_ = error;
  InvalidateRect(window_, nullptr, FALSE);
}
void Desktop::DrawTextAt(HDC dc, const std::wstring& text, int x, int y,
                         int width, int height, HFONT font, COLORREF color) {
  SelectObject(dc, font);
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  RECT rectangle{Scale(x), Scale(y), Scale(x + width), Scale(y + height)};
  DrawTextW(dc, text.c_str(), -1, &rectangle,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}
void Desktop::Card(HDC dc, int x, int y, int width, int height) {
  HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
  const auto old = SelectObject(dc, brush);
  const auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, Scale(x), Scale(y), Scale(x + width), Scale(y + height),
            Scale(12), Scale(12));
  SelectObject(dc, pen);
  SelectObject(dc, old);
  DeleteObject(brush);
}
void Desktop::Paint(HDC supplied) {
  PAINTSTRUCT paint{};
  HDC target = supplied ? supplied : BeginPaint(window_, &paint);
  RECT rectangle{};
  GetClientRect(window_, &rectangle);
  HDC dc = CreateCompatibleDC(target);
  HBITMAP bitmap =
      CreateCompatibleBitmap(target, (std::max)(1L, rectangle.right),
                             (std::max)(1L, rectangle.bottom));
  const auto old_bitmap = SelectObject(dc, bitmap);
  FillRect(dc, &rectangle, background_);
  RECT sidebar{0, 0, Scale(208), rectangle.bottom};
  FillRect(dc, &sidebar, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  RECT header{Scale(208), 0, rectangle.right, Scale(76)};
  FillRect(dc, &header, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  DrawTextAt(dc, L"二手交易平台", 24, 25, 180, 30, brand_font_, kText);
  DrawTextAt(dc, L"让闲置继续发挥价值", 24, height_ - 68, 175, 24, font_,
             kMuted);
  DrawTextAt(dc, L"首页  /  " + std::wstring(PageName(page_)), 240, 29, 400, 24,
             font_, kMuted);
  if (!session_.empty())
    DrawTextAt(
        dc,
        (user_.admin ? L"管理员" : Wide(user_.nickname)) +
            (user_.admin                               ? L" · 平台管理"
             : user_.seller == SellerStatus::kApproved ? L" · 买家 / 卖家"
                                                       : L" · 买家"),
        width_ - 480, 29, 320, 24, font_, kText);
  DrawTextAt(dc, PageName(page_), 240, 102, width_ - 280, 42, title_font_,
             kText);
  DrawTextAt(dc, PageDescription(page_), 240, 148, width_ - 280, 25, font_,
             kMuted);
  if (page_ == kProfile && !session_.empty()) {
    Card(dc, 240, 186, width_ - 272, 198);
    DrawTextAt(dc, L"昵称：" + (user_.admin ? L"管理员" : Wide(user_.nickname)),
               264, 208, 600, 26, brand_font_, kText);
    DrawTextAt(dc, L"用户编号：" + std::to_wstring(user_.id), 264, 252, 300, 24,
               font_, kMuted);
    DrawTextAt(dc, L"卖家资格：" + SellerState(user_.seller), 580, 252, 340, 24,
               font_, kText);
    DrawTextAt(
        dc,
        user_.status == AccountStatus::kFrozen
            ? L"账号已冻结：可查看历史、收货和申请退款，暂不能发起新交易。"
        : user_.admin ? L"账号正常。可在用户管理和商品管理中审核申请。"
                      : L"账号正常。申请卖家资格并通过审核后，即可发布商品。",
        264, 302, width_ - 320, 56, font_,
        user_.status == AccountStatus::kFrozen ? RGB(163, 83, 26) : kText);
  }
  if (page_ == kStats) {
    const int card_width = (width_ - 304) / 3;
    const std::vector<std::wstring> labels = {
        L"商品数量（含历史）", L"已完成订单", L"已完成金额 / 元"};
    const std::vector<std::wstring> values = {
        std::to_wstring(statistics_.products),
        std::to_wstring(statistics_.completed_orders),
        Amount(statistics_.completed_amount)};
    for (int i = 0; i < 3; ++i) {
      const int x = 240 + i * (card_width + 16);
      Card(dc, x, 192, card_width, 136);
      DrawTextAt(dc, labels[i], x + 24, 214, card_width - 40, 24, font_,
                 kMuted);
      DrawTextAt(dc, values[i], x + 24, 254, card_width - 40, 42, title_font_,
                 kAccent);
    }
  }
  DrawTextAt(dc, status_, 240, height_ - 36, width_ - 280, 28, font_,
             status_error_ ? RGB(175, 44, 44) : kMuted);
  BitBlt(target, 0, 0, rectangle.right, rectangle.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
  if (!supplied) EndPaint(window_, &paint);
}
void Desktop::DrawButton(DRAWITEMSTRUCT* item) {
  const bool navigation = item->CtlID >= kBrowse && item->CtlID <= kCart;
  const bool active = navigation && static_cast<int>(item->CtlID) == page_;
  const bool primary = item->CtlID == kLogin || item->CtlID == kSearch ||
                       item->CtlID == kReserve || item->CtlID == kDraft ||
                       item->CtlID == kCreateOrder;
  const bool disabled = (item->itemState & ODS_DISABLED) != 0;
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  COLORREF fill = active                 ? RGB(230, 241, 235)
                  : primary && !disabled ? kAccent
                                         : RGB(255, 255, 255);
  if (pressed && !disabled) fill = RGB(209, 228, 218);
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, navigation ? fill : RGB(213, 220, 223));
  const auto old_brush = SelectObject(item->hDC, brush);
  const auto old_pen = SelectObject(item->hDC, pen);
  RECT bounds = item->rcItem;
  FillRect(item->hDC, &bounds,
           navigation ? static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))
                      : background_);
  RoundRect(item->hDC, bounds.left, bounds.top, bounds.right, bounds.bottom,
            Scale(8), Scale(8));
  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(item->hDC, disabled  ? RGB(163, 170, 178)
                          : active  ? kAccent
                          : primary ? RGB(255, 255, 255)
                                    : kText);
  SelectObject(item->hDC, font_);
  const auto label = WindowText(item->hwndItem);
  DrawTextW(item->hDC, label.c_str(), -1, &bounds,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (item->itemState & ODS_FOCUS) {
    InflateRect(&bounds, -4, -4);
    DrawFocusRect(item->hDC, &bounds);
  }
  SelectObject(item->hDC, old_pen);
  SelectObject(item->hDC, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
}
}  // namespace
}  // namespace desktop

int RunDesktop(Marketplace* service, bool needs_setup) {
  desktop::Desktop desktop(service, needs_setup);
  return desktop.Run();
}
}  // namespace market
