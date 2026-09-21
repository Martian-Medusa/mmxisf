# XISF 1.0 Revision 1 implementation plan

## Status

- Plan state: `IMPLEMENTED_AWAITING_QUALIFICATION`
- Prepared: 2026-09-20
- Target specification: XISF 1.0, Revision 1, document version 1.01,
  September 2026
- Official source commit:
  `9dbdd650bbc4b7e51884f6bceeb7607be5f678a3`
- Intended release candidate after completion: `v0.1.0-rc.2`
- Implementation authorized: 2026-09-21
- Release, tag, push, and publication remain separate owner-approval steps.

This plan updates the standalone reusable C++ library. It does not start PFI
integration and does not promote the optional viewer into the library product
boundary.

## Outcome

Bring the declared local-monolithic `mmxisf` reader/writer profile into
alignment with XISF 1.0 Revision 1 while preserving the security, resource,
precision, packaging, and clean-room guarantees of `v0.1.0-rc.1`.

The immutable `v0.1.0-rc.1` tag remains historical evidence for the frozen
document-1.00 profile. It must not be moved or rewritten. Revision 1 work is a
new candidate and must receive new evidence.

## Current implementation result

The source-level Revision 1 corrections are implemented locally. The reader
and writer now use global shuffle semantics across compression subblocks;
identifier, non-finite scalar, empty container, extension-placement, unknown
Property/Table Field availability, standardized astrometric namespace, and
metadata-preservation cases have direct regression coverage. RGB working-space
support is intentionally classified as structural inspection only.

The current macOS arm64 tree passes static/shared warnings-as-errors builds,
installed/relocated/embedded consumers, reproducibility checks, ASan/UBSan,
20,000 deterministic mutations, generated API documentation, and the signed
viewer regression gate. Four representative generated headers validate against
the pinned official Revision 1 XSD.

This is not yet an RC2 qualification result. A fresh native PixInsight run,
independent-consumer replay, Linux, native Windows/MSVC, exact-candidate
coverage-guided fuzzing, and frozen candidate evidence remain pending. The
local Command Line Tools installation does not include a compatible libFuzzer
runtime, so coverage-guided fuzzing is deferred to a qualified Clang/Linux or
full-Xcode environment; the deterministic ASan/UBSan smoke remains PASS.

## Normative sources and clean-room boundary

Use only the following normative or first-party materials:

- the official XISF 1.0 Revision 1 HTML document;
- the official specification source repository at the exact commit above;
- the official `xisf-1.0.xsd` from that commit;
- XISF files produced through the public PixInsight UI or documented public
  APIs as black-box interoperability evidence.

Do not inspect, copy, translate, or imitate PixInsight/PCL XISF implementation
source. Do not use another XISF library implementation as a design template.
Independent packages may only be exercised through their documented public
interfaces, with provenance and license boundaries recorded as before.

## Current impact summary

Revision 1 preserves the XISF format version `1.0` and keeps valid document-1.00
units valid. Most of the current profile already implements the new baseline:
Zstandard, both byte orders, standard checksums, scalar types, Gray/RGB images,
multiple images, and generic vector/matrix Properties.

The release-blocking discrepancy is byte shuffle combined with compression
subblocks. Revision 1 states that the complete data block is shuffled before
the shuffled stream is divided into independently compressed subblocks. The
current implementation shuffles and unshuffles each subblock independently.
This is self-consistent but not conforming to the clarified standard and can
produce incorrect pixels for a conforming multi-subblock `+sh` input.

Additional required work concerns image identifiers, alternative non-finite
scalar forms, empty vectors/matrices, extension placement, unsupported-object
availability, RGB working-space validation, the standardized astrometric
namespace, and specification/readiness provenance.

## Scope and priority

### P0: release-blocking correctness

1. Global byte shuffle before compression subblock division in the writer.
2. Global byte unshuffle after logical subblock reconstruction in the reader.
3. Fail-closed behavior for row delivery until it is correct for every claimed
   shuffled/subblock combination.
