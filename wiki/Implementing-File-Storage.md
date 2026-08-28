# Implementing File Storage

An `IFileStorage` provider maps portable path/file operations onto a concrete filesystem or media implementation.

## Contract concerns

Implement only the operations/capabilities the backend can actually guarantee: bounded offset reads, complete writes/replacement, append, rename, directory/list/stat semantics, capacity/removable reporting, and initialization.

## Atomic replacement

If `Rename`/atomic-replacement suitability is advertised, verify the backend semantics are strong enough for `AtomicFileStore`'s temporary/backup/promote/rollback algorithm. Do not advertise the capability merely because the native API exposes a function named rename.

## Removable media

For SD/removable providers, readiness may change after initialization. Surface media disappearance/reappearance rather than caching a permanent ready state that no longer reflects reality.

## Formatting

If a target provider offers format-on-failure, make it explicit and opt-in. Mount failure must not silently destroy persisted data by default.

## Testing

Exercise missing paths, bounded reads, partial/native write failure, rename failure at each atomic stage, directories where supported, capacity reporting, and removable-media transitions.