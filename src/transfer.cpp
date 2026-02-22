#include "transfer.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "imap_client.h"

namespace imap_copy {

TransferStats transferMessages(const AppConfig &cfg, bool delete_after_copy) {
    TransferStats stats;
    ImapClient bootstrap_client(cfg.server);

    std::cout << "[INFO] Server: " << cfg.server.host << ":" << cfg.server.port
              << " tls=" << (cfg.server.tls ? "true" : "false")
              << " verify_tls=" << (cfg.server.verify_tls ? "true" : "false") << "\n";
    std::cout << "[INFO] From: user=" << cfg.from.user << " folder=" << cfg.from.folder << "\n";
    std::cout << "[INFO] To: user=" << cfg.to.user << " folder=" << cfg.to.folder << "\n";

    std::cout << "[INFO] Reading source UID list...\n";
    const std::vector<uint64_t> source_uids = bootstrap_client.listAllUids(cfg.from, cfg.from.folder);
    stats.source_total = source_uids.size();
    std::cout << "[INFO] Source messages found: " << stats.source_total << "\n";

    std::cout << "[INFO] Reading destination UID list...\n";
    const std::vector<uint64_t> dest_uids = bootstrap_client.listAllUids(cfg.to, cfg.to.folder);
    std::cout << "[INFO] Destination messages found: " << dest_uids.size() << "\n";

    std::cout << "[INFO] Building destination Message-ID set...\n";
    std::unordered_set<std::string> dest_message_ids;
    dest_message_ids.reserve(dest_uids.size() * 2 + 1);
    size_t dest_meta_fetch_failed = 0;
    size_t dest_meta_without_message_id = 0;

    const size_t progress_step = std::max<size_t>(1, dest_uids.size() / 20);

    for (size_t i = 0; i < dest_uids.size(); ++i) {
        const uint64_t uid = dest_uids[i];
        const std::optional<MessageMeta> meta = bootstrap_client.fetchMetaByUid(cfg.to, cfg.to.folder, uid);
        if (!meta.has_value()) {
            ++dest_meta_fetch_failed;
        } else if (meta->message_id.empty()) {
            ++dest_meta_without_message_id;
        } else {
            dest_message_ids.insert(meta->message_id);
        }

        const size_t done = i + 1;
        if (done == dest_uids.size() || done % progress_step == 0) {
            const size_t pct = dest_uids.empty() ? 100 : (done * 100) / dest_uids.size();
            std::cout << "[INFO] Destination Message-ID scan: " << done << "/" << dest_uids.size() << " (" << pct
                      << "%)\n";
        }
    }

    std::cout << "[INFO] Destination Message-ID entries loaded: " << dest_message_ids.size() << "\n";
    if (dest_meta_fetch_failed > 0 || (dest_uids.size() > 0 && dest_message_ids.empty())) {
        std::cerr << "[WARN] Destination metadata scan is incomplete (fetch_failed=" << dest_meta_fetch_failed
                  << ", missing_message_id=" << dest_meta_without_message_id
                  << "). Enabling server-side Message-ID verification.\n";
    }
    const bool enable_server_side_dedupe_fallback =
            (dest_meta_fetch_failed > 0) || (dest_uids.size() > 0 && dest_message_ids.empty());
    const std::unordered_set<std::string> empty_known_ids;

    std::unordered_set<std::string> pending_message_ids;
    pending_message_ids.reserve(source_uids.size() / 2 + 1);
    std::mutex message_id_mutex;

    std::atomic<size_t> next_index{0};
    std::atomic<size_t> copied{0};
    std::atomic<size_t> deleted{0};
    std::atomic<size_t> failed{0};
    std::atomic<size_t> already_exists{0};
    std::atomic<size_t> no_message_id{0};

    size_t worker_count = std::thread::hardware_concurrency();
    if (worker_count == 0) {
        worker_count = 4;
    }
    worker_count = std::max<size_t>(1, std::min(worker_count, source_uids.size() == 0 ? 1 : source_uids.size()));

    std::cout << "[INFO] Parallel copy workers: " << worker_count << "\n";

    auto worker = [&]() {
        ImapClient client(cfg.server);

        while (true) {
            const size_t index = next_index.fetch_add(1);
            if (index >= source_uids.size()) {
                break;
            }

            const uint64_t uid = source_uids[index];
            const std::optional<MessageMeta> meta = client.fetchMetaByUid(cfg.from, cfg.from.folder, uid);
            if (!meta.has_value()) {
                failed.fetch_add(1);
                continue;
            }

            if (meta->seen) {
                std::cerr << "[WARN] UID=" << uid << " message is Seen. Copying anyway.\n";
            }

            const std::string message_id = meta->message_id;
            if (message_id.empty()) {
                std::cerr << "[WARN] UID=" << uid
                          << " has no Message-ID. Skipping copy"
                          << " [from=\"" << (meta->from.empty() ? "-" : meta->from)
                          << "\", subject=\"" << (meta->subject.empty() ? "-" : meta->subject)
                          << "\", date=\"" << (meta->date.empty() ? "-" : meta->date) << "\"]\n";
                no_message_id.fetch_add(1);
                continue;
            }

            bool message_id_reserved = false;
            if (!message_id.empty()) {
                std::scoped_lock lock(message_id_mutex);
                if (dest_message_ids.find(message_id) != dest_message_ids.end() ||
                    pending_message_ids.find(message_id) != pending_message_ids.end()) {
                    already_exists.fetch_add(1);
                    continue;
                }
                pending_message_ids.insert(message_id);
                message_id_reserved = true;
            }

            auto releaseMessageIdReservation = [&]() {
                if (!message_id_reserved) {
                    return;
                }
                std::scoped_lock lock(message_id_mutex);
                pending_message_ids.erase(message_id);
                message_id_reserved = false;
            };

            if (message_id_reserved && enable_server_side_dedupe_fallback) {
                const bool exists_in_destination =
                        client.destinationHasMessageId(cfg.to, empty_known_ids, message_id);
                if (exists_in_destination) {
                    already_exists.fetch_add(1);
                    releaseMessageIdReservation();
                    continue;
                }
            }

            std::vector<char> raw_message;
            try {
                raw_message = client.downloadMessageByUid(cfg.from, cfg.from.folder, uid);
            } catch (const std::exception &ex) {
                std::cerr << "[ERROR] UID=" << uid << " download failed: " << ex.what() << "\n";
                failed.fetch_add(1);
                releaseMessageIdReservation();
                continue;
            }

            if (!meta->seen) {
                client.clearSeenByUid(cfg.from, uid);
            }

            const bool append_ok = client.appendMessage(cfg.to, cfg.to.folder, raw_message);
            if (!append_ok) {
                std::cerr << "[ERROR] UID=" << uid << " failed to append to destination\n";
                failed.fetch_add(1);
                releaseMessageIdReservation();
                continue;
            }

            copied.fetch_add(1);
            if (message_id_reserved) {
                std::scoped_lock lock(message_id_mutex);
                pending_message_ids.erase(message_id);
                dest_message_ids.insert(message_id);
                message_id_reserved = false;
            }

            if (!meta->seen && !message_id.empty()) {
                client.clearSeenByMessageId(cfg.to, message_id);
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

    stats.copied = copied.load();
    stats.deleted = deleted.load();
    stats.failed = failed.load();
    stats.already_exists = already_exists.load();
    stats.no_message_id = no_message_id.load();

    return stats;
}

}  // namespace imap_copy
