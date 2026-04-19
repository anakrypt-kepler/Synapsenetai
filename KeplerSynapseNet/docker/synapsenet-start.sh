#!/bin/sh
set -eu

CONF=/data/synapsenet.conf
NAAN_WEB_CONF=/data/naan_agent_web.conf
PRIVACY="${SYNAPSENET_PRIVACY:-false}"
TOR_MODE="${SYNAPSENET_TOR_MODE:-external}"
TOR_REQUIRED="${SYNAPSENET_TOR_REQUIRED:-false}"
ALLOW_CLEARNET_FALLBACK="${SYNAPSENET_ALLOW_CLEARNET_FALLBACK:-true}"
ALLOW_P2P_CLEARNET_FALLBACK="${SYNAPSENET_ALLOW_P2P_CLEARNET_FALLBACK:-true}"
FORCE_CLEARNET_NAAN="${SYNAPSENET_FORCE_CLEARNET_NAAN:-true}"
NAAN_CLEARNET_ENGINES="${SYNAPSENET_NAAN_CLEARNET_ENGINES:-duckduckgo,brave}"
NAAN_AUTO_SEARCH_MODE="${SYNAPSENET_NAAN_AUTO_SEARCH_MODE:-clearnet}"
NAAN_AUTO_SEARCH_QUERIES="${SYNAPSENET_NAAN_AUTO_SEARCH_QUERIES:-latest space engineering research,latest ai research papers,open source systems engineering best practices}"
NAAN_AUTO_SEARCH_MAX_RESULTS="${SYNAPSENET_NAAN_AUTO_SEARCH_MAX_RESULTS:-4}"
NAAN_AUTOMINING="${SYNAPSENET_NAAN_AUTOMINING:-true}"
NAAN_SCORE_INITIAL="${SYNAPSENET_NAAN_SCORE_INITIAL:-200}"
NAAN_SCORE_DECAY_NUM="${SYNAPSENET_NAAN_SCORE_DECAY_NUM:-99}"
NAAN_SCORE_DECAY_DEN="${SYNAPSENET_NAAN_SCORE_DECAY_DEN:-100}"
NAAN_SCORE_ACCEPT_WEIGHT="${SYNAPSENET_NAAN_SCORE_ACCEPT_WEIGHT:-20}"
NAAN_SCORE_REJECT_WEIGHT="${SYNAPSENET_NAAN_SCORE_REJECT_WEIGHT:-6}"
NAAN_SCORE_VIOLATION_WEIGHT="${SYNAPSENET_NAAN_SCORE_VIOLATION_WEIGHT:-4}"
NAAN_SCORE_THROTTLED_BELOW="${SYNAPSENET_NAAN_SCORE_THROTTLED_BELOW:--400}"
NAAN_SCORE_REVIEW_ONLY_BELOW="${SYNAPSENET_NAAN_SCORE_REVIEW_ONLY_BELOW:--700}"
NAAN_SCORE_LOCAL_ONLY_BELOW="${SYNAPSENET_NAAN_SCORE_LOCAL_ONLY_BELOW:--900}"
NAAN_SCORE_LOCAL_RECOVERY_ABOVE="${SYNAPSENET_NAAN_SCORE_LOCAL_RECOVERY_ABOVE:--650}"
NAAN_SCORE_LOCAL_RECOVERY_CLEAN_STEPS="${SYNAPSENET_NAAN_SCORE_LOCAL_RECOVERY_CLEAN_STEPS:-0}"
NAAN_BATCH_THROTTLED="${SYNAPSENET_NAAN_BATCH_THROTTLED:-8}"
NAAN_BATCH_REVIEW_ONLY="${SYNAPSENET_NAAN_BATCH_REVIEW_ONLY:-4}"
NAAN_BATCH_LOCAL_ONLY="${SYNAPSENET_NAAN_BATCH_LOCAL_ONLY:-2}"
NAAN_DRAFT_INTERVAL_NORMAL="${SYNAPSENET_NAAN_DRAFT_INTERVAL_NORMAL:-60}"
NAAN_DRAFT_INTERVAL_THROTTLED="${SYNAPSENET_NAAN_DRAFT_INTERVAL_THROTTLED:-120}"
NAAN_DRAFT_INTERVAL_QUARANTINED="${SYNAPSENET_NAAN_DRAFT_INTERVAL_QUARANTINED:-180}"
NAAN_PIPELINE_INTERVAL_NORMAL="${SYNAPSENET_NAAN_PIPELINE_INTERVAL_NORMAL:-10}"
NAAN_PIPELINE_INTERVAL_THROTTLED="${SYNAPSENET_NAAN_PIPELINE_INTERVAL_THROTTLED:-20}"
NAAN_PIPELINE_INTERVAL_QUARANTINED="${SYNAPSENET_NAAN_PIPELINE_INTERVAL_QUARANTINED:-40}"
NAAN_HEARTBEAT_INTERVAL_NORMAL="${SYNAPSENET_NAAN_HEARTBEAT_INTERVAL_NORMAL:-30}"
NAAN_HEARTBEAT_INTERVAL_THROTTLED="${SYNAPSENET_NAAN_HEARTBEAT_INTERVAL_THROTTLED:-45}"
NAAN_HEARTBEAT_INTERVAL_QUARANTINED="${SYNAPSENET_NAAN_HEARTBEAT_INTERVAL_QUARANTINED:-60}"
NAAN_ABUSE_POLICY_DELTA="${SYNAPSENET_NAAN_ABUSE_POLICY_DELTA:-1000000}"
NAAN_ABUSE_FAILURE_DELTA="${SYNAPSENET_NAAN_ABUSE_FAILURE_DELTA:-1000000}"
NAAN_ABUSE_COOLDOWN_TICKS="${SYNAPSENET_NAAN_ABUSE_COOLDOWN_TICKS:-300}"
NAAN_ABUSE_VIOLATION_STEPS="${SYNAPSENET_NAAN_ABUSE_VIOLATION_STEPS:-1}"
NAAN_DISABLE_DUPLICATE_GATES="${SYNAPSENET_NAAN_DISABLE_DUPLICATE_GATES:-false}"
POE_NOVELTY_BANDS="${SYNAPSENET_POE_NOVELTY_BANDS:-4}"
POE_NOVELTY_MAX_HAMMING="${SYNAPSENET_POE_NOVELTY_MAX_HAMMING:-8}"
POE_MIN_SUBMIT_INTERVAL_SECONDS="${SYNAPSENET_POE_MIN_SUBMIT_INTERVAL_SECONDS:-1}"
POE_AUTO_EPOCH_ENABLED="${SYNAPSENET_POE_AUTO_EPOCH_ENABLED:-true}"
POE_AUTO_EPOCH_INTERVAL_SECONDS="${SYNAPSENET_POE_AUTO_EPOCH_INTERVAL_SECONDS:-120}"
POE_AUTO_EPOCH_REQUIRE_NEW_FINALIZED="${SYNAPSENET_POE_AUTO_EPOCH_REQUIRE_NEW_FINALIZED:-false}"
POE_VALIDATORS_MAJORITY="${SYNAPSENET_POE_VALIDATORS_MAJORITY:-true}"
POE_VALIDATORS_ADAPTIVE="${SYNAPSENET_POE_VALIDATORS_ADAPTIVE:-true}"
POE_VALIDATORS_N="${SYNAPSENET_POE_VALIDATORS_N:-0}"
POE_ALLOW_SELF_BOOTSTRAP="${SYNAPSENET_POE_ALLOW_SELF_BOOTSTRAP:-false}"
POE_SELF_BOOTSTRAP_AUTO_DISABLE="${SYNAPSENET_POE_SELF_BOOTSTRAP_AUTO_DISABLE:-true}"
POE_SELF_BOOTSTRAP_STRICT_CONNECTED_PEERS="${SYNAPSENET_POE_SELF_BOOTSTRAP_STRICT_CONNECTED_PEERS:-4}"
POE_SELF_BOOTSTRAP_STRICT_KNOWN_PEERS="${SYNAPSENET_POE_SELF_BOOTSTRAP_STRICT_KNOWN_PEERS:-8}"
POE_SELF_BOOTSTRAP_STRICT_VALIDATOR_COUNT="${SYNAPSENET_POE_SELF_BOOTSTRAP_STRICT_VALIDATOR_COUNT:-3}"
POE_SELF_BOOTSTRAP_ACTIVATION_CHECKS="${SYNAPSENET_POE_SELF_BOOTSTRAP_ACTIVATION_CHECKS:-5}"
POE_SELF_BOOTSTRAP_FORCE_ALLOW_UNTIL="${SYNAPSENET_POE_SELF_BOOTSTRAP_FORCE_ALLOW_UNTIL:-0}"
FORCE_DAEMON="${SYNAPSENET_DAEMON:-false}"
FORCE_DAEMON_IF_NO_TTY="${SYNAPSENET_FORCE_DAEMON_IF_NO_TTY:-false}"
ADDNODE="${SYNAPSENET_ADDNODE:-}"
TOR_HOST="${SYNAPSENET_TOR_HOST:-tor}"
TOR_SOCKS_PORT="${SYNAPSENET_TOR_SOCKS_PORT:-9050}"
TOR_CONTROL_PORT="${SYNAPSENET_TOR_CONTROL_PORT:-9051}"

