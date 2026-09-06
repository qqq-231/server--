# First-stage hardening

The legacy data path is frozen. The security controls in this directory wrap
the existing programs or operate on stored artifacts; they do not modify the
`enc_data` supplied to Finesse.

## Frozen baseline

- Verify binaries with `sha256sum -c security/baseline-first-stage.sha256`.
- The exact Stage 0 reduction values are in
  `security/baseline-reduction-metrics.txt`.
- After a run, execute `security/verify_first_stage.sh bin/server-log`.
- Any binary hash or metric mismatch is a rollback condition.

## Runtime isolation

Run each executable through `security/harden_runtime.sh` using separate OS
accounts (`edr-client`, `edr-server`, and `edr-km`). The wrapper sets umask 077,
disables core dumps, checks key permissions, records non-sensitive lifecycle
events, and warns when Linux ptrace restrictions are too weak.

Example:

```text
security/harden_runtime.sh km ./KeyManager
security/harden_runtime.sh server ./ServerMain
security/harden_runtime.sh client ./ClientMain ...
```

The files under `security/systemd/` are deployment examples, not automatic
installers. Review paths and accounts before installing them. Recommended
permissions are 0700 for key/storage/audit directories and 0600 for key files.
Set `kernel.yama.ptrace_scope=2` (or stronger) through the host's managed sysctl
configuration after checking compatibility; the project scripts do not change
this host setting automatically.

## KeyManager network perimeter

`security/network/` contains outer mTLS stunnel examples and a dry-run-first
nftables generator. The tunnel transports the existing TLS byte stream without
changing its payload. Restrict the externally reachable KeyManager port to the
approved client addresses and keep the legacy endpoint on loopback.

Do not run `configure_km_firewall.sh --apply` until the printed rules, interface
policy, client address, and rollback access have been reviewed on the target
host.

## Stored-artifact confidentiality and integrity

The independently built tools protect containers and recipes with AES-256-GCM
and can authenticate files with HMAC-SHA256. They are deliberately absent from
the legacy build graph. Use new key files, keep them mode 0600, verify protected
artifacts before deleting originals, and retain a tested recovery copy.

The security-tool key buffer is best-effort memory locked. Failure to `mlock`
is explicit and non-fatal so a low host memlock limit cannot silently change
legacy behavior. Sensitive buffers are cleansed on destruction.

## Logging and audit

Run `security/check_sensitive_logging.sh` after changes. Audit records contain
only role, event name, process identifier, status, and time; command arguments,
keys, plaintext, fingerprints, and dataset paths are excluded.

## Stage gates

- Stages 0-5 may be validated without changing the Finesse input.
- Stage 6 (random persistent KeyManager root credentials) passed the exact
  Safe-4 metric gate. Stages 7 and 8 remain blocked/deferred; see
  `security/STAGE6-8_EXPERIMENT_PLAN.md`.
- A reduction mismatch, failed restore, authentication failure, or binary hash
  mismatch requires stopping the experiment and returning to the frozen files.
