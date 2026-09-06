# Security audit events

`harden_runtime.sh` records process start, stop, forwarded signals, exit status,
role, UID, PID, and the observed ptrace scope. The stunnel examples write
connection and certificate failures to this directory. Firewall rejection
counts remain in nftables counters.

Manual deployment events can be recorded with:

```bash
security/audit/audit_event.sh km authentication success 0
security/audit/audit_event.sh km connection_rejected failure bad_certificate
security/audit/audit_event.sh storage key_permission warning wrong_mode
```

Never place features, key seeds, chunk keys, master keys, plaintext, complete
ciphertext, TLS private keys, or command-line data paths in an audit field.
The script only accepts identifier characters and creates mode-0600 logs in a
mode-0700 directory.
