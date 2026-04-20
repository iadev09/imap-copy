#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

#include "types.h"

namespace imap_copy {

class TransferCache {
public:
    explicit TransferCache(const AppConfig &cfg);
    ~TransferCache();

    TransferCache(const TransferCache &) = delete;
    auto operator=(const TransferCache &) -> TransferCache & = delete;

    [[nodiscard]] auto makeUidKey(uint64_t source_uid) const -> uint64_t;
    [[nodiscard]] auto contains(uint64_t key) const -> bool;
    auto insert(uint64_t key) -> bool;
    void save();

    [[nodiscard]] auto size() const -> size_t;
    [[nodiscard]] auto cachePath() const -> const std::filesystem::path &;

private:
    std::string key_seed_;
    std::filesystem::path cache_path_;
    std::filesystem::path lock_path_;
    std::unordered_set<uint64_t> keys_;
    bool dirty_ = false;
    int lock_fd_ = -1;
    mutable std::mutex mutex_;
};

}  // namespace imap_copy
