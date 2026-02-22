# imap-copy

`imap-copy` is a C++20 command line tool that copies messages from one IMAP folder to another.

High-level behavior:
- Lists all UIDs in the source mailbox (`from`).
- Builds a `Message-ID` set from the destination mailbox (`to`) for duplicate checks.
- Copies messages that exist in source but not in destination.
- If `--delete` is enabled, removes successfully copied messages from source.

## Features

- TOML-based configuration
- TLS IMAP support (`imaps://` / `imap://`)
- Duplicate prevention via `Message-ID`
- Parallel copy workers
- Warning logs for `Seen` messages
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

## Config lookup order

If `--config` is not provided:
1. `./imap-copy.toml`
2. `$HOME/.local/imap-copy.toml`

## Logs and output

During execution, the tool logs:
- Server / source / destination details
- Destination scan progress
- IMAP command context and connection details on errors

Final summary includes:
- Source total
- Already in destination
- Copied
- Deleted
- Errors
- Elapsed seconds

## Important behavior notes

- Duplicate checks are based on `Message-ID`.
- IMAP `UID` is mailbox-local, so it is not used alone for cross-mailbox dedupe.
- Message copy downloads full raw RFC822 content (body + attachments).

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
