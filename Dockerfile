# syntax=docker/dockerfile:1.7

FROM debian:trixie-slim AS build

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        libcurl4-openssl-dev \
        ninja-build \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target imap_copy -j"$(nproc)" \
    && strip /build/imap-copy || true

FROM debian:trixie-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libcurl4 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /build/imap-copy /usr/local/bin/imap-copy
ENTRYPOINT ["/usr/local/bin/imap-copy"]

FROM scratch AS artifact
COPY --from=build /build/imap-copy /imap-copy
