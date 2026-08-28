# Memory Backends

Portable in-memory implementations live in ESPressio Persistence itself and are intended for host tests, examples, and application logic that should be exercised without hardware storage.

```cpp
MemoryFileStorage files;
files.Initialize();

MemoryKeyValueStorage values;
values.Initialize();
```

## Why they matter

The memory backends implement the same portable contracts used by platform providers. This allows tests to exercise:

- raw file/key-value operations;
- typed Serializable persistence;
- protected typed persistence;
- atomic replacement behaviour where supported by the test backend;
- application failure handling.

## Testing discipline

A memory backend is not a substitute for platform-provider conformance tests. It verifies domain/persistence logic independently; the platform package must separately prove that its filesystem/NVS/media implementation satisfies the same contract.