#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CANONICAL_DIR="${ROOT_DIR}/KeplerSynapseNet/interfaces txt"
ROOT_MIRROR_DIR="${ROOT_DIR}/interfaces txt"
PROJECT_MAIN_DIR="${ROOT_DIR}/KeplerSynapseNet"

if [ ! -d "${CANONICAL_DIR}" ]; then
  echo "Missing canonical interfaces directory: ${CANONICAL_DIR}" >&2
  exit 1
fi

mkdir -p "${ROOT_MIRROR_DIR}"

while IFS= read -r FILE_PATH; do
  BASE_NAME="$(basename "${FILE_PATH}")"
  if [ ! -f "${CANONICAL_DIR}/${BASE_NAME}" ]; then
    rm -f "${FILE_PATH}"
  fi
done < <(find "${ROOT_MIRROR_DIR}" -maxdepth 1 -type f -print)

while IFS= read -r FILE_PATH; do
  BASE_NAME="$(basename "${FILE_PATH}")"
  cp "${FILE_PATH}" "${ROOT_MIRROR_DIR}/${BASE_NAME}"
done < <(find "${CANONICAL_DIR}" -maxdepth 1 -type f -print)

cp "${CANONICAL_DIR}/main_interface.txt" "${PROJECT_MAIN_DIR}/main_interface.txt"
cp "${CANONICAL_DIR}/WHAT_IS_SYNAPSENET.txt" "${PROJECT_MAIN_DIR}/WHAT_IS_SYNAPSENET.txt"

echo "Interface mirrors synchronized from KeplerSynapseNet/interfaces txt."
