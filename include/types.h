#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace imap_copy {

struct ServerConfig {
    std::string host;
    int port = 993;
    bool tls = true;
    bool verify_tls = true;
};

struct MailboxConfig {
    std::string user;
    std::string password;
    std::string folder;
};

struct AppConfig {
    ServerConfig server;
    MailboxConfig from;
    MailboxConfig to;
};

struct CliOptions {
    static constexpr size_t k_default_worker_count = 4;
    std::string config_path;
    bool delete_after_copy = false;
    size_t worker_count = k_default_worker_count;
};

struct TransferStats {
    size_t source_total = 0;
    size_t already_exists = 0;
    size_t no_message_id = 0;
    size_t copied = 0;
    size_t deleted = 0;
    size_t failed = 0;
};

struct MessageMeta {
    std::string message_id;
    std::string from;
    std::string date;
    std::string subject;
    bool seen = false;
};

}  // namespace imap_copy
