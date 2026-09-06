#!/usr/bin/env bash
set -u

usage() {
    echo "Usage: $0 <client|server|km|security-tool> <program> [args ...]" >&2
}

if [ "$#" -lt 2 ]; then
    usage
    exit 2
fi

role=$1
shift
case "$role" in
    client|server|km|security-tool) ;;
    *) usage; exit 2 ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
audit_dir="$script_dir/audit"
audit_log="$audit_dir/security-audit.log"

umask 077
ulimit -c 0 || {
    echo "SECURITY WARNING: cannot disable core dumps." >&2
}

mkdir -p "$script_dir/km" "$script_dir/client" "$script_dir/storage" \
    "$audit_dir"
chmod 700 "$script_dir/km" "$script_dir/client" "$script_dir/storage" \
    "$audit_dir"

while IFS= read -r -d '' key_file; do
    mode=$(stat -c '%a' "$key_file" 2>/dev/null || echo unknown)
    if [ "$mode" != "600" ]; then
        echo "SECURITY WARNING: correcting key permission to 0600: $key_file" >&2
        chmod 600 "$key_file" ||
            echo "SECURITY WARNING: cannot correct key permission: $key_file" >&2
    fi
done < <(find "$script_dir/km" "$script_dir/client" "$script_dir/storage" \
    -maxdepth 1 -type f -name '*.key' -print0 2>/dev/null)

ptrace_scope=unavailable
if [ -r /proc/sys/kernel/yama/ptrace_scope ]; then
    ptrace_scope=$(tr -dc '0-9' < /proc/sys/kernel/yama/ptrace_scope)
    if [ -z "$ptrace_scope" ] || [ "$ptrace_scope" -lt 2 ]; then
        echo "SECURITY WARNING: kernel.yama.ptrace_scope=$ptrace_scope; recommended value is 2." >&2
    fi
else
    echo "SECURITY WARNING: cannot inspect kernel.yama.ptrace_scope." >&2
fi

expected_user=
case "$role" in
    km) expected_user=${EDR_KM_USER:-edr-km} ;;
    server) expected_user=${EDR_SERVER_USER:-edr-server} ;;
    client) expected_user=${EDR_CLIENT_USER:-edr-client} ;;
esac
actual_user=$(id -un)
if [ -n "$expected_user" ] && [ "$actual_user" != "$expected_user" ] &&
   [ "${EDR_SKIP_USER_CHECK:-0}" != "1" ]; then
    echo "SECURITY WARNING: role '$role' is running as '$actual_user'; recommended account is '$expected_user'." >&2
fi

audit_event() {
    local event=$1
    local result=$2
    local code=$3
    local timestamp
    timestamp=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
    printf 'time=%s component=%s event=%s result=%s code=%s uid=%s pid=%s ptrace_scope=%s\n' \
        "$timestamp" "$role" "$event" "$result" "$code" \
        "$(id -u)" "$$" "$ptrace_scope" >> "$audit_log"
    chmod 600 "$audit_log"
}

program_name=$(basename -- "$1")
audit_event process_start success 0
echo "Hardened runtime: role=$role program=$program_name core_limit=$(ulimit -c) ptrace_scope=$ptrace_scope" >&2

child_pid=
forward_signal() {
    local signal=$1
    audit_event signal_forwarded success "$signal"
    if [ -n "$child_pid" ]; then
        kill -"$signal" "$child_pid" 2>/dev/null || true
    fi
}
trap 'forward_signal INT' INT
trap 'forward_signal TERM' TERM
trap 'forward_signal HUP' HUP

"$@" &
child_pid=$!
wait "$child_pid"
status=$?
if [ "$status" -eq 0 ]; then
    audit_event process_stop success 0
else
    audit_event process_stop failure "$status"
fi
exit "$status"
