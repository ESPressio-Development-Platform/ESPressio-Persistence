# Typed Serializable Persistence

Typed persistence layers ESPressio Serializable over the raw storage contracts without changing backend semantics.

Include:

```cpp
#include <ESPressio_Persistence_Serializable.hpp>
```

Then save/load a Serializable model through an `IFileStorage` or `IKeyValueStorage` implementation:

```cpp
SaveSerializable(storage, "/config.espb", source);
LoadSerializable(storage, "/config.espb", restored);
```

For key/value storage, supply the storage key instead of a path.

## Representation

The normal typed path uses Serializable's structured ESPB `BinaryArchive`, retaining schema-version, migration, alias, default and validation behaviour.

## Layering

Persistence owns storage and reliability policy; Serializable owns schema and representation. Persistence does not duplicate schema logic.

## Decode limits

Typed loading remains bounded according to the Serializable archive limits. A backend returning a large or malformed blob does not imply that deserialization may allocate without constraint.