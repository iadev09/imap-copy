#include "config.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace imap_copy {
namespace {

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

bool iequals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

std::string stripComment(const std::string &line) {
    bool in_single = false;
    bool in_double = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
        } else if (ch == '"' && !in_single) {
            in_double = !in_double;
        } else if (ch == '#' && !in_single && !in_double) {
            return line.substr(0, i);
        }
    }

    return line;
}

std::string unquoteTomlString(const std::string &raw) {
    const std::string value = trim(raw);
    if (value.size() < 2) {
        throw std::runtime_error("Invalid TOML string: " + raw);
    }

    const char quote = value.front();
    if ((quote != '"' && quote != '\'') || value.back() != quote) {
        throw std::runtime_error("String values must be quoted: " + raw);
    }

    std::string result;
    result.reserve(value.size() - 2);
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        const char ch = value[i];
        if (quote == '"' && ch == '\\' && i + 1 < value.size() - 1) {
            const char next = value[++i];
            switch (next) {
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case '\\':
                    result.push_back('\\');
                    break;
                case '"':
                    result.push_back('"');
                    break;
                default:
                    result.push_back(next);
                    break;
            }
        } else {
            result.push_back(ch);
        }
    }

    return result;
}

bool parseTomlBool(const std::string &raw) {
    const std::string value = trim(raw);
    if (iequals(value, "true")) {
        return true;
    }
    if (iequals(value, "false")) {
        return false;
    }

    throw std::runtime_error("Expected boolean value: " + raw);
}

int parseTomlInt(const std::string &raw) {
    const std::string value = trim(raw);
    try {
        size_t used = 0;
        const int number = std::stoi(value, &used);
        if (used != value.size()) {
            throw std::runtime_error("integer parse");
        }
        return number;
    } catch (...) {
        throw std::runtime_error("Expected integer value: " + raw);
    }
}

void validateConfig(const AppConfig &cfg) {
    if (cfg.server.host.empty()) {
        throw std::runtime_error("[server].host is required");
    }
    if (cfg.server.port <= 0 || cfg.server.port > 65535) {
        throw std::runtime_error("[server].port must be in range 1-65535");
    }

    auto validateMailbox = [](const MailboxConfig &mailbox, const char *name) {
        if (mailbox.user.empty()) {
            throw std::runtime_error(std::string("[") + name + "].user is required");
        }
        if (mailbox.password.empty()) {
            throw std::runtime_error(std::string("[") + name + "].password is required");
        }
        if (mailbox.folder.empty()) {
            throw std::runtime_error(std::string("[") + name + "].folder is required");
        }
    };

    validateMailbox(cfg.from, "from");
    validateMailbox(cfg.to, "to");
}

}  // namespace

AppConfig parseConfig(const std::string &path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    AppConfig cfg;
    std::string section;
    std::string line;
    size_t line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        const std::string cleaned = trim(stripComment(line));
        if (cleaned.empty()) {
            continue;
        }

        if (cleaned.front() == '[' && cleaned.back() == ']') {
            section = trim(cleaned.substr(1, cleaned.size() - 2));
            continue;
        }

        const size_t eq_pos = cleaned.find('=');
        if (eq_pos == std::string::npos) {
            throw std::runtime_error("Invalid line (" + std::to_string(line_no) + "): " + cleaned);
        }

        const std::string key = trim(cleaned.substr(0, eq_pos));
        const std::string raw_value = trim(cleaned.substr(eq_pos + 1));

        if (section == "server") {
            if (key == "host") {
                cfg.server.host = unquoteTomlString(raw_value);
            } else if (key == "port") {
                cfg.server.port = parseTomlInt(raw_value);
            } else if (key == "tls") {
                cfg.server.tls = parseTomlBool(raw_value);
            } else if (key == "verify_tls") {
                cfg.server.verify_tls = parseTomlBool(raw_value);
            }
        } else if (section == "from") {
            if (key == "user") {
                cfg.from.user = unquoteTomlString(raw_value);
            } else if (key == "password") {
                cfg.from.password = unquoteTomlString(raw_value);
            } else if (key == "folder") {
                cfg.from.folder = unquoteTomlString(raw_value);
            }
        } else if (section == "to") {
            if (key == "user") {
                cfg.to.user = unquoteTomlString(raw_value);
            } else if (key == "password") {
                cfg.to.password = unquoteTomlString(raw_value);
            } else if (key == "folder") {
                cfg.to.folder = unquoteTomlString(raw_value);
            }
        }
    }

    validateConfig(cfg);
    return cfg;
}

void printUsage(const char *argv0) {
    std::cerr << "Usage: " << argv0 << " [--config <file.toml>] [--delete|-d]\n";
    std::cerr << "Default config lookup: ./imap-copy.toml then $HOME/.local/imap-copy.toml\n";
}

CliOptions parseArgs(int argc, char **argv) {
    CliOptions opts;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--config requires a file path");
            }
            opts.config_path = argv[++i];
        } else if (arg == "--delete" || arg == "-d") {
            opts.delete_after_copy = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (opts.config_path.empty()) {
        namespace fs = std::filesystem;
        const fs::path cwd_candidate = fs::current_path() / "imap-copy.toml";
        if (fs::exists(cwd_candidate) && fs::is_regular_file(cwd_candidate)) {
            opts.config_path = cwd_candidate.string();
        } else {
            const char *home = std::getenv("HOME");
            if (home != nullptr && *home != '\0') {
                const fs::path home_candidate = fs::path(home) / ".local" / "imap-copy.toml";
                if (fs::exists(home_candidate) && fs::is_regular_file(home_candidate)) {
                    opts.config_path = home_candidate.string();
                }
            }
        }
    }

    if (opts.config_path.empty()) {
        throw std::runtime_error(
                "No config file found. Checked ./imap-copy.toml and $HOME/.local/imap-copy.toml");
    }

    return opts;
}

}  // namespace imap_copy
