# Capability Contract

`StorageCapability` lets consumers query optional backend semantics without depending on a concrete implementation.

Capabilities are promises, not hints. If a provider advertises a capability, consuming code may rely on the corresponding contract.

## Examples

Capabilities cover distinctions such as hierarchical/file semantics, key/value semantics, directories, rename, append, removable media, capacity reporting, and suitability for atomic replacement.

## Provider rule

Advertise only what the native storage mechanism and implementation can guarantee across normal failure conditions.

For example, atomic-replacement suitability requires more than having a rename-shaped API: the backend must support the sequence and failure/rollback semantics expected by `AtomicFileStore`.

## Consumer rule

Check capabilities before selecting optional behaviour. Do not identify backend classes by type/name to infer features that the abstraction already exposes explicitly.