# Public API, error, and resource-contract audit

- Audit date: 2026-09-14
- Reviewed implementation: `85b94f4deb1ec59ac996ce72b2e4b4fc338bcb7f`
- Scope: installed C++ headers, shared-library export surface, generated API
  reference, installed-package consumer, error mapping, ownership, resource
  budgets, cancellation, and concurrency contract
- Result: **PASS for the current pre-1.0 API surface**
- Release authority: none; repeat the diff review on the exact frozen candidate

## Public surface

The installed headers expose only C++ standard-library and `mmxisf` types.
Expat, zlib, LZ4, Zstandard, OpenSSL, platform file handles, and AppKit types do
not cross the public boundary. `Reader` keeps its parser/source implementation
opaque and move-only. The shared build exports the expected `Reader`, `Writer`,
enum-string, error-string, and version functions; it does not expose dependency
entry points as project API.

The library requires C++20 and intentionally uses `std::filesystem`,
`std::span`, `std::stop_token`, `std::shared_ptr`, and standard containers in its
source API. ABI compatibility is not promised before 1.0, so pre-1.0 consumers
must rebuild against the selected library version. No C ABI, exception-disabled
toolchain profile, or stable binary plugin boundary is claimed.

## Error behavior

All public parsing, decoding, and writing operations return `Result<T>` with a
stable `ErrorCode` category and bounded diagnostic context. Malformed input,
unsupported features, checksum mismatches, cancellation, resource exhaustion,
overflow, and I/O failures are ordinary result states. Third-party and caller
callback exceptions are caught at the reader/writer public operation boundary
and mapped to `internal_error`; allocation failures are mapped to
`resource_limit` where safe.

`Result<T>::value()` and `error()` deliberately follow `std::variant` access
semantics and can throw `std::bad_variant_access` when the caller selects the
wrong alternative. This is API misuse, not normal file-processing control flow,
and is documented in `docs/API.md`. The compile/run contract verifies both the
success and misuse paths plus a nonempty stable string for every current
`ErrorCode` enumerator.

## Ownership, lifetime, and partial results

- `Reader` owns its immutable `Document` and retains a shared reference to a
  caller-supplied `ByteSource`.
- Owning image, Property, and ICC results retain their own bytes. Caller-buffer
  reads never own the destination.
- Row spans are valid only during the synchronous `ImageRowSink::consume`
  callback. A sink owns copies and rollback after partial delivery.
- Writer image and Property spans are borrowed only for the synchronous
  `write_file()` or `write_to()` call. The reviewed header comment was corrected
  to name both operations.
- A caller-owned `ByteSink` can retain a prefix on failure and remains
  responsible for transaction, close, and rollback semantics. Filesystem output
  retains the stronger exclusive temporary-file and atomic no-overwrite commit
  contract.

## Resource, cancellation, and concurrency boundaries

`ReaderOptions`, `WriterOptions`, and `ImageRowReadOptions` provide finite,
nonzero defaults for the externally amplified header, XML, metadata, extension,
image, Property, compression, checksum, table, thumbnail, ICC, row, and
subblock paths. The public contract test checks the principal defaults and
cumulative writer limits. Detailed negative and fuzz tests retain the full
boundary coverage.

Long reads and writes accept `std::stop_token` and report cooperative
`cancelled` results. Parsed state is immutable and operation state is per call.
The built-in file source serializes its stable handle; custom sources and sinks
retain their own thread-safety obligations. ThreadSanitizer covers concurrent
owning/row reads and same-destination writer contention.

## Verification

Exact commit `85b94f4deb1ec59ac996ce72b2e4b4fc338bcb7f` passed the maintained
local macOS arm64 gate: warnings-as-errors static/shared tests 11/11 each,
installed-package consumers 1/1 each, ASan/UBSan 11/11 plus 20,000 deterministic
mutations, ThreadSanitizer 11/11, generated API documentation, and strict deep
viewer-bundle signature verification. `mmxisf.public-api-contract` is included
in every maintained test configuration.

The final candidate gate must review the public-header and export-table diff
from this audited commit, repeat the installed static/shared consumers and
sanitizers on the exact frozen candidate, and confirm that release notes state
the pre-1.0 ABI policy. This open exact-candidate check is independent of the
current audit PASS.

## Linux shared-export follow-up

The production-dependency Linux build exposed a packaging defect after the
original source-level audit. Exact development commit
`5ee46464903cc5f9fb11ba3f9f04d1e51ba701b3` exported 8,239 dynamic symbols,
including entry points from statically linked dependencies. The first archive-
exclusion fix at `f5cf40403284171733589cc1853d8a2a364acb79` still exposed 18 weak
libstdc++ template symbols and failed its new shared-export test; that result is
retained as diagnosis, not passing evidence.

Exact development commit `436a9b443bab67a043842dcbda9168e502cf3c56`
adds a deny-by-default Linux version script restricted to the public `mmxisf`
namespace, keeps static dependency archives hidden, and makes the export audit
a CTest contract. Its deterministic extracted-source production-graph gate on
Linux amd64 passed static 13/13, shared 14/14, installed consumers 1/1 each,
and reported exactly 39 `mmxisf` symbols out of 39 dynamic exports. Full
evidence is retained in
`docs/quality-runs/2026-09-14-linux-amd64-436a9b4.json`. This closes the current
Linux dependency-export defect without changing the pre-1.0 ABI policy or the
still-open exact frozen-candidate diff review.
