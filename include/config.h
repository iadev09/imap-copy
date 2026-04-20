#pragma once

#include <string>
#include "types.h"

namespace imap_copy {

    auto parseConfig(const std::string &path) -> AppConfig;
    auto parseArgs(int argc, char **argv) -> CliOptions;
    void printUsage(const char *argv0);

} // namespace imap_copy
