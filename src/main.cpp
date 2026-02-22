#include <curl/curl.h>

#include <iostream>
#include <stdexcept>

#include "config.h"
#include "transfer.h"

int main(int argc, char **argv) {
    using namespace imap_copy;

    try {
        const CliOptions opts = parseArgs(argc, argv);
        const AppConfig cfg = parseConfig(opts.config_path);

        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed");
        }

        struct CurlGlobalCleanupGuard {
            ~CurlGlobalCleanupGuard() { curl_global_cleanup(); }
        } cleanup_guard;

        const TransferStats stats = transferMessages(cfg, opts.delete_after_copy);

        std::cout << "\n[INFO] Transfer complete\n";
        std::cout << "[INFO] Source total messages: " << stats.source_total << "\n";
        std::cout << "[INFO] Already in destination: " << stats.already_exists << "\n";
        std::cout << "[INFO] Copied: " << stats.copied << "\n";
        std::cout << "[INFO] Deleted from source (--delete): " << stats.deleted << "\n";
        std::cout << "[INFO] Errors: " << stats.failed << "\n";

        return stats.failed == 0 ? 0 : 2;
    } catch (const std::exception &ex) {
        std::cerr << "[FATAL] " << ex.what() << "\n";
        return 1;
    }
}
