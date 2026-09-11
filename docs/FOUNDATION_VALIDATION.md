# Foundation durability validation

Baseline `1ae6641eeaec57c5a0aec4117e3de4283dd20be0` was revalidated on primitives_redesign before modification. F07 implements the locked P4 allocator and the reusable atomic-record seam required by later C3/C4/S5 work. Core dependency remains System; no package version change.

## Semantic review before executing existing tests

- persistence_tests: retain general memory filesystem/key-value lifecycle/read/write/list/capability tests. Replace obsolete backup/rollback assertions with durable publication and ambiguous-outcome recovery. Add rejection of a volatile backend before writes.
- serializable_persistence_tests: retain general serialization, limits, malformed/missing values and key/value behavior. Use a durable fault-model backend for atomic file saves. Replace silent fallback expectation with explicit ordinary-write opt-in; remove obsolete backup-file assertions.
- protected_serializable_persistence_tests: retain protection/authentication context and typed round trips; use a durable model for file saves and remove obsolete backup assertions.
- persistent_log_sink_tests: retain bounded queue, storage-work separation, formatting, rotation, restart discovery and overflow behavior. These are general Logging persistence semantics, not identity durability proof.
- persistent_log_worker_compile_tests: retain the current optional worker compile contract for the Foundation baseline. Its predecessor TaskExecutor configuration is a dependent migration when T1 changes; do not preserve that API with a compatibility alias.

## Added tests

atomic_record_tests denies heap allocation while exercising all seven write/sync/publication cut points, old-or-new recovery, caller-buffer limits, idempotent durable removal, key limits, unsupported/non-bounded backend rejection, corrupt generation/integrity, and the exact 40-byte P4 representation.

runtime_incarnation_tests uses separate process invocations for unsupported, missing, mismatched, corrupt, exhausted, final-valid, ambiguous-old, ambiguous-new, failed-write, concurrent and successful bootstrap. It verifies no candidate exposure on failure, sticky failure, exactly one advancement across concurrent allocator instances, AlreadyInstalled on service restart, and continued installed identity after storage shutdown.

runtime_incarnation_powerloss_test uses separate forked process identities with shared model media. The first process exits immediately after durable commit and before System installation. Subsequent processes allocate 2 and 3, proving committed-but-unused incarnation 1 is burned without adding any production reset API.

## Resource accounting and ordering

AtomicRecordKey stores 32 bytes and one length. AtomicFileRecordStore<M,N> owns N keys, a 256-byte directory buffer, one M+64-byte frame scratch buffer, and fixed bookkeeping. Per-operation temporary paths are bounded by StorageEntry::MaximumPathLength; AtomicFileStore uses two such arrays. StorageEntry metadata and fixed scalar/CRC temporaries add bounded stack use. Concrete media implementations separately account for their own fixed resources and must declare BoundedOperations truthfully.

The generic 64-byte envelope stores magic/format/header length, nonzero uint64 generation, opaque key/length, uint32 payload length, reserved zero fields and CRC32. Replacement increments generation without wrap. The specialized 40-byte P4 payload stores device identity and LastCommitted with its own version/integrity and H+1 generation. Neither uses native layout encoding.

Ordering is prepared write -> durable file data sync -> atomic namespace replacement -> durable directory sync -> successful return. P4 installation follows that successful return. Errors/exceptions after publication begins are CommitAmbiguous; there is no rollback. Temporary files are ignored during recovery. Deletion follows the owning semantic retirement commit and requires a directory sync even when a prior attempt already removed the visible name.

One atomic flag serializes all process bootstrap/provisioning transactions. The installed System identity is immutable and independent of later provider lifetime. No spin-wait, hidden task, heap buffer, counter block reservation or per-service incarnation exists.

## Validation status and platform limits

GCC 13.3 C++17 host build and all 16 CTest cases passed, including the seven-cut atomic-record suite, eleven bootstrap cases, separate-process commit-before-install test and existing optional Logging tests. Both optional serialization/protection suites compiled and ran separately. Six affected public headers compiled independently with warnings as errors and RTTI disabled; the key/record/allocator headers also compiled with exceptions disabled. Both new README snippets compiled, and the record-store example ran against the durable model. GitHub and ESP32 compile validation is pending publication. No external quota failure has been observed.

The fixed test model is an executable durability model, not a hardware certification. Existing physical ESP32 filesystem/NVS backends do not gain capabilities automatically. Their platform migration must provide proven durable/bounded operations or a bounded journal over an appropriate medium. Unsupported backends fail closed; no ordinary file/key-value Write is accepted as P4 durability evidence.
