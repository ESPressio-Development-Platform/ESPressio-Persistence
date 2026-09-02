# Storage Backends and Capabilities

`IStorageBackend` provides common initialization, readiness, capability, and statistics semantics beneath both file and key/value storage.

Persistence deliberately distinguishes filesystem semantics from namespace/key semantics rather than forcing every backend into one lowest-common-denominator API.

## Capability discovery

Use backend capabilities to decide which optional operations are safe:

```cpp
if (HasCapability(
        storage.GetCapabilities(),
        StorageCapability::Removable)) {
    // Media disappearance is an expected runtime condition.
}
```

Capabilities describe properties such as hierarchical paths, key/value semantics, directory support, rename, append, removable media, capacity reporting, and suitability for atomic replacement.

## Application design

Code should depend only on capabilities it genuinely requires. Do not downcast a storage interface to a concrete backend merely to discover whether an operation is supported.

## Provider design

A backend must advertise capabilities truthfully. Claiming rename/atomic suitability when the native medium cannot provide the required semantics can turn recoverable storage failure into data corruption.