4. Independent deterministic fixtures that would fail with the old
   per-subblock algorithm.
5. Native PixInsight interoperability for the corrected path.

### P1: normative parser/writer corrections

1. Unique and syntactically valid `Image/@id` values.
2. All Revision 1 non-finite floating-point spellings.
3. Empty vector and matrix Property serialization and reading.
4. Root-only placement of extension elements.
5. Ignore/preserve unsupported Properties and optional objects without making
   the rest of a valid XISF unit inaccessible.
6. Revision 1 RGB working-space consistency.

### P2: conformance and provenance

1. Pin specification version 1.01 and its exact source identity.
2. Reclassify Zstandard as a standard required codec.
3. Record the `AstrometricSolution:` namespace as raw Property support without
   claiming coordinate transformation semantics.
4. Update the old String Table Cell exception from “specification ambiguity”
   to an explicit legacy compatibility policy.
5. Add Revision 1 gates to the machine-checked readiness ledger.

### Explicit non-goals

- distributed XISF units or XISB files;
- external path or URL resolution and automatic network access;
- XML digital-signature verification;
- CIELab pixel conversion;
- execution of ICC color transforms or XISF display functions;
- a semantic astrometric solver or image/celestial coordinate-transform API;
- typed decoding of Table Cell data blocks;
- PFI adapter changes;
- viewer feature work.

## Work package R1-0: freeze the migration baseline

### Changes

1. Update `docs/specification-baseline.json` to document version 1.01 and the
   exact official source commit.
2. Recompute and record the official source-tree manifest and relevant PIDoc/XSD
   SHA-256 values without vendoring the specification.
3. Update `docs/SOURCES.md` with the Revision 1 announcement, HTML document,
   source commit, XSD, retrieval date, and clean-room statement.
4. Add a short derived requirements checklist mapping every Revision 1 change
   to `PASS_EXISTING`, `CODE_CHANGE`, `TEST_CHANGE`, `DOCUMENTATION_ONLY`, or
   `OUTSIDE_PROFILE`.
5. Introduce a Revision 1 qualification gate in
   `docs/production-readiness.json` as `NOT_TESTED` before code claims are
   changed. Preserve all RC1 evidence as historical evidence.

### Acceptance

- The exact official commit is reproducibly retrievable.
- Hashes are generated from a clean temporary checkout.
- The installed provenance artifact names document 1.01, not 1.00.
- Readiness cannot report current-specification production readiness while the
  new gate is not `PASS`.

## Work package R1-1: specification-derived codec fixtures

Create fixtures before changing the production decoder so the old behavior is
demonstrably caught.

### Required fixture matrix

- codecs: zlib, LZ4, LZ4HC-compatible, and Zstandard;
- item sizes: 2, 4, and 8 bytes;
- one-subblock and multi-subblock forms;
- subblock boundaries both aligned and not aligned to the shuffled item size;
- Planar Gray and RGB images;
- one vector and one matrix Property;
- little- and big-endian uncompressed values;
- SHA-1, SHA-256, SHA-512, SHA3-256, and SHA3-512 coverage distributed across
  the cases;
- deterministic source arrays with exact decoded-byte SHA-256 values.

The oracle must implement the Revision 1 transformation directly from the
normative description and must not call the production shuffle/unshuffle
helpers. At least one fixture must decode incorrectly with the RC1 algorithm.

### Negative fixtures

- compressed-size totals do not match the attached block;
- uncompressed subblock totals do not match the block size;
- invalid shuffle item size;
- truncated compressed frame;
- decompression ratio and Zstandard window limit violations;
- checksum mismatch before codec execution;
- trailing incomplete item bytes handled according to Revision 1;
- unknown codec keeps the document accessible and makes only the block
  unavailable.

### Acceptance

- Fixture provenance and expected bytes are reviewable without production-code
  reuse.
- RC1 fails at least the global-shuffle discriminator.
- All malformed cases fail with stable, intentional error categories.

## Work package R1-2: correct reader shuffle semantics

### Owning and caller-buffer reads

