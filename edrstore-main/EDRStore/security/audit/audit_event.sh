#!/usr/bin/env bash
set -u

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "Usage: $0 <component> <event> <success|failure|warning> [error-code]" >&2
    exit 2
fi

component=$1
event=$2
result=$3
code=${4:-0}
safe='^[A-Za-z0-9_.:-]+$'
if ! [[ "$component" =~ $safe && "$event" =~ $safe && "$result" =~ $safe && "$code" =~ $safe ]]; then
    echo "Audit fields may contain only safe identifier characters." >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
log="$script_dir/security-audit.log"
umask 077
mkdir -p "$script_dir"
chmod 700 "$script_dir"
printf 'time=%s component=%s event=%s result=%s code=%s uid=%s pid=%s\n' \
    "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$component" "$event" \
    "$result" "$code" "$(id -u)" "$$" >> "$log"
chmod 600 "$log"
