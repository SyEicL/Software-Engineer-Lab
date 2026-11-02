#include "market/repository.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace market {
namespace {
void ValidStream(bool valid) {
  Require(valid, ErrorCode::kStorage, "Invalid or truncated database snapshot");
}
#ifdef _WIN32
std::wstring Wide(const std::string& text) {
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       text.c_str(), -1, nullptr, 0);
  Require(size > 0, ErrorCode::kStorage, "Invalid UTF-8 file path");
  std::wstring wide(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, &wide[0],
                      size);
  wide.resize(size - 1);
  return wide;
}
class FileHandle {
 public:
  explicit FileHandle(HANDLE handle) : handle_(handle) {}
  ~FileHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
  }
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;
  HANDLE get() const { return handle_; }

 private:
  HANDLE handle_;
};
#endif
void CreateParents(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string::npos || slash == 0) return;
  const auto parent = path.substr(0, slash);
  if (parent.size() == 2 && parent[1] == ':') return;
  CreateParents(parent);
#ifdef _WIN32
  const auto wide = Wide(parent);
  if (!CreateDirectoryW(wide.c_str(), nullptr)) {
    Require(GetLastError() == ERROR_ALREADY_EXISTS, ErrorCode::kStorage,
            "Cannot create data directory");
  }
#else
  Require(mkdir(parent.c_str(), 0700) == 0 || errno == EEXIST,
          ErrorCode::kStorage, "Cannot create data directory");
#endif
}
std::string ReadFileData(const std::string& path) {
#ifdef _WIN32
  FileHandle file(CreateFileW(Wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    Require(GetLastError() == ERROR_FILE_NOT_FOUND, ErrorCode::kStorage,
            "Cannot open database");
    return {};
  }
  LARGE_INTEGER size{};
  Require(GetFileSizeEx(file.get(), &size) && size.QuadPart <= 67108864,
          ErrorCode::kStorage, "Snapshot exceeds MVP limit (64 MiB)");
  std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  Require(ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()),
                   &read, nullptr) &&
              read == data.size(),
          ErrorCode::kStorage, "Cannot read complete database");
  Require(!data.empty(), ErrorCode::kStorage, "Database is empty or corrupted");
  return data;
#else
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    Require(errno == ENOENT, ErrorCode::kStorage, "Cannot open database");
    return {};
  }
  std::ostringstream data;
  data << file.rdbuf();
  Require(!file.bad() && !data.str().empty(), ErrorCode::kStorage,
          "Cannot read database");
  return data.str();
