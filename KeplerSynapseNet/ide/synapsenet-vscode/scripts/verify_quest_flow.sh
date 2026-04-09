#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage:
  $(basename "$0") --repo owner/name --issue N --submit-id HEX64 [options]

Options:
  --workspace PATH         Workspace path (default: current directory)
  --rpc-url URL            RPC URL (default: http://127.0.0.1:8332)
  --min-pow-bits N         Minimum leading-zero PoW bits (default: 16)
  --pr-url URL             Optional PR URL to format-check
  --help                   Show this help
EOF
}

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

pass() {
  echo "PASS: $1"
}

warn() {
  echo "WARN: $1"
}

trim() {
  local s="$1"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

parse_github_repo_from_remote() {
  local raw
  raw="$(trim "$1")"
  raw="${raw%.git}"
  if [[ "$raw" =~ ^git@github\.com:([^/[:space:]]+/[^/[:space:]]+)$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  if [[ "$raw" =~ ^ssh://git@github\.com/([^/[:space:]]+/[^/[:space:]]+)$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  if [[ "$raw" =~ ^https?://github\.com/([^/[:space:]]+/[^/[:space:]]+)$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  return 1
}

leading_zero_bits_hex() {
  local h="${1,,}"
  local bits=0
  local i c
  for ((i=0; i<${#h}; i++)); do
    c="${h:i:1}"
    case "$c" in
      0) bits=$((bits+4)) ;;
      1) bits=$((bits+3)); echo "$bits"; return 0 ;;
      2|3) bits=$((bits+2)); echo "$bits"; return 0 ;;
      4|5|6|7) bits=$((bits+1)); echo "$bits"; return 0 ;;
      8|9|a|b|c|d|e|f) echo "$bits"; return 0 ;;
      *) echo "-1"; return 0 ;;
    esac
  done
  echo "$bits"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

repo=""
issue=""
submit_id=""
workspace="$(pwd)"
rpc_url="http://127.0.0.1:8332"
min_pow_bits="16"
pr_url=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) repo="${2:-}"; shift 2 ;;
    --issue) issue="${2:-}"; shift 2 ;;
    --submit-id) submit_id="${2:-}"; shift 2 ;;
    --workspace) workspace="${2:-}"; shift 2 ;;
    --rpc-url) rpc_url="${2:-}"; shift 2 ;;
    --min-pow-bits) min_pow_bits="${2:-}"; shift 2 ;;
    --pr-url) pr_url="${2:-}"; shift 2 ;;
    --help) usage; exit 0 ;;
    *) fail "unknown argument: $1" ;;
  esac
done

repo="$(trim "$repo")"
issue="$(trim "$issue")"
submit_id="$(trim "$submit_id")"
workspace="$(trim "$workspace")"
rpc_url="$(trim "$rpc_url")"
min_pow_bits="$(trim "$min_pow_bits")"
pr_url="$(trim "$pr_url")"

[[ -n "$repo" ]] || fail "--repo is required"
[[ "$repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || fail "--repo must be owner/name"
[[ -n "$issue" ]] || fail "--issue is required"
[[ "$issue" =~ ^[0-9]+$ ]] || fail "--issue must be numeric"
[[ "$issue" -gt 0 ]] || fail "--issue must be > 0"
[[ -n "$submit_id" ]] || fail "--submit-id is required"
[[ "$submit_id" =~ ^[0-9a-fA-F]{64}$ ]] || fail "--submit-id must be 64 hex chars"
[[ "$min_pow_bits" =~ ^[0-9]+$ ]] || fail "--min-pow-bits must be numeric"
[[ "$min_pow_bits" -ge 1 && "$min_pow_bits" -le 256 ]] || fail "--min-pow-bits must be in [1,256]"

need_cmd git
need_cmd curl
need_cmd jq

[[ -d "$workspace" ]] || fail "workspace does not exist: $workspace"
cd "$workspace"

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "workspace is not a git repository"
pass "workspace is a git repository"

current_branch="$(trim "$(git branch --show-current)")"
expected_branch="quest-$issue"
[[ "$current_branch" == "$expected_branch" ]] || fail "current branch is '$current_branch', expected '$expected_branch'"
pass "branch matches expected quest branch: $expected_branch"

latest_commit="$(git log -1 --pretty=%B)"
echo "$latest_commit" | grep -F "Quest: #$issue" >/dev/null || fail "latest commit does not contain 'Quest: #$issue'"
echo "$latest_commit" | grep -F "PoE CODE submitId: $submit_id" >/dev/null || fail "latest commit does not contain submitId line"
pass "latest commit contains quest and submitId metadata"

pow_bits="$(leading_zero_bits_hex "$submit_id")"
[[ "$pow_bits" -ge 0 ]] || fail "invalid hex in submitId"
[[ "$pow_bits" -ge "$min_pow_bits" ]] || fail "submitId PoW too low: $pow_bits < $min_pow_bits leading-zero bits"
pass "submitId PoW bits are valid: $pow_bits >= $min_pow_bits"

payload="$(jq -cn --arg sid "${submit_id,,}" '{jsonrpc:"2.0",id:1,method:"poe.fetch_code",params:{id:$sid}}')"
rpc_resp="$(curl -sS --fail -H 'Content-Type: application/json' --data "$payload" "$rpc_url")" || fail "rpc call failed for $rpc_url"
rpc_error="$(echo "$rpc_resp" | jq -r '.error.message // empty')"
[[ -z "$rpc_error" ]] || fail "poe.fetch_code rpc error: $rpc_error"
fetched_submit="$(echo "$rpc_resp" | jq -r '.result.submitId // empty')"
[[ -n "$fetched_submit" ]] || fail "poe.fetch_code returned empty submitId"
[[ "${fetched_submit,,}" == "${submit_id,,}" ]] || fail "poe.fetch_code submitId mismatch: expected ${submit_id,,}, got ${fetched_submit,,}"
fetched_finalized="$(echo "$rpc_resp" | jq -r '.result.finalized // false')"
pass "poe.fetch_code resolved submitId (finalized=$fetched_finalized)"

origin_url="$(git remote get-url origin 2>/dev/null || true)"
if [[ -n "$(trim "$origin_url")" ]]; then
  origin_repo="$(parse_github_repo_from_remote "$origin_url" || true)"
  if [[ -n "$origin_repo" ]]; then
    if [[ "${origin_repo,,}" == "${repo,,}" ]]; then
      pass "origin remote repo matches configured quest repo: $repo"
    else
      warn "origin remote repo differs from configured quest repo: $origin_repo vs $repo"
    fi
  else
    warn "origin remote is not a parseable github repo URL: $origin_url"
  fi
fi

if [[ -n "$pr_url" ]]; then
  if [[ ! "$pr_url" =~ ^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/pull/[0-9]+$ ]]; then
    fail "--pr-url format is invalid: $pr_url"
  fi
  pass "pr-url format is valid"
fi

echo "PASS: deterministic quest flow verification completed"