1. Treat decompressed subblocks as consecutive regions of one logical globally
   shuffled stream.
2. Map each byte from its global shuffled offset to the destination item and
   component byte. Do not restart item indexing at subblock boundaries.
3. Remove the requirement that each subblock's uncompressed size be divisible
   by the shuffle item size. Validate divisibility or trailing bytes against the
   complete logical block instead.
4. Preserve checksum-before-decompression, cancellation, codec window limits,
   decompression ratio limits, endian conversion, and Planar/Normal conversion.
5. Avoid a second full-frame allocation: decompress one bounded subblock and
   scatter its bytes into the caller/owning destination using global offsets.

### Row delivery

The standard does not require the custom low-copy row API, but the API must
never return corrupted data.

For RC2, use the following policy:

- keep bounded row delivery for uncompressed, unshuffled compressed, and
  single-subblock shuffled inputs;
- return an explicit `unsupported_feature` for globally shuffled
  multi-subblock row delivery unless a correct bounded algorithm is completed;
- keep owning and caller-buffer reads fully conforming for that combination;
- update the support profile and API documentation so this limitation is
  machine-visible and cannot be counted as `PASS` accidentally.

A later scratch-backed or indexed multi-pass row implementation may remove the
limitation, but it is not allowed to delay correct baseline image decoding or
silently allocate another full frame.

### RC1 compatibility

Do not guess whether a multi-subblock `+sh` file uses the old mmxisf behavior or
the Revision 1 behavior. The default must implement Revision 1.

During execution, first inventory whether any externally retained RC1 writer
outputs exist. If compatibility is necessary, add an explicit opt-in
`ReaderOptions` legacy mode that:

- is disabled by default;
- is never selected only from `CreatorApplication` text;
- is reported in decode provenance;
- has dedicated fixtures and cannot be confused with Revision 1 conformance.

Do not add a legacy writer mode.

### Acceptance

- All R1-1 positive fixtures reproduce exact bytes.
- Existing single-subblock and unshuffled fixtures remain byte-identical.
- Multi-subblock shuffled row reads fail explicitly unless correct.
- No checksum, memory, cancellation, or endian guarantee is weakened.

## Work package R1-3: correct writer shuffle semantics

1. Model byte shuffle as one logical transformation over the complete data
   block.
2. Divide the globally shuffled stream into compression subblocks only after
   transformation.
3. Generate shuffled chunks by global index mapping or bounded spool staging;
   do not require an additional full-frame RAM allocation.
4. Compress every subblock independently and concatenate the compressed frames.
5. Emit exact compressed/uncompressed subblock sizes and the total original
   uncompressed size.
6. Compute checksums over the serialized binary block as required by the
   specification.
7. Keep file and `ByteSink` output byte-identical for the same fixed inputs.
8. Preserve exclusive temporary-file creation, cancellation cleanup, short-write
   handling, deterministic output, and resource limits.

### Acceptance

- Every generated codec fixture is read exactly by the corrected reader.
- An independent Revision 1 oracle reconstructs the exact original bytes.
- Repeated file and sink writes are byte-identical.
- At least one multi-subblock `zstd+sh` image and Property open in current
  PixInsight with exact values.

## Work package R1-4: scalar, identifier, and empty-container rules

### Non-finite floating-point values

Reader and writer must accept the original forms `NaN`, `+Inf`, and `-Inf` plus
the Revision 1 alternatives `nan`, `-nan`, `inf`, and `-inf`. Do not silently
accept unrelated case variations or `+nan` unless the normative grammar permits
them.

Apply the same validation to scalar Properties, complex components, and Table
Cells that use these scalar types.

### Image identifiers

1. Validate every nonempty `Image/@id` against
   `[_a-zA-Z][_a-zA-Z0-9]*` in reader and writer.
2. Enforce case-sensitive uniqueness within the XISF unit.
3. Update existing tests that currently use invalid identifiers containing `&`
   or `-`.
4. Test absent IDs, valid underscores/digits, invalid starts, punctuation,
   duplicates, and case-sensitive distinct IDs.

