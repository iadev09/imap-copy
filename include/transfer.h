#pragma once

#include "types.h"

namespace imap_copy {

auto transferMessages(const AppConfig &cfg, bool delete_after_copy, size_t worker_count) -> TransferStats;

}  // namespace imap_copy
