#ifndef MARKET_GUI_H_
#define MARKET_GUI_H_

#include "market/service.h"

namespace market {
// Windows desktop adapter. All business operations use Marketplace.
int RunDesktop(Marketplace* service, bool needs_setup);
}  // namespace market

#endif  // MARKET_GUI_H_
