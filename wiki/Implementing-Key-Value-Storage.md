# Implementing Key Value Storage

An `IKeyValueStorage` provider maps namespace/key operations onto a concrete record store such as NVS/preferences or another key/value database.

## Contract concerns

Preserve key identity, complete binary-value replacement/read semantics, remove and clear behaviour, initialization/readiness, and accurate capacity/statistics where available.

## No filesystem assumptions

Do not expose directory, offset-read, append or rename semantics unless they genuinely belong to the key/value abstraction. The separate interface exists to avoid fictional filesystem behaviour.

## Namespaces

If the native backend has namespace/session ownership, keep that implementation detail behind the storage instance while presenting stable key semantics to consumers.

## Value bounds

Reads and writes must remain explicitly sized. Reject values the native backend cannot store rather than silently truncating them.

## Testing

Exercise overwrite, missing key, empty/binary values, remove/clear, size boundaries, initialization failure, and repeated lifecycle open/close behaviour.