mkdir -p /data
touch "$CONF"

sed -i \
  -e '/^agent\.tor\.mode=/d' \
  -e '/^agent\.tor\.required=/d' \
  -e '/^agent\.tor\.socks_host=/d' \
  -e '/^agent\.tor\.socks_port=/d' \
  -e '/^tor\.socks\.host=/d' \
  -e '/^tor\.socks\.port=/d' \
  -e '/^tor\.control\.port=/d' \
  -e '/^agent\.routing\.allow_clearnet_fallback=/d' \
  -e '/^agent\.routing\.allow_p2p_clearnet_fallback=/d' \
  -e '/^web\.inject\.tor_clearnet=/d' \
  -e '/^naan\.score\.initial=/d' \
  -e '/^naan\.score\.decay_numerator=/d' \
  -e '/^naan\.score\.decay_denominator=/d' \
  -e '/^naan\.score\.accept_weight=/d' \
  -e '/^naan\.score\.reject_weight=/d' \
  -e '/^naan\.score\.violation_weight=/d' \
  -e '/^naan\.score\.band\.throttled_below_or_equal=/d' \
  -e '/^naan\.score\.band\.review_only_below_or_equal=/d' \
  -e '/^naan\.score\.band\.local_draft_only_below_or_equal=/d' \
  -e '/^naan\.score\.band\.local_draft_recovery_above=/d' \
  -e '/^naan\.score\.band\.local_draft_recovery_clean_steps=/d' \
  -e '/^naan\.score\.batch_limit\.throttled=/d' \
  -e '/^naan\.score\.batch_limit\.review_only=/d' \
  -e '/^naan\.score\.batch_limit\.local_draft_only=/d' \
  -e '/^naan\.scheduler\.adaptive\.draft_interval_normal_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.draft_interval_throttled_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.draft_interval_quarantined_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.pipeline_interval_normal_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.pipeline_interval_throttled_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.pipeline_interval_quarantined_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.heartbeat_interval_normal_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.heartbeat_interval_throttled_seconds=/d' \
  -e '/^naan\.scheduler\.adaptive\.heartbeat_interval_quarantined_seconds=/d' \
  -e '/^naan\.connector_abuse\.policy_block_delta_threshold=/d' \
  -e '/^naan\.connector_abuse\.failure_delta_threshold=/d' \
  -e '/^naan\.connector_abuse\.cooldown_ticks=/d' \
  -e '/^naan\.connector_abuse\.violation_penalty_steps=/d' \
  -e '/^naan\.duplicate_gates\.content_id=/d' \
  -e '/^naan\.duplicate_gates\.novelty_buckets=/d' \
  -e '/^naan\.duplicate_gates\.citation_graph=/d' \
  -e '/^poe\.novelty_bands=/d' \
  -e '/^poe\.novelty_max_hamming=/d' \
  -e '/^poe\.min_submit_interval_seconds=/d' \
  -e '/^poe\.epoch\.auto_enabled=/d' \
  -e '/^poe\.epoch\.auto_interval_seconds=/d' \
  -e '/^poe\.epoch\.auto_require_new_finalized=/d' \
  -e '/^poe\.validators_majority=/d' \
  -e '/^poe\.validators_adaptive=/d' \
  -e '/^poe\.validators_n=/d' \
  -e '/^poe\.allow_self_validator_bootstrap=/d' \
  -e '/^poe\.self_validator_bootstrap_auto_disable=/d' \
  -e '/^poe\.self_validator_bootstrap_strict_connected_peers=/d' \
  -e '/^poe\.self_validator_bootstrap_strict_known_peers=/d' \
  -e '/^poe\.self_validator_bootstrap_strict_validator_count=/d' \
  -e '/^poe\.self_validator_bootstrap_activation_checks=/d' \
  -e '/^poe\.self_validator_bootstrap_force_allow_until=/d' \
  "$CONF"

