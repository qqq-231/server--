# Stage 6-8 experiment gate

Stage 6 is now the active single-variable experiment. The current gate is the
frozen Safe-4 reduction baseline and the `.safe4-baseline` executables.

## Stage 6: random KeyManager master secrets

Status: **PASS**.

Only the fixed KeyManager root secrets are replaced by two independent random,
persistent 256-bit values. The legacy SHA-256 key-seed construction is frozen.
Use a separate, fresh storage namespace and a controlled A/B run against the
Safe-4 dataset. The A and B stores, recipes, keys, and logs must not be mixed.
The two controlled fresh-store runs produced the exact Safe-4 metrics and
byte-identical restored output. The persistent-reload deterministic test also
returned `SAME`. Results are recorded in `security/stage6-results.txt`.

## Stage 7: HMAC PRF

Status: **BLOCKED BY STAGE 6**.

Do not replace the current derivation with HMAC until Stage 6 passes its own
A/B gate. This stage must repeat the same fresh-store and download-integrity
tests. A failed metric comparison means rollback to the Stage 6 baseline.

## Stage 8: TEE / SGX

Status: **DEFERRED**.

Do not emulate a TEE in software and call it secure. First verify hardware,
kernel device, SDK, attestation service, sealing policy, and operational key
rotation support. Introduce a KeyManager abstraction only in a separate branch
after Stages 6 and 7 pass. Preserve a non-TEE provider for exact A/B testing.
