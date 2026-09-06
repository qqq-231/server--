# KeyManager perimeter controls

These files protect who can reach KeyManager without modifying the legacy
request/response payload, feature, key seed, chunk key, or encrypted chunk.

## Firewall

Review a rate-limited nftables ruleset without changing the host:

```bash
security/network/configure_km_firewall.sh --client-ip 192.0.2.10 \
  --legacy-port 16668 --mtls-port 17667
```

After review, an administrator may add `--apply`. The script refuses to replace
an existing `inet edrstore_km` table. Test through an out-of-band administration
session first so firewall mistakes cannot lock out the host.

The generated rule accepts the legacy port only on loopback and drops that
port on every other interface. Approved remote clients can reach only the
outer mTLS port. A firewall rule that permits an approved client to connect
directly to the legacy port defeats this boundary and must not be used.

## Outer mTLS

Install stunnel separately, issue a dedicated outer CA, one client certificate,
and one KeyManager certificate, then replace every `@PLACEHOLDER@` in the two
example files. Private keys must be mode 0600.

The client-side tunnel accepts only loopback port 16667 and authenticates the KM
certificate and hostname. The KM-side tunnel requires a certificate signed by
the outer CA and forwards accepted connections to loopback port 16668. Run the
legacy KeyManager in a separate runtime directory whose config uses port 16668.
The existing inner EDRStore TLS stream passes unchanged through the outer mTLS
tunnel, so the application payload and `enc_data` do not change.

Use the firewall to expose outer port 17667 only to approved client addresses
and keep inner port 16668 restricted to loopback. stunnel authentication errors
and connection events are written to `security/audit/` without application
features or keys.
