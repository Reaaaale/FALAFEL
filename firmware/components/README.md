# Firmware components

`falafel_persistence` compiles unchanged shared crypto, offline envelope,
mailbox and history modules. `falafel_storage` implements their callbacks on a
dedicated NVS partition. See [storage design](../../docs/embedded-storage.md) and
[C6 build commands](../README.md). Host recovery tests and C6 cross-build pass;
physical power-cut/GC and S3 validation remain pending. Embedded crypto is not
approved for permanent identities.

Future adapters include display/touch, networking and power; no board-specific
driver is assumed by storage.

The shared GUI is not integrated into firmware. Boot only probes an existing
namespace: it does not provision identities or consume messages.