This also closes a pre-existing writer validation gap that Revision 1 made more
visible; do not describe it as a newly introduced format incompatibility.

### Empty vectors and matrices

1. Accept `length="0"` vectors.
2. Accept matrices with zero rows or zero columns.
3. Require the empty value to use an inline data block with empty character
   contents.
4. Reject zero-length attachments, compressed empty blocks, nonempty encoded
   contents, checksums inconsistent with the decoded binary data, and extents
   whose declared zero shape conflicts with bytes.
5. Make the writer emit the mandatory empty inline representation without
   adding a new public API if the current `MetadataWriteEntry` can represent it
   unambiguously.

### Acceptance

- Reader and writer positive/negative matrices cover all rules above.
- No current nonempty Property behavior changes.
- Writer output validates against the official XSD where the XSD expresses the
  applicable constraint.

## Work package R1-5: extensibility and unsupported-object availability

1. Permit extension elements only as direct children of the XISF root in the
   strict supported profile.
2. Require extension elements to use a non-XISF namespace for schema-valid
   output; unknown root children in the XISF namespace remain unrecognized and
   ignored by the decoder as required.
3. Continue bounded namespace-aware inventory for accepted root extensions.
4. Reject or ignore nested extensions consistently; never treat their
   `location` attributes as trusted core data blocks.
5. Preserve unknown Property identifiers with supported declared types.
6. Preserve Properties with unknown future types as opaque descriptors instead
   of failing the complete document. Their values remain unavailable.
7. For Tables with an unknown future Field type, retain bounded structure and
   raw Cell forms but skip type-specific coercion; do not make unrelated images
   unavailable.
8. Keep the existing behavior for an unknown compression codec: opening the
   document succeeds, attempting to read the affected block returns
   `unsupported_feature`.

### Acceptance

- A unit with one supported and one unsupported image/object keeps the
  supported object readable.
- Unknown elements, attributes, and Properties do not cause unbounded storage
  or external I/O.
- Invalid mandatory container structure still fails closed.
- The conformance matrix no longer classifies an unknown codec as rejection of
  the entire XISF unit.

## Work package R1-6: metadata semantics introduced or clarified by Revision 1

### Standard astrometric namespace

1. Add a conformance row for `AstrometricSolution:` properties.
2. Test retention and raw block decoding of the normative String, Int32,
   Float64, I32Vector, F64Vector, and F64Matrix forms used by a minimal standard
   solution.
3. Preserve the namespace and `AstrometricSolution:Version` exactly.
4. Keep older `PCL:AstrometricSolution:` properties as independent opaque
   metadata.
5. Explicitly state that mmxisf does not evaluate projections, splines, or
   coordinate transformations in this release.

### RGB working space

1. Derive the expected luminance coefficients from the primaries and the D50
   reference white using the normative equations.
2. Define and document a deterministic numeric tolerance suitable for decimal
   serialization; test boundary and clearly invalid cases.
3. Keep `RGBWorkingSpace` inspect-only: do not execute a color conversion.
4. If robust semantic validation cannot be completed in RC2, downgrade the
   conformance row to structural inspection rather than retaining an overstated
   validation claim.

### Metadata properties

Add exact type/value tests for `XISF:BlockAlignmentSize`,
`XISF:ChecksumAlgorithms`, `XISF:MaxInlineBlockSize`, and `XISF:OutputHints`.
They remain optional. The writer should emit `XISF:ChecksumAlgorithms` when it
uses checksums and `XISF:BlockAlignmentSize` when it applies attachment
alignment, unless interoperability evidence identifies a reason to defer these
recommended declarations.

### Corrected legacy interpretations

- Keep String Table Cell `value` attributes only as a documented legacy input
  compatibility form. New writer output must use character data or a data
  block.
- Preserve geodetic Observation values and Equinox properties without semantic
  reinterpretation.
- Preserve FITS keyword values exactly; do not add padding.
- Keep display functions inspect-only, so corrected adaptive-display equations
  do not become an implicit image transform.

