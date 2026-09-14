# Security policy

## Supported versions

`mmxisf` is a pre-release library. The current `main` branch and the latest
published `0.1.x` pre-release receive best-effort security fixes. Older
pre-releases are unsupported. This is not a recommendation to deploy the
library without the consumer's own risk review.

## Input trust boundary

Treat every XISF file as untrusted. The parser validates XML structure, counts,
depth, text budgets, dimensions, arithmetic, attachment ranges and overlap,
unused space, compression descriptors, decompression ratio/window, checksums,
and output sizes before exposing decoded data. External file/URL blocks never
trigger automatic I/O or network access.

Applications remain responsible for choosing limits appropriate to their own
address space and workload. Raising a public `ReaderOptions` or `WriterOptions`
budget is an explicit consumer decision, not a claim that the host has enough
memory. A verified checksum establishes integrity against the declared digest,
not authenticity or trust in the producer.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability or attach proprietary
astronomy files to one. Use **Security > Report a vulnerability** in this
GitHub repository. Private vulnerability reporting is the project's official
security contact channel.

Include the affected commit/version, platform, smallest safe reproducer,
observed error, configured limits, and sanitizer output when available. Share
sensitive fixtures only after agreeing on a private transfer.

The maintainers aim to acknowledge a report within seven calendar days and to
triage it before public disclosure. This acknowledgement target is not a
service-level agreement. No remediation deadline is guaranteed before 1.0;
timing depends on severity, reproducibility, and the availability of a safe
fix.
