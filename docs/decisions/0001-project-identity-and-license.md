# ADR 0001: provisional project identity and license

- Status: Accepted locally; external publication pending G0
- Date: 2026-09-13

## Context

The required local directory is `software/libXISF`, but an unrelated C++
library already uses `libXISF`, publishes a `libXISF` shared object, and is
distributed as `libxisf`. Reusing that identity would create package, linker,
search, and legal-provenance confusion.

The new library must be usable by PFI and attractive to unrelated open-source
consumers.

## Decision

Use the provisional public identity:

- project/package/repository: `mmxisf`;
- CMake target: `mmxisf::mmxisf`;
- C++ namespace and include prefix: `mmxisf`;
- local working directory: `software/libXISF` as requested.

Use Apache License 2.0 for the local repository. Do not publish or register a
package, and do not treat the public name as final, until G0 receives explicit
owner approval.

## Consequences

The project avoids a known public collision and remains suitable for permissive
reuse, including in PFI. The local directory name differs from the public
package identity. Renaming before the first public release is inexpensive.
