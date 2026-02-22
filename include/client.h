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

        std::vector<uint64_t> listAllUids(const MailboxConfig &account, const std::string &folder) const;
        std::optional<MessageMeta> fetchMetaByUid(const MailboxConfig &account, const std::string &folder, uint64_t uid) const;
        std::vector<char> downloadMessageByUid(const MailboxConfig &account, const std::string &folder, uint64_t uid) const;
        bool appendMessage(const MailboxConfig &account, const std::string &folder, const std::vector<char> &message) const;
        bool deleteSourceMessage(const MailboxConfig &source, uint64_t uid) const;
        bool clearSeenByUid(const MailboxConfig &account, uint64_t uid) const;
        bool clearSeenByMessageId(const MailboxConfig &account, const std::string &message_id) const;

        bool destinationHasMessageId(const MailboxConfig &dest,
                                     const std::unordered_set<std::string> &known_ids,
                                     const std::string &message_id) const;

    private:
        ServerConfig server_;

        std::string runImapCommand(const MailboxConfig &account, const std::string &folder,
                                   const std::string &command,
                                   long timeout_seconds = 300) const;
    };

} // namespace imap_copy
