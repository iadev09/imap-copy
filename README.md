# imap-copy

`imap-copy` is a C++20 command line tool that copies messages from one IMAP folder to another.

High-level behavior:
- Lists all UIDs in the source mailbox (`from`).
- Uses a local UID cache to skip already copied source messages.
- Copies source messages that are not present in the cache.
- If `--delete` is enabled, removes successfully copied messages from source.

## Features

- TOML-based configuration
- TLS IMAP support (`imaps://` / `imap://`)
- Duplicate prevention via source UID cache (`$HOME/.cache/imap-copy/uids.cache`)
- Cache lock file to prevent concurrent runs (`$HOME/.cache/imap-copy/uids.lock`)
- Parallel copy workers (max 5)
- Destination unseen enforcement after append
- Linux-focused build/deploy scripts

## Requirements

- CMake (>= 3.16)
- C++20 compiler (clang or gcc)
- libcurl (for IMAP + TLS)

## Build

### Local build (macOS/Linux)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Output binary:
- `build/imap-copy`

### Linux x64 artifact with Docker

```bash
./scripts/docker-linux.sh --no-run
```

Output binary:
- `dist/imap-copy`

## Configuration

Use a TOML file:

```toml
[server]
host = "mail.example.com"
port = 993
tls = true
verify_tls = true

[from]
user = "from@example.com"
password = "from-password"
folder = "INBOX"

[to]
user = "to@example.com"
password = "to-password"
folder = "INBOX"
```

Notes:
- Set `host` to a plain hostname/domain (example: `mail.example.com`). Do not use `tls://`.
- `tls = true` uses `imaps://`.
- UTF-8 folder names are supported (for example `INBOX.YONETIM` or `.YONETIM`, depending on server layout).

## Run

```bash
./build/imap-copy --config ./imap-copy.toml
```

With source delete after successful copy:

```bash
./build/imap-copy --config ./imap-copy.toml --delete
```

Short flags:
- `-c` => `--config`
- `-d` => `--delete`
- `-h` => `--help`

Optional environment variables:
- `IMAP_COPY_LOG_LEVEL=DEBUG` enables debug logs.
- `IMAP_COPY_WORKERS=<n>` sets worker count, capped at `5`.

## Config lookup order

If `--config` is not provided:
1. `./imap-copy.toml`
2. `$HOME/.local/imap-copy.toml`

## Logs and output

During execution, the tool logs:
- Server / source / destination details
- Cache path and current cache entry count
- IMAP command context and connection details on errors

Final summary includes:
- Source total
- Already in destination (cache skip count)
- Copied
- Deleted
- Errors
- Elapsed seconds

## Important behavior notes

- Duplicate checks are based on source UID cache.
- Cache key is raw source UID (string form).
- Cache file is global for this app path: `$HOME/.cache/imap-copy/uids.cache`.
- If cache open/lock fails, the program exits with a fatal error.
- Message copy downloads full raw RFC822 content (body + attachments).
- After copy, destination message is forced to `UNSEEN` (with fallback retries).
- Source side is only modified when `--delete` is enabled.

## Linux deploy

Install locally to `/usr/local/bin`:

```bash
./scripts/deploy.sh
```

Deploy to remote host:

```bash
./scripts/deploy.sh --host user@server
```

## License

This project is licensed under MIT. See `LICENSE` for details.