{
  echo "agent.tor.mode=${TOR_MODE}"
  echo "agent.tor.required=${TOR_REQUIRED}"
  echo "agent.tor.socks_host=${TOR_HOST}"
  echo "agent.tor.socks_port=${TOR_SOCKS_PORT}"
  echo "tor.socks.host=${TOR_HOST}"
  echo "tor.socks.port=${TOR_SOCKS_PORT}"
  echo "tor.control.port=${TOR_CONTROL_PORT}"
  echo "agent.routing.allow_clearnet_fallback=${ALLOW_CLEARNET_FALLBACK}"
  echo "agent.routing.allow_p2p_clearnet_fallback=${ALLOW_P2P_CLEARNET_FALLBACK}"
  echo "web.inject.tor_clearnet=false"
  echo "poe.epoch.auto_enabled=${POE_AUTO_EPOCH_ENABLED}"
  echo "poe.epoch.auto_interval_seconds=${POE_AUTO_EPOCH_INTERVAL_SECONDS}"
  echo "poe.epoch.auto_require_new_finalized=${POE_AUTO_EPOCH_REQUIRE_NEW_FINALIZED}"
  echo "poe.validators_majority=${POE_VALIDATORS_MAJORITY}"
  echo "poe.validators_adaptive=${POE_VALIDATORS_ADAPTIVE}"
  echo "poe.validators_n=${POE_VALIDATORS_N}"
  echo "poe.allow_self_validator_bootstrap=${POE_ALLOW_SELF_BOOTSTRAP}"
  echo "poe.self_validator_bootstrap_auto_disable=${POE_SELF_BOOTSTRAP_AUTO_DISABLE}"
  echo "poe.self_validator_bootstrap_strict_connected_peers=${POE_SELF_BOOTSTRAP_STRICT_CONNECTED_PEERS}"
  echo "poe.self_validator_bootstrap_strict_known_peers=${POE_SELF_BOOTSTRAP_STRICT_KNOWN_PEERS}"
  echo "poe.self_validator_bootstrap_strict_validator_count=${POE_SELF_BOOTSTRAP_STRICT_VALIDATOR_COUNT}"
  echo "poe.self_validator_bootstrap_activation_checks=${POE_SELF_BOOTSTRAP_ACTIVATION_CHECKS}"
  echo "poe.self_validator_bootstrap_force_allow_until=${POE_SELF_BOOTSTRAP_FORCE_ALLOW_UNTIL}"
  if [ "${NAAN_AUTOMINING}" = "true" ]; then
    echo "naan.score.initial=${NAAN_SCORE_INITIAL}"
    echo "naan.score.decay_numerator=${NAAN_SCORE_DECAY_NUM}"
    echo "naan.score.decay_denominator=${NAAN_SCORE_DECAY_DEN}"
    echo "naan.score.accept_weight=${NAAN_SCORE_ACCEPT_WEIGHT}"
    echo "naan.score.reject_weight=${NAAN_SCORE_REJECT_WEIGHT}"
    echo "naan.score.violation_weight=${NAAN_SCORE_VIOLATION_WEIGHT}"
    echo "naan.score.band.throttled_below_or_equal=${NAAN_SCORE_THROTTLED_BELOW}"
    echo "naan.score.band.review_only_below_or_equal=${NAAN_SCORE_REVIEW_ONLY_BELOW}"
    echo "naan.score.band.local_draft_only_below_or_equal=${NAAN_SCORE_LOCAL_ONLY_BELOW}"
    echo "naan.score.band.local_draft_recovery_above=${NAAN_SCORE_LOCAL_RECOVERY_ABOVE}"
    echo "naan.score.band.local_draft_recovery_clean_steps=${NAAN_SCORE_LOCAL_RECOVERY_CLEAN_STEPS}"
    echo "naan.score.batch_limit.throttled=${NAAN_BATCH_THROTTLED}"
    echo "naan.score.batch_limit.review_only=${NAAN_BATCH_REVIEW_ONLY}"
    echo "naan.score.batch_limit.local_draft_only=${NAAN_BATCH_LOCAL_ONLY}"
    echo "naan.scheduler.adaptive.draft_interval_normal_seconds=${NAAN_DRAFT_INTERVAL_NORMAL}"
    echo "naan.scheduler.adaptive.draft_interval_throttled_seconds=${NAAN_DRAFT_INTERVAL_THROTTLED}"
    echo "naan.scheduler.adaptive.draft_interval_quarantined_seconds=${NAAN_DRAFT_INTERVAL_QUARANTINED}"
    echo "naan.scheduler.adaptive.pipeline_interval_normal_seconds=${NAAN_PIPELINE_INTERVAL_NORMAL}"
    echo "naan.scheduler.adaptive.pipeline_interval_throttled_seconds=${NAAN_PIPELINE_INTERVAL_THROTTLED}"
    echo "naan.scheduler.adaptive.pipeline_interval_quarantined_seconds=${NAAN_PIPELINE_INTERVAL_QUARANTINED}"
    echo "naan.scheduler.adaptive.heartbeat_interval_normal_seconds=${NAAN_HEARTBEAT_INTERVAL_NORMAL}"
    echo "naan.scheduler.adaptive.heartbeat_interval_throttled_seconds=${NAAN_HEARTBEAT_INTERVAL_THROTTLED}"
    echo "naan.scheduler.adaptive.heartbeat_interval_quarantined_seconds=${NAAN_HEARTBEAT_INTERVAL_QUARANTINED}"
    echo "naan.connector_abuse.policy_block_delta_threshold=${NAAN_ABUSE_POLICY_DELTA}"
    echo "naan.connector_abuse.failure_delta_threshold=${NAAN_ABUSE_FAILURE_DELTA}"
    echo "naan.connector_abuse.cooldown_ticks=${NAAN_ABUSE_COOLDOWN_TICKS}"
    echo "naan.connector_abuse.violation_penalty_steps=${NAAN_ABUSE_VIOLATION_STEPS}"
    if [ "${NAAN_DISABLE_DUPLICATE_GATES}" = "true" ]; then
      echo "naan.duplicate_gates.content_id=false"
      echo "naan.duplicate_gates.novelty_buckets=false"
      echo "naan.duplicate_gates.citation_graph=false"
    fi
    echo "poe.novelty_bands=${POE_NOVELTY_BANDS}"
    echo "poe.novelty_max_hamming=${POE_NOVELTY_MAX_HAMMING}"
    echo "poe.min_submit_interval_seconds=${POE_MIN_SUBMIT_INTERVAL_SECONDS}"
  fi
} >> "$CONF"

