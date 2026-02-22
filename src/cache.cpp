#include "cache.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace imap_copy {
namespace {

std::string trim(const std::string &value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    if (first == value.size()) {
        return "";
    }
    size_t last = value.size() - 1;
    while (last > first && std::isspace(static_cast<unsigned char>(value[last])) != 0) {
        --last;
    }
    return value.substr(first, last - first + 1);
}

}  // namespace

TransferCache::TransferCache(const AppConfig &) {
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        throw std::runtime_error("HOME is not set. Cannot open cache directory.");
    }

    const std::filesystem::path base_dir = std::filesystem::path(home) / ".cache" / "imap-copy";
    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create cache directory: " + base_dir.string() +
                                 " (" + ec.message() + ")");
    }

    cache_path_ = base_dir / "uids.cache";
    lock_path_ = base_dir / "uids.lock";

    lock_fd_ = ::open(lock_path_.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd_ < 0) {
        throw std::runtime_error("Failed to open cache lock file: " + lock_path_.string() +
                                 " (" + std::strerror(errno) + ")");
    }
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        const std::string err = std::strerror(errno);
        ::close(lock_fd_);
        lock_fd_ = -1;
        throw std::runtime_error("Failed to acquire cache lock: " + lock_path_.string() +
                                 " (another imap-copy process may be running, " + err + ")");
    }

    std::ifstream in(cache_path_);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) {
            keys_.insert(line);
        }
    }
}

TransferCache::~TransferCache() {
    if (lock_fd_ >= 0) {
        ::flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
        lock_fd_ = -1;
    }
}

std::string TransferCache::makeUidKey(const uint64_t source_uid) const {
    return std::to_string(source_uid);
}

bool TransferCache::contains(const std::string &key) const {
    std::scoped_lock lock(mutex_);
    return keys_.find(key) != keys_.end();
}

bool TransferCache::insert(const std::string &key) {
    std::scoped_lock lock(mutex_);
    const auto [_, inserted] = keys_.insert(key);
    if (inserted) {
        dirty_ = true;
    }
    return inserted;
}

void TransferCache::save() {
    std::vector<std::string> snapshot;
    {
        std::scoped_lock lock(mutex_);
        if (!dirty_) {
            return;
        }
        snapshot.reserve(keys_.size());
        for (const auto &k : keys_) {
            snapshot.push_back(k);
        }
        dirty_ = false;
    }

    std::sort(snapshot.begin(), snapshot.end());

    const std::filesystem::path tmp_path = cache_path_.string() + ".tmp";
    std::ofstream out(tmp_path, std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open cache temp file for write: " + tmp_path.string());
    }
    for (const auto &k : snapshot) {
        out << k << '\n';
    }
    out.flush();
    if (!out.good()) {
        throw std::runtime_error("Failed to write cache temp file: " + tmp_path.string());
    }
    out.close();

    std::error_code ec;
    std::filesystem::rename(tmp_path, cache_path_, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        throw std::runtime_error("Failed to publish cache file: " + cache_path_.string() +
                                 " (" + ec.message() + ")");
    }
}

size_t TransferCache::size() const {
    std::scoped_lock lock(mutex_);
    return keys_.size();
}

const std::filesystem::path &TransferCache::cachePath() const {
    return cache_path_;
}

}  // namespace imap_copy
