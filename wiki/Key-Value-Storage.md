# Key Value Storage

`IKeyValueStorage` models namespace-owned key/value storage separately from filesystem paths.

Use it for compact settings and records whose natural identity is a key rather than a hierarchical file path.

Typical operations include replace/write, read, remove and clear.

## Why it is separate from files

A key/value store does not necessarily have directories, rename semantics, file offsets or append. Persistence therefore does not force those concepts into one generic storage API.

## Typed persistence

The same `SaveSerializable()` / `LoadSerializable()` model can operate over key/value storage; only the locator semantics differ from file storage.

## Provider boundary

A platform implementation such as an NVS/preferences adapter implements this contract outside the portable Persistence core. Domain code should continue to accept `IKeyValueStorage&`.