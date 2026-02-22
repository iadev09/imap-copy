#!/usr/bin/env bash
set -euo pipefail

NAME="imap-copy"
INSTALL_DIR="/usr/local/bin"
HOST=""
BINARY=""
LOCAL_HOSTNAME="$(hostname -s)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="build-release"
BUILD_ENABLED=1
BUILD_MODE="docker-linux-x64"
DOCKER_PLATFORM="linux/amd64"
DOCKER_OUT_DIR="dist"
FORCE_BUILD=0

usage() {
  cat <<'USAGE'
Usage:
  ./scripts/deploy.sh [--host [user@]server] [--install-dir DIR] [--no-build] [--native-build] [--force-build]
  ./scripts/deploy.sh [--binary PATH] [--host [user@]server] [--install-dir DIR] [--no-build] [--native-build] [--force-build]

Examples:
  ./scripts/deploy.sh
  ./scripts/deploy.sh --host root@mail01
  ./scripts/deploy.sh --host
  ./scripts/deploy.sh --binary ./build-release/imap-copy --host mail01
  ./scripts/deploy.sh --native-build --host root@mail01
  ./scripts/deploy.sh --force-build --host root@mail01
USAGE
}

cmake_compiler_args=()
if [[ "$(uname -s)" == "Darwin" ]]; then
  cmake_compiler_args+=(
    -DCMAKE_C_COMPILER=/usr/bin/clang
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
  )
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--binary requires a path value" >&2
        exit 1
      fi
      BINARY="${2:-}"
      shift 2
      ;;
    --host)
      if [[ $# -ge 2 && "${2:-}" != --* ]]; then
        HOST="$2"
        shift 2
      else
        HOST="${LOCAL_HOSTNAME}"
        shift 1
      fi
      ;;
    --install-dir)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--install-dir requires a directory value" >&2
        exit 1
      fi
      INSTALL_DIR="${2:-}"
      shift 2
      ;;
    --no-build)
      BUILD_ENABLED=0
      shift 1
      ;;
    --native-build)
      BUILD_MODE="native"
      shift 1
      ;;
    --force-build)
      FORCE_BUILD=1
      shift 1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$BINARY" ]]; then
  if [[ "$BUILD_ENABLED" -eq 1 ]]; then
    if [[ "$BUILD_MODE" == "docker-linux-x64" ]]; then
      BINARY="$PROJECT_ROOT/$DOCKER_OUT_DIR/$NAME"
      if [[ -x "$BINARY" && "$FORCE_BUILD" -eq 0 ]]; then
        echo "Using existing Linux artifact: $BINARY"
      else
        echo "Building ${NAME} for ${DOCKER_PLATFORM} with Docker..."
        "$PROJECT_ROOT/scripts/docker-linux-test.sh" \
          --platform "$DOCKER_PLATFORM" \
          --out-dir "$DOCKER_OUT_DIR" \
          --no-run
      fi
    else
      echo "Building ${NAME} (Release) ..."
      cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${cmake_compiler_args[@]}"
      cmake --build "$PROJECT_ROOT/$BUILD_DIR" -j
      BINARY="$PROJECT_ROOT/$BUILD_DIR/$NAME"
    fi
  else
    for candidate in \
      "$PROJECT_ROOT/dist/${NAME}" \
      "$PROJECT_ROOT/build-release/${NAME}" \
      "$PROJECT_ROOT/build/${NAME}" \
      "$PROJECT_ROOT/build/Release/${NAME}"
    do
      if [[ -x "$candidate" ]]; then
        BINARY="$candidate"
        break
      fi
    done
  fi
else
  if [[ "$BINARY" != /* ]]; then
    BINARY="$PROJECT_ROOT/$BINARY"
  fi
fi

if [[ -z "$BINARY" ]]; then
  echo "Binary not found. Run without --no-build or pass --binary PATH." >&2
  exit 1
fi

if [[ ! -f "$BINARY" ]]; then
  echo "Binary path does not exist: $BINARY" >&2
  exit 1
fi

if [[ -z "$HOST" ]]; then
  echo "Installing locally with sudo: ${INSTALL_DIR}/${NAME}"
  sudo install -m 0755 "$BINARY" "${INSTALL_DIR}/${NAME}"
  echo "Done: ${INSTALL_DIR}/${NAME}"
else
  TMP_PATH="/tmp/${NAME}.$$"
  echo "Copying to ${HOST}:${TMP_PATH}"
  scp "$BINARY" "${HOST}:${TMP_PATH}"
  echo "Installing on ${HOST} with sudo: ${INSTALL_DIR}/${NAME}"
  ssh "$HOST" "sudo install -m 0755 '${TMP_PATH}' '${INSTALL_DIR}/${NAME}' && rm -f '${TMP_PATH}'"
  echo "Done: ${HOST}:${INSTALL_DIR}/${NAME}"
fi
