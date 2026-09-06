#!/usr/bin/env bash
set -u

usage() {
    echo "Usage: $0 --client-ip <IPv4> [--legacy-port 16668] [--mtls-port 17667] [--rate 20] [--apply]" >&2
}

client_ip=
legacy_port=16668
mtls_port=17667
rate=20
apply=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --client-ip) [ "$#" -ge 2 ] || { usage; exit 2; }; client_ip=$2; shift 2 ;;
        --legacy-port) [ "$#" -ge 2 ] || { usage; exit 2; }; legacy_port=$2; shift 2 ;;
        --mtls-port) [ "$#" -ge 2 ] || { usage; exit 2; }; mtls_port=$2; shift 2 ;;
        --rate) [ "$#" -ge 2 ] || { usage; exit 2; }; rate=$2; shift 2 ;;
        --apply) apply=1; shift ;;
        *) usage; exit 2 ;;
    esac
done

if ! [[ "$client_ip" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]] ||
   ! [[ "$legacy_port" =~ ^[0-9]+$ ]] || [ "$legacy_port" -lt 1 ] || [ "$legacy_port" -gt 65535 ] ||
   ! [[ "$mtls_port" =~ ^[0-9]+$ ]] || [ "$mtls_port" -lt 1 ] || [ "$mtls_port" -gt 65535 ] ||
   [ "$legacy_port" -eq "$mtls_port" ] ||
   ! [[ "$rate" =~ ^[0-9]+$ ]] || [ "$rate" -lt 1 ]; then
    usage
    exit 2
fi

rules=$(cat <<EOF
table inet edrstore_km {
  chain input {
    type filter hook input priority 0; policy accept;
    iifname "lo" tcp dport $legacy_port counter accept
    tcp dport $legacy_port counter drop
    ip saddr $client_ip tcp dport $mtls_port ct state new limit rate $rate/second burst $((rate * 2)) packets counter accept
    tcp dport $mtls_port counter drop
  }
}
EOF
)

printf '%s\n' "$rules"
if [ "$apply" -eq 0 ]; then
    echo "Dry run only. Re-run with --apply after reviewing the rules." >&2
    exit 0
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "--apply requires root privileges." >&2
    exit 1
fi
command -v nft >/dev/null 2>&1 || {
    echo "nft is not installed." >&2
    exit 1
}
if nft list table inet edrstore_km >/dev/null 2>&1; then
    echo "Refusing to replace an existing inet edrstore_km table." >&2
    exit 1
fi
printf '%s\n' "$rules" | nft -f -
