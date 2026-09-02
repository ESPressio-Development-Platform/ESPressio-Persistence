# Testing Storage Backends

Backend tests should prove conformance to the portable Persistence contract rather than merely demonstrate successful native SDK calls.

## Common coverage

Test initialization/readiness, advertised capabilities, statistics, bounded reads, complete writes, missing resources, invalid locators, resource cleanup, and repeated lifecycle use.

## File backends

Exercise replace, append, rename, list/stat/directories where advertised, partial-write failure, removable-media behaviour, and every failure stage of atomic replacement/rollback.

## Key/value backends

Exercise replace/read/remove/clear, binary values, overwrite, missing keys, value-size limits, and namespace lifecycle.

## Typed integrations

Run Serializable round trips and malformed/bounded decode tests through the backend. Where Security is enabled, verify protected round trips and preserve distinct storage/authentication/schema errors.

## Destructive policy

If a platform provider supports format-on-failure, test both conservative default and explicit destructive recovery modes. Mount failure in conservative mode must not format automatically.