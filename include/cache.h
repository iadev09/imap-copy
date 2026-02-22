#pragma once

#include <cstdint>
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
    TransferCache &operator=(const TransferCache &) = delete;

    [[nodiscard]] std::string makeUidKey(uint64_t source_uid) const;
    [[nodiscard]] bool contains(const std::string &key) const;
    bool insert(const std::string &key);
    void save();

    [[nodiscard]] size_t size() const;
    [[nodiscard]] const std::filesystem::path &cachePath() const;

private:
    std::filesystem::path cache_path_;
    std::filesystem::path lock_path_;
    std::unordered_set<std::string> keys_;
    bool dirty_ = false;
    int lock_fd_ = -1;
    mutable std::mutex mutex_;
};

}  // namespace imap_copy
