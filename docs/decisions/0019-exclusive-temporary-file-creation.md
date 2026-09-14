# ADR 0019: Exclusive temporary-file creation

- Status: accepted
- Date: 2026-09-14

## Context

`write_file` and multi-subblock compression use sibling temporary files. A
separate availability check followed by a truncating stream open leaves a
time-of-check/time-of-use interval. Another process can create the same path in
that interval, after which a truncating open can overwrite it. Registering the
path for cleanup before successful creation can also make the writer remove a
file it did not create.

The final destination already uses a no-overwrite hard-link commit, but that
does not make creation of the source temporary file exclusive.

## Decision

Temporary XISF files and compression spools are created with the operating
system's exclusive-create primitive:

- POSIX: `open` with `O_CREAT | O_EXCL`, plus close-on-exec where available;
- Windows: `_wsopen_s` with `_O_CREAT | _O_EXCL`, binary and non-inheritable
  flags.

The cleanup guard takes ownership of a path only after exclusive creation has
succeeded. File-backed output is flushed to the operating system before close.
The destination remains committed by a no-overwrite hard link after the
temporary file has closed successfully.

The writer directory and scratch directory remain caller-controlled trust
boundaries. The library prevents accidental or cooperating-process collisions;
it does not claim to defend a path against a privileged actor that can unlink
or replace entries inside those directories while a write is in progress.

## Consequences

- stale temporary and scratch files fail closed without being changed;
- simultaneous writers targeting the same destination cannot both acquire the
  deterministic temporary path, and exactly one can publish successfully;
- cleanup never registers a path solely because it was observed as absent;
- filesystems without exclusive creation or hard links fail explicitly;
- publication durability across a power loss still depends on filesystem and
  volume semantics; atomic no-overwrite visibility is the guaranteed contract.