if [ "${FORCE_CLEARNET_NAAN}" = "true" ]; then
  touch "$NAAN_WEB_CONF"
  sed -i \
    -e '/^clearnet_engines=/d' \
    -e '/^route_clearnet_through_tor=/d' \
    -e '/^naan_force_tor_mode=/d' \
    -e '/^naan_auto_search_enabled=/d' \
    -e '/^naan_auto_search_mode=/d' \
    -e '/^naan_auto_search_queries=/d' \
    -e '/^naan_auto_search_max_results=/d' \
    "$NAAN_WEB_CONF"
  {
    echo "clearnet_engines=${NAAN_CLEARNET_ENGINES}"
    echo "route_clearnet_through_tor=false"
    echo "naan_force_tor_mode=false"
    if [ "${NAAN_AUTOMINING}" = "true" ]; then
      echo "naan_auto_search_enabled=true"
      echo "naan_auto_search_mode=${NAAN_AUTO_SEARCH_MODE}"
      echo "naan_auto_search_queries=${NAAN_AUTO_SEARCH_QUERIES}"
      echo "naan_auto_search_max_results=${NAAN_AUTO_SEARCH_MAX_RESULTS}"
    else
      echo "naan_auto_search_enabled=false"
    fi
  } >> "$NAAN_WEB_CONF"
fi

set -- /app/synapsed -D /data
if [ "${PRIVACY}" = "true" ]; then
  set -- "$@" --privacy
fi
if [ -n "$ADDNODE" ]; then
  set -- "$@" --addnode "$ADDNODE"
fi
if [ "${FORCE_DAEMON}" = "true" ]; then
  set -- "$@" -d
elif [ "${FORCE_DAEMON_IF_NO_TTY}" = "true" ] && [ ! -t 0 ]; then
  set -- "$@" -d
fi

exec "$@"
