# Firmware components

`falafel_persistence` compiles unchanged shared crypto, offline envelope,
mailbox, v2 link and history modules. `falafel_storage` implements their callbacks on a
dedicated NVS partition. See [storage design](../../docs/embedded-storage.md) and
[C6 build commands](../README.md). Host recovery tests and C6 cross-build pass;
physical power-cut/GC and S3 validation remain pending. Embedded crypto is not
approved for permanent identities.

Future adapters include display/touch and power; no board-specific driver is
assumed by storage. `../main/lab_messaging.c` uses the existing mailbox as the sole
ACK/delivery authority; `relay_network.c` owns its Wi-Fi/TCP task and the NVS handle.
The optional C6 lab worker loads an explicitly provisioned temporary identity,
creates/reopens the lab mailbox, retries persisted ciphertext and verifies peer
receipts. Shared GUI and Message Manager are not integrated into firmware.
Hardware exchange and physical NVS validation remain pending. See
[lab procedure](../../docs/c6-relay-transport.md).
