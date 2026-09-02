# Atomic File Replacement

`AtomicFileStore` provides capability-aware replacement intended to reduce the chance that a failed update destroys the last valid file.

When the backend advertises the required rename semantics, replacement follows the conceptual sequence:

```text
write temporary
      |
      v
backup existing target
      |
      v
promote temporary
      |
      +-- failure --> rollback backup
```

Typed and protected file persistence can use this automatically when the backend supports it.

## Capability requirement

Atomic replacement is not assumed merely because a backend looks filesystem-like. The backend must advertise the capabilities required by the algorithm.

## Partial writes

A partial write is not success. Implementations and adapters must propagate incomplete writes explicitly so promotion never treats a truncated temporary file as valid.

## Direct replacement

Higher-level protected persistence can deliberately disable preferred atomic replacement when the caller explicitly accepts direct replacement semantics.