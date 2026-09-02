# Extension Architecture

Persistence extensions should preserve the separation between storage semantics, backend implementation, serialization, and security.

```mermaid
graph TD
  DOMAIN[Domain / Reusable Library] --> CONTRACT[IStorageBackend]
  CONTRACT --> FILE[IFileStorage]
  CONTRACT --> KV[IKeyValueStorage]
  MEMORY[Portable Memory Backend] -. implements .-> FILE
  MEMORY -. implements .-> KV
  PLATFORM[Target Platform Package] -. implements .-> FILE
  PLATFORM -. implements .-> KV
  SERIALIZABLE[Serializable Integration] --> CONTRACT
  SECURITY[Protected Serializable Integration] --> SERIALIZABLE
```

## Ownership rule

Persistence owns portable storage contracts, capabilities, atomic-replacement policy and optional typed-persistence adapters.

Target-specific filesystem, flash, NVS, SD, driver and SDK implementation belongs in the platform package.

## Extension invariants

Keep core Persistence platform-neutral, retain distinct file/key-value semantics, advertise capabilities truthfully, preserve bounded reads/decoding, propagate partial writes/failures explicitly, and keep optional integrations optional.