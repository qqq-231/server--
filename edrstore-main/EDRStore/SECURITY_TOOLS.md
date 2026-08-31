# Independent post-reduction security tools

The tools in this document run only after the original EDRStore upload and
container finalization have completed. They are built through the standalone
`tools/CMakeLists.txt` and do not link or rebuild `CLIENT_LIB`, `SERVER_LIB`, or
the key-manager library.

```bash
cmake -S tools -B /tmp/edrstore-security-tools-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/edrstore-security-tools-build
sha256sum -c baseline-binaries.sha256
```

## Container protection

Run only after ServerMain has finalized every container and emitted its normal
reduction statistics:

```bash
./bin/SecureStorageTool protect Containers SecureContainers
./bin/SecureStorageTool unprotect SecureContainers RecoveredContainers
./bin/SecureStorageTool verify Containers RecoveredContainers
```

The default key is `security/storage_master.key`. `--key <path>` selects an
explicit key file. Original containers are never overwritten or deleted, and
existing output files are never overwritten.

Each secure container uses this little-endian, versioned format:

```text
Magic "EDRSEC01"       8 bytes
Version                4 bytes
OriginalSize           8 bytes
Random GCM nonce      12 bytes
Ciphertext  OriginalSize bytes
GCM tag                16 bytes
```

The fixed overhead is 48 bytes per container. A per-container AES-256-GCM key
is derived as HMAC-SHA256(StorageMasterKey,
`"EDRStore-container-AES256-GCM-v1" || basename`). The complete 32-byte
header is authenticated as GCM AAD. Authentication must succeed before an
output file is atomically published.

For a normal download, point the unchanged server configuration at
`RecoveredContainers/`, not `SecureContainers/`.

## KeyRecipe protection

```bash
./bin/RecipeSecurityTool protect Recipes/<name>-key SecureRecipes/<name>-key.sec
./bin/RecipeSecurityTool unprotect SecureRecipes/<name>-key.sec Recipes/<name>-key
```

The default key is `security/client_master.key`. Recipe files use the same
48-byte-overhead layout with magic `EDRKEY01`, a fresh 12-byte nonce, and a
16-byte GCM tag. The AES-256-GCM key is independently derived with domain
`EDRStore-recipe-AES256-GCM-v1`.

Protection preserves the plaintext recipe by default. After a successful
experiment, `--delete-original` may be added explicitly to the `protect`
command. A plaintext recipe produced for download should be removed after the
unchanged ClientMain finishes.

## End-to-end file integrity

Create the tag before upload and verify it after download:

```bash
./bin/FileIntegrityTool create original.tar original.tar.mac.sec
./bin/FileIntegrityTool verify recovered.tar original.tar.mac.sec
```

The 52-byte metadata format is `EDRMAC01` (8 bytes), version (4), file size
(8), and HMAC-SHA256 (32). Its key is independently derived from the client
master key with domain `EDRStore-file-HMAC-v1`. Verification uses
`CRYPTO_memcmp` and returns nonzero on a size or MAC mismatch.

## Master keys and permissions

```text
security/storage_master.key  random 32 bytes, mode 0600
security/client_master.key   random 32 bytes, mode 0600
security/                    mode 0700
```

Existing keys are loaded, never regenerated. Keys and derived material are not
logged. The storage key, recipe key, and file-MAC key are separated.

## Full manual sequence

```text
FileIntegrityTool create
→ unchanged ClientMain upload
→ unchanged ServerMain finishes all containers and statistics
→ RecipeSecurityTool protect
→ SecureStorageTool protect

SecureStorageTool unprotect
→ RecipeSecurityTool unprotect to a temporary plaintext recipe
→ unchanged ClientMain download using recovered containers
→ FileIntegrityTool verify
→ remove the temporary plaintext recipe
```

## Security boundary

The unchanged network module already uses OpenSSL TLS methods, peer
certificate verification, CA certificates, and client/server certificates.
This phase deliberately does not set a new minimum TLS version or change mTLS
policy because that would rebuild and change the frozen ClientMain,
ServerMain, and KeyManager binaries. TLS hardening therefore remains a later,
separately measured phase or an external proxy deployment.

The frozen legacy `key_gen_thd.cc` also retains its uninitialized local-buffer
behavior. This is documented as the **Legacy similarity-preserving encryption
limitation**. The tools add post-reduction at-rest confidentiality, container
and recipe integrity, recipe confidentiality, and end-to-end file integrity;
they do not make the legacy inner XTS construction a standard AEAD scheme.