### Acceptance

- New metadata rows have explicit `SUPPORTED`, `INSPECT_ONLY`, or
  `OUTSIDE_PROFILE` disposition.
- No raw metadata is silently renamed from the legacy PCL astrometric namespace
  to the standard namespace.
- No scientific coordinate or color transformation is introduced accidentally.

## Work package R1-7: XSD and conformance audit

The XSD is an audit oracle, not a runtime dependency.

1. Hash and record the official XSD at the pinned specification commit.
2. Validate generated XML headers in a manual/development conformance gate using
   an available external schema validator.
3. Do not add a network fetch to normal configure, build, test, install, or
   runtime paths.
4. Do not vendor the XSD unless its required notices and redistribution terms
   are reviewed and preserved.
5. Add derived C++ tests for every rule relied on by the production parser so
   normal CI remains offline and deterministic.
6. Reconcile every current conformance-matrix section number and description
   with document 1.01.

### Acceptance

- All writer-generated headers used by the Revision 1 suite pass the official
  XSD, except documented constructs the XSD cannot express.
- Any intentional legacy-reader acceptance is clearly distinguished from
  conforming writer output.
- The installed package contains exact provenance and derived claims but no
  undeclared normative-source copy.

## Work package R1-8: regression, hardening, and interoperability

### Fast local gate during implementation

- warnings-as-errors static build;
- focused reader/writer/property tests;
- sanitizer test subset;
- package-consumer and public-API contracts when headers change;
- deterministic mutation smoke for the changed parser/codec paths.

### Full local qualification

- macOS arm64 static and shared builds;
- installed and relocated consumers;
- embedded consumer;
- ASan/UBSan and ThreadSanitizer where supported;
- reader/writer fuzz smoke with seeds covering global shuffle and empty inline
  Properties;
- deterministic source archive rehearsal;
- strict viewer signature gate only as a regression check, not a product claim.

### Linux qualification

Use the existing `mllse` Docker production gate for viewer-free static/shared
builds, sanitizers, consumers, exports, reproducibility, and fuzz smoke. Do not
leave unrelated source checkouts or build artifacts on the server.

### Windows qualification

Use the existing inexpensive MinGW/Wine compatibility gate during development.
Run native Windows/MSVC only once on the exact RC2 candidate, locally if a
supported host is available or through the manual GitHub workflow. Do not spend
hosted-runner minutes on routine intermediate commits.

### PixInsight black-box acceptance

At least one current PixInsight 1.9.5 file must exercise:

- Zstandard plus byte shuffle;
- multiple compression subblocks;
- Gray or RGB floating-point pixels with deterministic verification data;
- standard `AstrometricSolution:` metadata if PixInsight emits it;
- exact header provenance and file SHA-256.

If creating or verifying this fixture requires PixInsight UI interaction, stop
and provide the user with a short manual procedure. Do not use computer-use
automation. Record native evidence separately from synthetic and independent
fixtures.

### Existing corpus regression

- reopen and decode the retained uncompressed PFI Gray/RGB files;
- confirm that all 28 currently inventoried `/Users/matt/PixInsight/*.xisf`
  inputs still open within their declared profile;
- repeat exact pixel/metadata hashes for the retained PFI parity sources;
- do not modify PFI or count these checks as PFI operator acceptance.

## Work package R1-9: documentation, readiness, and RC2 preparation

1. Update README, API, architecture, resource-limit, threat-model, writer,
   row-reader, specification-note, source, roadmap, and milestone documents.
2. Replace every statement that calls Zstandard an out-of-spec PixInsight
   extension.
3. Describe global shuffle semantics and the row-reader limitation precisely.
4. Update the conformance matrix and support-profile hash.
5. Change the active profile from RC1 to a prepared RC2 identity only after all
   dispositions are reviewed.
6. Add exact Revision 1 evidence records and keep old RC1 evidence immutable.
7. Run the machine-checked readiness validator. Revision 1 readiness may become
   `READY` only when every new mandatory gate is `PASS`.
