#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "types.h"

namespace imap_copy
{

    class ImapClient
    {
    public:
        explicit ImapClient(ServerConfig server);

        [[nodiscard]] auto listAllUids(const MailboxConfig &account, const std::string &folder) const -> std::vector<uint64_t>;
        [[nodiscard]] auto fetchMetaByUid(const MailboxConfig &account, const std::string &folder, uint64_t uid) const -> std::optional<MessageMeta>;
        [[nodiscard]] auto downloadMessageByUid(const MailboxConfig &account, const std::string &folder, uint64_t uid) const -> std::vector<char>;
        [[nodiscard]] auto appendMessage(const MailboxConfig &account, const std::string &folder, const std::vector<char> &message) const -> bool;
        [[nodiscard]] auto deleteSourceMessage(const MailboxConfig &source, uint64_t uid) const -> bool;
        [[nodiscard]] auto clearSeenByUid(const MailboxConfig &account, uint64_t uid) const -> bool;
        [[nodiscard]] auto clearSeenByMessageId(const MailboxConfig &account, const std::string &message_id) const -> bool;

        [[nodiscard]] auto destinationHasMessageId(const MailboxConfig &dest,
                                     const std::unordered_set<std::string> &known_ids,
                                     const std::string &message_id) const -> bool;

    private:
        ServerConfig server_;

        [[nodiscard]] auto runImapCommand(const MailboxConfig &account, const std::string &folder,
                                   const std::string &command,
                                   long timeout_seconds = 300) const -> std::string;
    };

} // namespace imap_copy
