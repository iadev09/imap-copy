#include "imap_client.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
    return toLower(message_id);
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
        if (t.rfind("* SEARCH", 0) == 0) {
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
                break;
            }
        }
    }

    return meta;
}

std::string escapeImapQuoted(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

ImapClient::ImapClient(ServerConfig server) : server_(std::move(server)) {}

std::string ImapClient::runImapCommand(const MailboxConfig &account, const std::string &folder,
                                       const std::string &command) const {
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("curl init error");
    }

    Buffer response;
    const std::string url = mailboxUrl(server_, folder);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    configureCommonImap(curl, server_, account);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, command.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        const std::string err = curl_easy_strerror(rc);
        curl_easy_cleanup(curl);
        throw std::runtime_error("IMAP command failed: " + command + " (" + err + ")");
    }

    curl_easy_cleanup(curl);
    return response.data;
}

std::vector<uint64_t> ImapClient::listAllUids(const MailboxConfig &account, const std::string &folder) const {
    const std::string response = runImapCommand(account, folder, "UID SEARCH ALL");
    return parseSearchResult(response);
}

std::optional<MessageMeta> ImapClient::fetchMetaByUid(const MailboxConfig &account, const std::string &folder,
                                                      uint64_t uid) const {
    try {
        const std::string response = runImapCommand(
                account, folder,
                "UID FETCH " + std::to_string(uid) + " (FLAGS BODY.PEEK[HEADER.FIELDS (MESSAGE-ID)])");
        return parseFetchMeta(response);
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
        curl_easy_cleanup(curl);
        throw std::runtime_error("Failed to download message UID=" + std::to_string(uid) + " (" + err + ")");
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
        std::cerr << "[ERROR] APPEND failed: " << curl_easy_strerror(rc) << "\n";
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);
    return true;
}

bool ImapClient::deleteSourceMessage(const MailboxConfig &source, uint64_t uid) const {
    try {
        runImapCommand(source, source.folder,
                       "UID STORE " + std::to_string(uid) + " +FLAGS.SILENT (\\Deleted)");
        runImapCommand(source, source.folder, "UID EXPUNGE " + std::to_string(uid));
        return true;
    } catch (const std::exception &ex) {
        std::cerr << "[WARN] UID=" << uid
                  << " was copied but could not be deleted from source (UID EXPUNGE may not be supported): "
                  << ex.what() << "\n";
        return false;
    }
}

bool ImapClient::destinationHasMessageId(const MailboxConfig &dest,
                                         const std::unordered_set<std::string> &known_ids,
                                         const std::string &message_id) const {
    if (message_id.empty()) {
        return false;
    }

    if (known_ids.find(message_id) != known_ids.end()) {
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

}  // namespace imap_copy