#endif
}
void WriteFileData(const std::string& path, const std::string& data) {
  Require(data.size() <= 67108864, ErrorCode::kStorage,
          "Snapshot exceeds MVP limit (64 MiB)");
#ifdef _WIN32
  FileHandle file(CreateFileW(Wide(path).c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  Require(file.get() != INVALID_HANDLE_VALUE, ErrorCode::kStorage,
          "Cannot open temporary snapshot");
  DWORD written = 0;
  Require(WriteFile(file.get(), data.data(), static_cast<DWORD>(data.size()),
                    &written, nullptr) &&
              written == data.size() && FlushFileBuffers(file.get()),
          ErrorCode::kStorage, "Cannot write complete database");
#else
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << data;
  file.flush();
  Require(file.good(), ErrorCode::kStorage, "Cannot write database");
  file.close();
  Require(!file.fail(), ErrorCode::kStorage, "Cannot close database");
#endif
}
template <typename E>
E Enum(int value, int max) {
  ValidStream(value >= 0 && value <= max);
  return static_cast<E>(value);
}
std::size_t Count(std::istream& input) {
  std::size_t count = 0;
  input >> count;
  ValidStream(input.good() && count <= 100000);
  return count;
}
void Validate(const State& state) {
  ValidStream(state.users.size() <= 100000 && state.products.size() <= 100000 &&
              state.reservations.size() <= 100000 &&
              state.orders.size() <= 100000 && state.audits.size() <= 100000 &&
              state.favorites.size() <= 100000 && state.cart.size() <= 100000);
  std::set<Id> ids;
  auto id = [&](Id value) {
    ValidStream(value > 0 && value < state.next_id && ids.insert(value).second);
  };
  std::set<std::string> phones;
  std::set<Id> users;
  std::set<Id> products;
  std::set<Id> reservations;
  for (const auto& user : state.users) {
    id(user.id);
    users.insert(user.id);
    ValidStream(phones.insert(user.phone).second && user.salt.size() == 32 &&
                user.digest.size() == 64);
  }
  for (const auto& product : state.products) {
    id(product.id);
    products.insert(product.id);
    ValidStream(users.count(product.seller_id) && product.price > 0 &&
                product.version > 0);
  }
  for (const auto& r : state.reservations) {
    id(r.id);
    reservations.insert(r.id);
    ValidStream(users.count(r.buyer_id) && products.count(r.product_id) &&
                r.version > 0 && !r.request_key.empty());
  }
  std::set<Id> order_reservations;
  for (const auto& order : state.orders) {
    id(order.id);
    ValidStream(reservations.count(order.reservation_id) &&
                order_reservations.insert(order.reservation_id).second &&
                users.count(order.buyer_id) && users.count(order.seller_id) &&
                products.count(order.product_id) && order.amount > 0 &&
                order.buyer_id != order.seller_id);
  }
  for (const auto& p : state.products) {
    if (!p.hold_id) continue;
    bool found = false;
    for (const auto& r : state.reservations) {
      if (r.id == p.hold_id && r.product_id == p.id &&
          r.status != ReservationStatus::kCancelled)
        found = true;
    }
    ValidStream(found);
  }
  for (const auto& a : state.audits) id(a.id);
  for (const auto* entries : {&state.favorites, &state.cart}) {
    std::set<std::pair<Id, Id>> pairs;
    for (const auto& item : *entries) {
      ValidStream(users.count(item.user_id) &&
                  products.count(item.product_id) &&
                  !item.title_snapshot.empty() && item.price_snapshot > 0 &&
                  pairs.insert({item.user_id, item.product_id}).second);
    }
  }
}
}  // namespace

FileRepository::FileRepository(const std::string& path) : path_(path) {
  CreateParents(path_);
#ifdef _WIN32
  const auto lock_path = Wide(path_ + ".lock");
  lock_ = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(lock_ != INVALID_HANDLE_VALUE, ErrorCode::kStorage,
          "Database is already open, or directory is not writable");
#else
  const int fd = open((path_ + ".lock").c_str(), O_CREAT | O_RDWR, 0600);
  Require(fd >= 0, ErrorCode::kStorage, "Cannot open database lock");
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    throw Error(ErrorCode::kStorage, "Database is already open");
  }
  lock_ = reinterpret_cast<void*>(static_cast<intptr_t>(fd + 1));
#endif
}
FileRepository::~FileRepository() {
#ifdef _WIN32
  if (lock_ && lock_ != INVALID_HANDLE_VALUE) CloseHandle(lock_);
#else
  if (lock_) close(static_cast<int>(reinterpret_cast<intptr_t>(lock_)) - 1);
#endif
}
State FileRepository::Load() {
  const auto data = ReadFileData(path_);
  if (data.empty()) return {};
  std::istringstream in(data);
  std::string magic;
  int schema = 0;
  State s;
  in >> magic >> schema >> s.next_id;
  ValidStream(in.good() && magic == "MARKET" && (schema == 1 || schema == 2) &&
              s.next_id > 0);
  for (auto n = Count(in); n > 0; --n) {
    User u;
    int status, seller, admin;
    in >> u.id >> std::quoted(u.phone) >> std::quoted(u.nickname) >>
        std::quoted(u.salt) >> std::quoted(u.digest) >> admin >> status >>
        seller;
    ValidStream(in.good() && (admin == 0 || admin == 1));
    u.admin = admin != 0;
    u.status = Enum<AccountStatus>(status, 1);
    u.seller = Enum<SellerStatus>(seller, 3);
    s.users.push_back(u);
  }
  for (auto n = Count(in); n > 0; --n) {
    Product p;
    int status;
    in >> p.id >> p.seller_id >> std::quoted(p.title) >>
        std::quoted(p.category) >> std::quoted(p.description) >> p.price >>
        status >> p.version >> p.hold_id;
    ValidStream(in.good());
    p.status = Enum<ProductStatus>(status, 5);
    s.products.push_back(p);
  }
  for (auto n = Count(in); n > 0; --n) {
    Reservation r;
    int status;
    in >> r.id >> r.buyer_id >> r.product_id >> std::quoted(r.request_key) >>
        status >> r.version;
    ValidStream(in.good());
    r.status = Enum<ReservationStatus>(status, 3);
    s.reservations.push_back(r);
  }
  for (auto n = Count(in); n > 0; --n) {
    Order o;
    int status, before, held;
    in >> o.id >> o.reservation_id >> o.buyer_id >> o.seller_id >>
        o.product_id >> o.amount >> std::quoted(o.title_snapshot) >>
        std::quoted(o.address_snapshot) >> std::quoted(o.tracking_number) >>
        std::quoted(o.payment_key) >> status >> before >> held;
    ValidStream(in.good() && (held == 0 || held == 1));
    o.status = Enum<OrderStatus>(status, 6);
    o.before_refund = Enum<OrderStatus>(before, 6);
    o.settlement_held = held != 0;
    s.orders.push_back(o);
  }
  for (auto n = Count(in); n > 0; --n) {
    Audit a;
    in >> a.id >> a.actor_id >> a.timestamp >> std::quoted(a.action) >>
        std::quoted(a.detail);
    ValidStream(in.good());
    s.audits.push_back(a);
  }
  if (schema == 2) {
    for (auto* entries : {&s.favorites, &s.cart}) {
      for (auto n = Count(in); n > 0; --n) {
        SavedItem item;
        in >> item.user_id >> item.product_id >>
            std::quoted(item.title_snapshot) >> item.price_snapshot;
        ValidStream(in.good());
        entries->push_back(item);
      }
    }
  }
  std::string end;
  in >> end;
  ValidStream(end == "END");
  in >> std::ws;
  ValidStream(in.eof());
  Validate(s);
  return s;
}
void FileRepository::Save(const State& s) {
  Validate(s);
  const auto temp = path_ + ".tmp";
  std::ostringstream out;
  out << "MARKET 2 " << s.next_id << '\n' << s.users.size() << '\n';
  for (const auto& u : s.users) {
    out << u.id << ' ' << std::quoted(u.phone) << ' ' << std::quoted(u.nickname)
        << ' ' << std::quoted(u.salt) << ' ' << std::quoted(u.digest) << ' '
        << u.admin << ' ' << static_cast<int>(u.status) << ' '
        << static_cast<int>(u.seller) << '\n';
  }
  out << s.products.size() << '\n';
  for (const auto& p : s.products) {
    out << p.id << ' ' << p.seller_id << ' ' << std::quoted(p.title) << ' '
        << std::quoted(p.category) << ' ' << std::quoted(p.description) << ' '
        << p.price << ' ' << static_cast<int>(p.status) << ' ' << p.version
        << ' ' << p.hold_id << '\n';
  }
  out << s.reservations.size() << '\n';
  for (const auto& r : s.reservations) {
    out << r.id << ' ' << r.buyer_id << ' ' << r.product_id << ' '
        << std::quoted(r.request_key) << ' ' << static_cast<int>(r.status)
        << ' ' << r.version << '\n';
  }
  out << s.orders.size() << '\n';
  for (const auto& o : s.orders) {
    out << o.id << ' ' << o.reservation_id << ' ' << o.buyer_id << ' '
        << o.seller_id << ' ' << o.product_id << ' ' << o.amount << ' '
        << std::quoted(o.title_snapshot) << ' '
        << std::quoted(o.address_snapshot) << ' '
        << std::quoted(o.tracking_number) << ' ' << std::quoted(o.payment_key)
        << ' ' << static_cast<int>(o.status) << ' '
        << static_cast<int>(o.before_refund) << ' ' << o.settlement_held
        << '\n';
  }
  out << s.audits.size() << '\n';
  for (const auto& a : s.audits) {
    out << a.id << ' ' << a.actor_id << ' ' << a.timestamp << ' '
        << std::quoted(a.action) << ' ' << std::quoted(a.detail) << '\n';
  }
  for (const auto* entries : {&s.favorites, &s.cart}) {
    out << entries->size() << '\n';
    for (const auto& item : *entries) {
      out << item.user_id << ' ' << item.product_id << ' '
          << std::quoted(item.title_snapshot) << ' ' << item.price_snapshot
          << '\n';
    }
  }
  out << "END\n";
  WriteFileData(temp, out.str());
#ifdef _WIN32
  Require(MoveFileExW(Wide(temp).c_str(), Wide(path_).c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
          ErrorCode::kStorage, "Cannot replace database snapshot");
#else
  Require(std::rename(temp.c_str(), path_.c_str()) == 0, ErrorCode::kStorage,
          "Cannot replace database snapshot");
#endif
}
}  // namespace market
