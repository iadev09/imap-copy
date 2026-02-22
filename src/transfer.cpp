#include "transfer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "client.h"
#include "cache.h"

namespace imap_copy {
namespace {

bool isDebugLoggingEnabled() {
    const char *value = std::getenv("IMAP_COPY_LOG_LEVEL");
    if (value == nullptr || *value == '\0') {
        return false;
    }
    const std::string level(value);
    return level == "DEBUG" || level == "debug" || level == "TRACE" || level == "trace" ||
           level == "1" || level == "true" || level == "TRUE";
}

size_t configuredWorkerCount(size_t source_count) {
    size_t worker_count = 5;
    const char *value = std::getenv("IMAP_COPY_WORKERS");
    if (value != nullptr && *value != '\0') {
        try {
            const unsigned long parsed = std::stoul(value);
            if (parsed > 0) {
                worker_count = static_cast<size_t>(parsed);
            }
        } catch (...) {
            // Ignore invalid override and keep default.
        }
    }
    worker_count = std::min<size_t>(worker_count, 5);
    if (source_count == 0) {
        return 1;
    }
    return std::max<size_t>(1, std::min(worker_count, source_count));
}

}  // namespace

TransferStats transferMessages(const AppConfig &cfg, bool delete_after_copy) {
    TransferStats stats;
    const bool debug_enabled = isDebugLoggingEnabled();
    ImapClient bootstrap_client(cfg.server);
    TransferCache cache(cfg);

    std::cout << "[INFO] Server: " << cfg.server.host << ":" << cfg.server.port
              << " tls=" << (cfg.server.tls ? "true" : "false")
              << " verify_tls=" << (cfg.server.verify_tls ? "true" : "false") << "\n";
    std::cout << "[INFO] From: user=" << cfg.from.user << " folder=" << cfg.from.folder << "\n";
    std::cout << "[INFO] To: user=" << cfg.to.user << " folder=" << cfg.to.folder << "\n";
    std::cout << "[INFO] Cache: " << cache.cachePath().string() << " entries=" << cache.size() << "\n";

    std::cout << "[INFO] Reading source UID list...\n";
    const std::vector<uint64_t> source_uids = bootstrap_client.listAllUids(cfg.from, cfg.from.folder);
    stats.source_total = source_uids.size();
    std::cout << "[INFO] Source messages found: " << stats.source_total << "\n";
    if (debug_enabled) {
        for (uint64_t uid : source_uids) {
            std::cerr << "[DEBUG] source_uid=" << uid
                      << " user=\"" << cfg.from.user
                      << "\" folder=\"" << cfg.from.folder << "\"\n";
        }
    }

    std::atomic<size_t> next_index{0};
    std::atomic<size_t> copied{0};
    std::atomic<size_t> deleted{0};
    std::atomic<size_t> failed{0};
    std::atomic<size_t> already_exists{0};

    const size_t worker_count = configuredWorkerCount(source_uids.size());
    std::cout << "[INFO] Parallel copy workers: " << worker_count << "\n";

    auto worker = [&]() {
        ImapClient client(cfg.server);

        while (true) {
            const size_t index = next_index.fetch_add(1);
            if (index >= source_uids.size()) {
                break;
            }

            const uint64_t uid = source_uids[index];
            uint64_t dest_max_uid_before = 0;
            bool have_dest_max_uid_before = false;
            try {
                const std::vector<uint64_t> before_uids = client.listAllUids(cfg.to, cfg.to.folder);
                if (!before_uids.empty()) {
                    dest_max_uid_before = *std::max_element(before_uids.begin(), before_uids.end());
                    have_dest_max_uid_before = true;
                }
            } catch (const std::exception &ex) {
                if (debug_enabled) {
                    std::cerr << "[DEBUG] UID=" << uid
                              << " could not read destination max UID before append: " << ex.what() << "\n";
                }
            }

            const uint64_t cache_key = cache.makeUidKey(uid);
            if (cache.contains(cache_key)) {
                already_exists.fetch_add(1);
                if (debug_enabled) {
                    std::cerr << "[DEBUG] source_uid=" << uid
                              << " skipped by cache\n";
                }
                continue;
            }

            std::vector<char> raw_message;
            try {
                raw_message = client.downloadMessageByUid(cfg.from, cfg.from.folder, uid);
            } catch (const std::exception &ex) {
                std::cerr << "[ERROR] UID=" << uid << " download failed: " << ex.what() << "\n";
                failed.fetch_add(1);
                continue;
            }

            const bool append_ok = client.appendMessage(cfg.to, cfg.to.folder, raw_message);
            if (!append_ok) {
                std::cerr << "[ERROR] UID=" << uid << " failed to append to destination\n";
                failed.fetch_add(1);
                continue;
            }

            copied.fetch_add(1);
            cache.insert(cache_key);

            bool dest_unseen_ok = false;
            if (!dest_unseen_ok) {
                try {
                    for (int attempt = 0; attempt < 6 && !dest_unseen_ok; ++attempt) {
                        const std::vector<uint64_t> after_uids = client.listAllUids(cfg.to, cfg.to.folder);
                        size_t changed = 0;
                        for (uint64_t to_uid : after_uids) {
                            if (!have_dest_max_uid_before || to_uid > dest_max_uid_before) {
                                if (client.clearSeenByUid(cfg.to, to_uid)) {
                                    ++changed;
                                }
                            }
                        }
                        dest_unseen_ok = changed > 0;
                        if (!dest_unseen_ok) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        }
                    }
                } catch (const std::exception &ex) {
                    std::cerr << "[WARN] UID=" << uid
                              << " could not enforce destination unseen by UID fallback: " << ex.what() << "\n";
                }
            }

            if (!dest_unseen_ok) {
                std::cerr << "[ERROR] UID=" << uid
                          << " copied, but destination unseen could not be enforced.\n";
                failed.fetch_add(1);
            }

            if (delete_after_copy && client.deleteSourceMessage(cfg.from, uid)) {
                deleted.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back(worker);
    }
    for (auto &t : workers) {
        t.join();
    }

    cache.save();
    std::cout << "[INFO] Cache saved: " << cache.cachePath().string() << " entries=" << cache.size() << "\n";

    stats.copied = copied.load();
    stats.deleted = deleted.load();
    stats.failed = failed.load();
    stats.already_exists = already_exists.load();
    stats.no_message_id = 0;
    return stats;
}

}  // namespace imap_copy
