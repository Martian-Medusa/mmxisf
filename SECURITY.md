# Security policy

## Supported versions

`mmxisf` is a pre-release library. No released version receives a production
security-support promise yet. The current `main` branch is maintained while the
public-beta gate is being completed; this is not a recommendation to deploy it
without the consumer's own risk review.

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
astronomy files to one. Contact the repository owner privately or use GitHub's
private vulnerability-reporting form once it is enabled for the public
repository. Include the affected commit/version, platform, smallest safe
reproducer, observed error, configured limits, and sanitizer output when
available. Share sensitive fixtures only after agreeing on a private transfer.

Security reports will be acknowledged and triaged before public disclosure.
Exact response-time and supported-version commitments will be published with
the first public beta.
