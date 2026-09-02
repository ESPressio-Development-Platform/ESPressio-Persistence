# Implementing Storage Backends

A backend implements the portable Persistence contract by translating it onto a concrete storage mechanism without leaking native SDK types upward.

## Common responsibilities

Every backend must provide deterministic initialization/readiness behaviour, accurate capability reporting, appropriate statistics, explicit error outcomes, and resource cleanup.

## Choose the correct semantic interface

Implement `IFileStorage` for hierarchical path/file semantics and `IKeyValueStorage` for namespace/key semantics. Do not emulate unsupported filesystem features on a key/value store merely to fit one interface.

## Partial operations

A write that stores fewer bytes than requested is failure. A read reports the actual number of bytes returned within the caller-provided bound.

## Native errors

Translate native filesystem/media/NVS errors into the Persistence result vocabulary while retaining useful diagnostics where the contract permits it.

## Platform location

Hardware/SDK implementations belong in the target platform repository rather than portable ESPressio Persistence.