8. Perform a fresh public API/export review if any installed header changes.
9. Perform a fresh dependency/advisory and source-release security review.
10. Prepare, but do not publish, deterministic RC2 source assets and record
    their exact hashes.

### Release checkpoint requiring owner approval

Before any external write, present:

- repository and branch;
- clean candidate commit;
- proposed annotated tag `v0.1.0-rc.2`;
- full support-profile and readiness results;
- macOS, Linux, Windows, fuzz, XSD, and PixInsight evidence;
- exact asset names, byte sizes, and SHA-256 hashes;
- remaining `LIMITED` or `NOT_TESTED` rows;
- rollback procedure.

Push, tag creation, GitHub release creation, and asset publication require
explicit approval at that checkpoint. Never move `v0.1.0-rc.1`.

## Expected files affected during implementation

The exact diff must remain minimal, but the work is expected to touch:

- `src/reader.cpp`
- `src/writer.cpp`
- possibly `src/property_types.hpp`
- `include/mmxisf/reader.hpp` only if explicit legacy compatibility or
  availability reporting requires a public option
- reader, writer, independent-producer, package-consumer, fuzz, and contract
  tests
- `docs/specification-baseline.json`
- `docs/conformance/xisf-1.0-matrix.json`
- `docs/support-profile-0.1.0.json`
- `docs/production-readiness.json`
- the documentation listed in R1-9
- release notes for `v0.1.0-rc.2`

Avoid public-header changes for purely internal global-shuffle correction.

## Execution order for the next session

1. Re-read this plan and verify that official specification HEAD is still the
   pinned commit.
2. Confirm a clean mmXISF worktree and current remote state.
3. Execute R1-0 and add the fail-closed Revision 1 readiness gate.
4. Execute R1-1 and prove the existing global-shuffle defect with a deterministic
   fixture.
5. Implement and review R1-2 and R1-3 before any secondary metadata work.
6. Run focused tests and sanitizers.
7. Execute R1-4 through R1-7 in small reviewable commits.
8. Run local macOS qualification and the mllse Linux gate.
9. Obtain the PixInsight black-box fixture/manual verification if required.
10. Freeze the exact RC2 candidate only after the matrix and evidence are
    complete.
11. Run native Windows/MSVC once on that exact candidate.
12. Stop at the release checkpoint and request owner approval before push/tag/
    publication if approval is not already explicit for the exact checkpoint.

## Effort estimate with AI-assisted implementation

- R1-0/R1-1 provenance and discriminator fixtures: 0.5–1 focused day.
- R1-2/R1-3 codec correction and focused hardening: 1.5–3 focused days.
- R1-4 through R1-7 parser/writer/conformance corrections: 1–2 focused days.
- R1-8/R1-9 cross-platform, PixInsight, and candidate qualification: 1–2 days
  of engineering plus external build/runtime latency.

Expected total: 4–7 focused working days. A correct bounded multi-subblock
shuffled row-delivery implementation, if promoted into RC2 instead of remaining
explicitly unsupported, can add 1–3 days and should be treated as a separate
decision after the baseline reader/writer path is correct.

## Definition of done

Revision 1 work is complete only when:

- the exact document-1.01 baseline is pinned and installed as provenance;
- global shuffle/subblock reader and writer fixtures pass for all standard
  codecs;
- no affected path can silently return per-subblock-unshuffled pixels;
- image IDs, non-finite values, empty vectors/matrices, extensions, and
  unsupported objects follow the new rules;
- Zstandard and baseline checksums are classified normatively;
- standard astrometric properties are retained without overstated semantics;
- generated headers pass the XSD audit;
- existing corpus and package/API/security tests pass;
- macOS, Linux, and native Windows candidate evidence is bound to one exact
  commit;
- PixInsight black-box acceptance covers the corrected multi-subblock shuffled
  path;
- the readiness ledger derives Revision 1 standalone readiness without manual
  overrides;
- no PFI or viewer claim has been broadened;
- publication remains unperformed until the explicit release checkpoint.
