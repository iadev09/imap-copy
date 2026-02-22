#!/usr/bin/env bash
set -euo pipefail

NAME="imap-copy"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PLATFORM="linux/amd64"
OUT_DIR="dist"
IMAGE_TAG="imap-copy:trixie-local"
BUILD_ARTIFACT=1
RUN_CONTAINER=1

usage() {
  cat <<'USAGE'
Usage:
  ./scripts/docker-linux.sh [--platform linux/amd64] [--out-dir dist] [--image-tag tag] [--no-run] [--run-arg ARG ...]

Examples:
  ./scripts/docker-linux.sh
  ./scripts/docker-linux.sh --no-run
  ./scripts/docker-linux.sh --platform linux/amd64 --image-tag imap-copy:test
  ./scripts/docker-linux.sh --run-arg --config --run-arg /tmp/config.toml
USAGE
}

RUN_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --platform)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--platform requires a value" >&2
        exit 1
      fi
      PLATFORM="${2:-}"
      shift 2
      ;;
    --out-dir)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--out-dir requires a value" >&2
        exit 1
      fi
      OUT_DIR="${2:-}"
      shift 2
      ;;
    --image-tag)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--image-tag requires a value" >&2
        exit 1
      fi
      IMAGE_TAG="${2:-}"
      shift 2
      ;;
    --no-artifact)
      BUILD_ARTIFACT=0
      shift
      ;;
    --no-run)
      RUN_CONTAINER=0
      shift
      ;;
    --run-arg)
      if [[ $# -lt 2 ]]; then
        echo "--run-arg requires a value" >&2
        exit 1
      fi
      RUN_ARGS+=("$2")
      shift 2
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

if [[ "$BUILD_ARTIFACT" -eq 1 ]]; then
  echo "Building ${NAME} artifact for ${PLATFORM}..."
  docker buildx build \
    --platform "${PLATFORM}" \
    --target artifact \
    -o "type=local,dest=${PROJECT_ROOT}/${OUT_DIR}" \
    "$PROJECT_ROOT"

  if [[ -f "${PROJECT_ROOT}/${OUT_DIR}/${NAME}" ]]; then
    echo "Artifact: ${PROJECT_ROOT}/${OUT_DIR}/${NAME}"
    file "${PROJECT_ROOT}/${OUT_DIR}/${NAME}" || true
  fi
fi

if [[ "$RUN_CONTAINER" -eq 1 ]]; then
  echo "Building runtime image ${IMAGE_TAG} for ${PLATFORM}..."
  docker buildx build \
    --platform "${PLATFORM}" \
    --target runtime \
    --load \
    -t "${IMAGE_TAG}" \
    "$PROJECT_ROOT"

  if [[ ${#RUN_ARGS[@]} -eq 0 ]]; then
    RUN_ARGS=(--help)
  fi

  echo "Running container: docker run --rm --platform ${PLATFORM} ${IMAGE_TAG} ${RUN_ARGS[*]}"
  docker run --rm --platform "${PLATFORM}" "${IMAGE_TAG}" "${RUN_ARGS[@]}"
fi
