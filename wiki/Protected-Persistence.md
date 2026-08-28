# Protected Persistence

Protected persistence composes Persistence, Serializable and Security without making cryptography part of the storage backend.

Include the explicit integration surface:

```cpp
#include <ESPressio_Persistence_Serializable_Security.hpp>
```

Conceptually:

```text
Serializable object
      |
      v
ESPB BinaryArchive
      |
      v
Security::IDataProtector
      |
      v
protected bytes
      |
      v
IFileStorage / IKeyValueStorage
```

Loading reverses the process: read bytes, authenticate/decrypt, deserialize/migrate/default/validate, then populate the object.

## Result separation

Protected persistence keeps storage/media failures, Security authentication/key failures, and Serializable schema/deserialization failures distinguishable.

## Atomic files

For file storage, protected saves may use `AtomicFileStore` when the backend advertises suitable rename capability. Protection does not weaken the normal reliability policy.

## Purpose binding

Use a stable, domain-specific Serializable protection context so a protected record cannot be substituted into an unrelated persistence purpose under the same key.