# Reliability and Failure Handling

Persistence treats failure semantics as part of the storage contract.

## Core principles

1. Initialization failure is visible.
2. Destructive recovery/formatting is never an implicit core policy.
3. Raw reads are caller-bounded.
4. Typed archive decoding is bounded.
5. Partial writes are not success.
6. Filesystem and key/value semantics remain distinct.
7. Atomic replacement is capability-aware.
8. Serializable persistence preserves schema evolution.
9. Protected persistence authenticates before deserialization.
10. Optional integrations do not force dependencies on core-only consumers.
11. Concrete hardware/backend choice remains outside reusable domain code.

## Removable media

When a backend advertises `Removable`, disappearance/reappearance is an expected runtime condition and should be handled explicitly rather than treated as impossible state.

## Destructive recovery

Some platform filesystem providers may offer format-on-mount-failure as an opt-in policy. Portable Persistence does not silently request that behaviour; data destruction must be an explicit platform/application decision.