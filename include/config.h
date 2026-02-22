#pragma once

#include <string>

#include "types.h"

namespace imap_copy {

AppConfig parseConfig(const std::string &path);
CliOptions parseArgs(int argc, char **argv);
void printUsage(const char *argv0);

}  // namespace imap_copy
