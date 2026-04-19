#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CANONICAL_DIR="${ROOT_DIR}/interfaces txt"
ROOT_MIRROR_DIR="${ROOT_DIR}/KeplerSynapseNet/interfaces txt"
PROJECT_MAIN_DIR="${ROOT_DIR}/KeplerSynapseNet"

if [ ! -d "${CANONICAL_DIR}" ]; then
  echo "Missing canonical interfaces directory: ${CANONICAL_DIR}" >&2
  exit 1
fi
if [ ! -d "${ROOT_MIRROR_DIR}" ]; then
  echo "Missing root mirror interfaces directory: ${ROOT_MIRROR_DIR}" >&2
  exit 1
fi

CANONICAL_LIST="$(cd "${CANONICAL_DIR}" && find . -maxdepth 1 -type f -name '*.txt' -print | sed 's#^./##' | LC_ALL=C sort)"
ROOT_MIRROR_LIST="$(cd "${ROOT_MIRROR_DIR}" && find . -maxdepth 1 -type f -name '*.txt' -print | sed 's#^./##' | LC_ALL=C sort)"

if [ "${CANONICAL_LIST}" != "${ROOT_MIRROR_LIST}" ]; then
  echo "Interface file list mismatch between canonical and root mirror." >&2
  diff -u <(printf '%s\n' "${CANONICAL_LIST}") <(printf '%s\n' "${ROOT_MIRROR_LIST}") || true
  exit 1
fi

STATUS=0
while IFS= read -r REL_PATH; do
  [ -n "${REL_PATH}" ] || continue
  if ! cmp -s "${CANONICAL_DIR}/${REL_PATH}" "${ROOT_MIRROR_DIR}/${REL_PATH}"; then
    echo "Mismatch: ${CANONICAL_DIR}/${REL_PATH} != ${ROOT_MIRROR_DIR}/${REL_PATH}" >&2
    STATUS=1
  fi
done <<< "${CANONICAL_LIST}"

for REL_PATH in main_interface.txt WHAT_IS_SYNAPSENET.txt; do
  if ! cmp -s "${CANONICAL_DIR}/${REL_PATH}" "${PROJECT_MAIN_DIR}/${REL_PATH}"; then
    echo "Mismatch: KeplerSynapseNet/${REL_PATH} != KeplerSynapseNet/interfaces txt/${REL_PATH}" >&2
    STATUS=1
  fi
done

if [ "${STATUS}" -ne 0 ]; then
  exit 1
fi

echo "Interface mirrors are in sync."
