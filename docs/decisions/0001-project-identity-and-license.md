# ADR 0001: project identity and license

- Status: Accepted
- Date: 2026-09-13

## Context

An unrelated C++ library already uses a colliding public identity. Reusing it
would create package, linker, search, and legal-provenance confusion.

The new library must be usable by PFI and attractive to unrelated open-source
consumers.

## Decision

Use the public identity:

- project/package/repository: `mmxisf`;
- CMake target: `mmxisf::mmxisf`;
- C++ namespace and include prefix: `mmxisf`.

Use Apache License 2.0 for the repository.

## Consequences

The project avoids a known public collision and remains suitable for permissive
reuse, including in PFI.
