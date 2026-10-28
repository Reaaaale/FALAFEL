# Desktop pairing experiment (Milestone 3B.2)

This experiment integrates the portable `shared/pairing` state machine with
Noise-C revision `cfe25410979a87391bb9ac8d4d4bef64e9f268c6`. The pairing core
does not change the GUI, relay, event-v1 protocol, or 3A provider; the separate
3B.3 adapter below adds desktop identity persistence.
The verified archive is fetched into the CMake build directory through
`cmake/noise_c_pinned.cmake`; no pre-existing `/tmp` checkout is required.

The module performs all three `Noise_XX_25519_AESGCM_SHA256` flights with a
distinct X25519 static identity per peer. After `Split()`, it derives a
zero-padded 12-digit SAS from `SHA-256("falafel/pairing/sas/v1" ||
handshake_hash || counter)`. A 40-bit candidate is accepted only when below
`10^12`; rejection sampling therefore avoids modulo bias.

Acceptance and rejection records include the role, SAS, and complete handshake
hash and are AEAD-protected by the directional Noise transport states. Both
authenticated accept records are required for completion. The state machine
rejects out-of-order flights, early or duplicate decisions, modified records,
and reuse after a terminal state. It never logs key material.

## Build and automated demonstration

```sh
cmake -S experiments/desktop-pairing -B build/desktop-pairing \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop-pairing
ctest --test-dir build/desktop-pairing --output-on-failure
./build/desktop-pairing/pairing_harness
```

`pairing_harness` forks two independent processes connected by a Unix socket.
Each generates an ephemeral-for-this-run identity, completes XX, displays the
same SAS, explicitly accepts, exchanges authenticated decisions, and exits
successfully only if both reach `COMPLETE`.

## Manual two-terminal pairing on macOS

Terminal 1 (responder):

```sh
./build/desktop-pairing/pairing_peer listen 9847
```

Terminal 2 (initiator):

```sh
./build/desktop-pairing/pairing_peer connect 127.0.0.1 9847
```

Compare the displayed SAS and type `yes` in both terminals. Any other answer
sends an authenticated rejection. The demo binds only to loopback and creates
fresh identities on every run, as persistence is outside this milestone.

## Persistent desktop harness (Milestone 3B.3)

The portable persistence module stores an X25519 local identity plus an
optional verified peer public key, validates private/public consistency, and
encodes versioned metadata with a corruption checksum. Filesystem handling is
separate. On macOS, the private key is a generic-password Keychain item with
`AfterFirstUnlockThisDeviceOnly` accessibility; the `0600` metadata file holds
only public keys, flags, generation, and checksum. Updates use a same-directory
temporary file, `fsync()`, atomic `rename()`, and directory `fsync()`.

Run the restart, recognition, interrupted-save, corruption, incomplete-pairing,
and identity-rotation demonstration with:

```sh
./build/desktop-pairing/pairing_persistence_harness
```

The harness uses isolated temporary Keychain items and removes them when done.
A corrupt committed metadata file fails closed; recovery requires explicit
`unpair`. No rejected or incomplete session is passed to the persistence API,
and the API refuses to extract a peer key before bilateral completion.

### Rollback limitation

Atomic writes and the checksum detect torn or corrupted files, but do not
prevent an attacker with filesystem write access from replacing metadata with
an older, internally valid copy for the same Keychain identity. macOS Keychain
protects the private key at rest, but this milestone does not use a Keychain
monotonic counter, Secure Enclave anti-rollback primitive, or server witness.
The generation field supports detection only when compared with a separately
trusted value, which is not yet stored. Therefore rollback resistance is not a
security guarantee of 3B.3.
