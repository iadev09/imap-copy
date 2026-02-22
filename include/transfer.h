#pragma once

#include "app_types.h"

namespace imap_copy {

TransferStats transferMessages(const AppConfig &cfg, bool delete_after_copy);

}  // namespace imap_copy
