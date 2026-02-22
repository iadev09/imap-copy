#include "client.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imap_copy {
    namespace {
        struct Buffer {
            std::string data;
        };

        struct UploadPayload {
            const std::vector<char> *bytes = nullptr;
            size_t offset = 0;
        };

        size_t writeToString(void *ptr, size_t size, size_t nmemb, void *userdata) {
            const size_t total = size * nmemb;
            auto *buffer = static_cast<Buffer *>(userdata);
            buffer->data.append(static_cast<char *>(ptr), total);
            return total;
        }

        size_t readFromBuffer(char *dest, size_t size, size_t nmemb, void *userdata) {
            const size_t max_write = size * nmemb;
            auto *payload = static_cast<UploadPayload *>(userdata);
            if (payload->bytes == nullptr || payload->offset >= payload->bytes->size()) {
                return 0;
            }

            const size_t remaining = payload->bytes->size() - payload->offset;
            const size_t to_copy = std::min(remaining, max_write);
            std::copy_n(payload->bytes->data() + payload->offset, to_copy, dest);
            payload->offset += to_copy;
            return to_copy;
        }

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

        std::string toLower(std::string value) {
            std::ranges::transform(value, value.begin(),
                                   [](unsigned char c) {
                                       return static_cast<char>(std::tolower(c));
                                   });
            return value;
        }

        std::string normalizedMessageId(std::string message_id) {
            message_id = trim(message_id);
            if (message_id.empty()) {
                return message_id;
            }
            if (message_id.back() == '\r') {
                message_id.pop_back();
            }
            return message_id;
        }

        bool isDebugLoggingEnabled() {
            static const bool enabled = []() {
                const char *value = std::getenv("IMAP_COPY_LOG_LEVEL");
                if (value == nullptr || *value == '\0') {
                    return false;
                }
                const std::string lowered = toLower(trim(value));
                return lowered == "debug" || lowered == "trace" || lowered == "1" || lowered == "true";
            }();
            return enabled;
        }

        std::string logPreview(const std::string &value, size_t max_len = 120) {
            if (value.size() <= max_len) {
                return value;
            }
            if (max_len < 4) {
                return value.substr(0, max_len);
            }
            return value.substr(0, max_len - 3) + "...";
        }

        std::string baseUrl(const ServerConfig &server) {
            const std::string scheme = server.tls ? "imaps" : "imap";
            return scheme + "://" + server.host + ":" + std::to_string(server.port);
        }

        std::string escapePathSegment(const std::string &segment) {
            CURL *curl = curl_easy_init();
            if (curl == nullptr) {
                throw std::runtime_error("curl init error (escape)");
            }

            char *escaped = curl_easy_escape(curl, segment.c_str(), static_cast<int>(segment.size()));
            if (escaped == nullptr) {
                curl_easy_cleanup(curl);
                throw std::runtime_error("Failed to encode folder name");
            }

            std::string result(escaped);
            curl_free(escaped);
            curl_easy_cleanup(curl);
            return result;
        }

        std::string mailboxUrl(const ServerConfig &server, const std::string &folder) {
            return baseUrl(server) + "/" + escapePathSegment(folder);
        }

        void configureCommonImap(CURL *curl, const ServerConfig &server, const MailboxConfig &account) {
            curl_easy_setopt(curl, CURLOPT_USERNAME, account.user.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, account.password.c_str());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

            if (server.tls) {
                curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
            }

            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, server.verify_tls ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, server.verify_tls ? 2L : 0L);
        }

        std::vector<uint64_t> parseSearchResult(const std::string &response) {
            std::vector<uint64_t> result;
            std::istringstream in(response);
            std::string line;

            while (std::getline(in, line)) {
                const std::string t = trim(line);
                if (t.starts_with("* SEARCH")) {
                    std::istringstream tokens(t.substr(8));
                    uint64_t uid = 0;
                    while (tokens >> uid) {
                        result.push_back(uid);
                    }
                }
            }

            return result;
        }

        std::optional<MessageMeta> parseFetchMeta(const std::string &response) {
            MessageMeta meta;
            meta.seen = response.find("\\Seen") != std::string::npos;

            std::istringstream in(response);
            std::string line;
            while (std::getline(in, line)) {
                std::string t = trim(line);
                if (t.size() >= 11) {
                    const std::string prefix = toLower(t.substr(0, 11));
                    if (prefix == "message-id:") {
                        meta.message_id = normalizedMessageId(t.substr(11));
                        continue;
                    }
                }
                if (t.size() >= 5 && toLower(t.substr(0, 5)) == "from:" && meta.from.empty()) {
                    meta.from = trim(t.substr(5));
                } else if (t.size() >= 5 && toLower(t.substr(0, 5)) == "date:" && meta.date.empty()) {
                    meta.date = trim(t.substr(5));
                } else if (t.size() >= 8 && toLower(t.substr(0, 8)) == "subject:" && meta.subject.empty()) {
                    meta.subject = trim(t.substr(8));
                }
            }

            return meta;
        }

        std::string escapeImapQuoted(const std::string &value) {
            std::string out;
            out.reserve(value.size() + 8);
            for (char c: value) {
                if (c == '\\' || c == '"') {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            return out;
        }

        std::string curlConnectionInfo(CURL *curl) {
            const char *primary_ip = nullptr;
            long primary_port = 0;
            const char *local_ip = nullptr;
            long local_port = 0;

            std::ostringstream out;

            if (curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &primary_ip) == CURLE_OK && primary_ip != nullptr &&
                *primary_ip != '\0') {
                out << " peer_ip=" << primary_ip;
            }
            if (curl_easy_getinfo(curl, CURLINFO_PRIMARY_PORT, &primary_port) == CURLE_OK && primary_port > 0) {
                out << " peer_port=" << primary_port;
            }
            if (curl_easy_getinfo(curl, CURLINFO_LOCAL_IP, &local_ip) == CURLE_OK && local_ip != nullptr &&
                *local_ip != '\0') {
                out << " local_ip=" << local_ip;
            }
            if (curl_easy_getinfo(curl, CURLINFO_LOCAL_PORT, &local_port) == CURLE_OK && local_port > 0) {
                out << " local_port=" << local_port;
            }

            return out.str();
        }
    } // namespace

    ImapClient::ImapClient(ServerConfig server) :
        server_(std::move(server)) {
    }

    std::string ImapClient::runImapCommand(const MailboxConfig &account, const std::string &folder,
                                           const std::string &command, long timeout_seconds) const {
        CURL *curl = curl_easy_init();
        if (curl == nullptr) {
            throw std::runtime_error("curl init error");
        }

        Buffer response;
        const std::string url = mailboxUrl(server_, folder);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        configureCommonImap(curl, server_, account);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, command.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        const CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            const std::string err = curl_easy_strerror(rc);
            const std::string connection_info = curlConnectionInfo(curl);
            curl_easy_cleanup(curl);
            throw std::runtime_error(
                    "IMAP command failed"
                    " [host=" + server_.host +
                    ", user=" + account.user +
                    ", folder=" + folder +
                    ", command=\"" + command + "\"]"
                    " (timeout_s=" + std::to_string(timeout_seconds) + ", " + err + ")" + connection_info);
        }

        curl_easy_cleanup(curl);
        return response.data;
    }

    std::vector<uint64_t> ImapClient::listAllUids(const MailboxConfig &account, const std::string &folder) const {
        const std::string response = runImapCommand(account, folder, "UID SEARCH ALL", 120);
        return parseSearchResult(response);
    }

    std::optional<MessageMeta> ImapClient::fetchMetaByUid(const MailboxConfig &account, const std::string &folder,
                                                          uint64_t uid) const {
        try {
            const std::string response = runImapCommand(
                    account, folder,
                    "UID FETCH " + std::to_string(uid) +
                    " (FLAGS BODY.PEEK[HEADER.FIELDS (MESSAGE-ID FROM DATE SUBJECT)])",
                    30);
            MessageMeta meta = parseFetchMeta(response).value_or(MessageMeta{});
            if (meta.message_id.empty()) {
                // Fallback for servers that do not reliably return HEADER.FIELDS subset.
                const std::string full_header_response =
                        runImapCommand(account, folder,
                                       "UID FETCH " + std::to_string(uid) + " (FLAGS BODY.PEEK[HEADER])", 30);
                const MessageMeta fallback_meta = parseFetchMeta(full_header_response).value_or(MessageMeta{});

                if (!fallback_meta.message_id.empty()) {
                    meta.message_id = fallback_meta.message_id;
                }
                if (meta.from.empty()) {
                    meta.from = fallback_meta.from;
                }
                if (meta.date.empty()) {
                    meta.date = fallback_meta.date;
                }
                if (meta.subject.empty()) {
                    meta.subject = fallback_meta.subject;
                }
                meta.seen = meta.seen || fallback_meta.seen;
            }

            return meta;
        } catch (const std::exception &ex) {
            std::cerr << "[WARN] UID=" << uid << " failed to read metadata: " << ex.what() << "\n";
            return std::nullopt;
        }
    }

    std::vector<char> ImapClient::downloadMessageByUid(const MailboxConfig &account, const std::string &folder,
                                                       uint64_t uid) const {
        CURL *curl = curl_easy_init();
        if (curl == nullptr) {
            throw std::runtime_error("curl init error");
        }

        Buffer response;
        const std::string url = mailboxUrl(server_, folder) + "/;UID=" + std::to_string(uid);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        configureCommonImap(curl, server_, account);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        const CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            const std::string err = curl_easy_strerror(rc);
            const std::string connection_info = curlConnectionInfo(curl);
            curl_easy_cleanup(curl);
            throw std::runtime_error("Failed to download message UID=" + std::to_string(uid) + " (" + err + ")" +
                                     connection_info);
        }

        curl_easy_cleanup(curl);
        return {response.data.begin(), response.data.end()};
    }

    bool ImapClient::appendMessage(const MailboxConfig &account, const std::string &folder,
                                   const std::vector<char> &message) const {
        CURL *curl = curl_easy_init();
        if (curl == nullptr) {
            throw std::runtime_error("curl init error");
        }

        UploadPayload payload;
        payload.bytes = &message;

        Buffer response;
        const std::string url = mailboxUrl(server_, folder);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        configureCommonImap(curl, server_, account);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromBuffer);
        curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(message.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        const CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            const std::string connection_info = curlConnectionInfo(curl);
            std::cerr << "[ERROR] APPEND failed: " << curl_easy_strerror(rc) << connection_info << "\n";
            curl_easy_cleanup(curl);
            return false;
        }

        curl_easy_cleanup(curl);
        return true;
    }

    bool ImapClient::deleteSourceMessage(const MailboxConfig &source, uint64_t uid) const {
        try {
            (void) runImapCommand(source, source.folder,
                                  "UID STORE " + std::to_string(uid) + " +FLAGS.SILENT (\\Deleted)");
            (void) runImapCommand(source, source.folder, "UID EXPUNGE " + std::to_string(uid));
            return true;
        } catch (const std::exception &ex) {
            std::cerr << "[WARN] UID=" << uid
                    << " was copied but could not be deleted from source (UID EXPUNGE may not be supported): "
                    << ex.what() << "\n";
            return false;
        }
    }

    bool ImapClient::clearSeenByUid(const MailboxConfig &account, uint64_t uid) const {
        try {
            (void) runImapCommand(account, account.folder,
                                  "UID STORE " + std::to_string(uid) + " -FLAGS.SILENT (\\Seen)");
            return true;
        } catch (const std::exception &ex) {
            std::cerr << "[WARN] UID=" << uid << " could not clear Seen flag: " << ex.what() << "\n";
            return false;
        }
    }

    bool ImapClient::clearSeenByMessageId(const MailboxConfig &account, const std::string &message_id) const {
        if (message_id.empty()) {
            return false;
        }

        try {
            const std::string search_command = "UID SEARCH HEADER MESSAGE-ID \"" + escapeImapQuoted(message_id) + "\"";

            // Some servers need a short delay before freshly appended mails become searchable by header.
            for (int attempt = 0; attempt < 5; ++attempt) {
                const std::string search_response = runImapCommand(account, account.folder, search_command);
                const std::vector<uint64_t> uids = parseSearchResult(search_response);
                if (uids.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                    continue;
                }

                bool any_ok = false;
                for (uint64_t uid: uids) {
                    const std::string store_command =
                            "UID STORE " + std::to_string(uid) + " -FLAGS.SILENT (\\Seen)";
                    (void) runImapCommand(account, account.folder, store_command);
                    any_ok = true;
                }
                if (any_ok) {
                    return true;
                }
            }
            return false;
        } catch (const std::exception &ex) {
            std::cerr << "[WARN] Message-ID=" << message_id << " could not clear Seen flag: " << ex.what() << "\n";
            return false;
        }
    }

    bool ImapClient::destinationHasMessageId(const MailboxConfig &dest,
                                             const std::unordered_set<std::string> &known_ids,
                                             const std::string &message_id) const {
        if (message_id.empty()) {
            return false;
        }

        if (known_ids.contains(message_id)) {
            return true;
        }

        try {
            const std::string command = "UID SEARCH HEADER MESSAGE-ID \"" + escapeImapQuoted(message_id) + "\"";
            const std::string response = runImapCommand(dest, dest.folder, command);
            const std::vector<uint64_t> found = parseSearchResult(response);
            return !found.empty();
        } catch (...) {
            return false;
        }
    }
} // namespace imap_copy
