# Threat model

## Security objective

Opening or inspecting an untrusted XISF file must not allow memory corruption,
unbounded allocation, excessive CPU consumption, filesystem/network access,
silent data substitution, or false integrity/authenticity claims. Failure must
be bounded, structured, and safe for unattended PFI batch processing.

## Trust boundaries

- File bytes, XML, offsets, sizes, dimensions, encoded text, compressed streams,
  checksums, paths, URLs, identifiers, and metadata are attacker-controlled.
- Codec and XML libraries are third-party code inside the process boundary.
- The PFI adapter trusts only validated library output, never raw metadata.
- A checksum establishes accidental/tamper detection under the declared
  algorithm. SHA-1 support required by XISF does not establish modern
  authenticity.
- XML signatures are unverified in the initial profile. Their presence is data,
  not proof.

## Threats and controls

| Threat | Required control | Verification |
| --- | --- | --- |
| Truncated/forged preamble or header length | Checked reads; maximum header budget; exact signature/version | boundary and truncation tests |
| Integer overflow in geometry or offsets | Checked 64-bit add/multiply before narrowing or allocation | property tests and sanitizers |
| Attachment outside file or overlapping header | Validate every closed-open range against file size and protected header range | generated negative fixtures |
| Aliased/overlapping blocks | Permit only when semantics allow identical ranges; otherwise diagnose before reads | overlap matrix tests |
| Compression bomb | Declared output-size cap, cumulative decoded-byte cap, codec output bound, cancellation | adversarial compressed corpus |
| Corrupt compressed input | Verify checksum on serialized compressed bytes before codec invocation when present | order-of-operation test |
| Streaming consumer treats a partial image as valid | Check declared checksum before callbacks; make row spans ephemeral; return success summary only after exact full geometry; require caller rollback on any error | checksum, sink-failure, cancellation, subblock, and extent tests |
| Malicious byte-shuffle parameters | Validate item size, divisibility, subblock totals and output length | fuzz target |
| Deep/wide XML | Header, depth, node, per-element attribute, metadata text, and cumulative extension record/string budgets | limit tests |
| DTD/entity expansion or XXE | Reject DOCTYPE; no external entity resolver; no network-capable XML callbacks | hostile XML corpus |
| Invalid UTF-8 or permissive XML edge cases | Strict well-formedness and UTF-8 validation; reject duplicate attributes/multiple roots | differential XML tests |
| Extension-driven memory growth or hidden behavior | Semantic-only immutable inventory with independent element/attribute/byte limits; no extension execution or resolution | nested extension and zero/tiny-limit tests |
| Ancillary-object ambiguity or resource amplification | Required syntax/placement validation, document-local reference resolution, independent object/attribute/binding/byte limits, and no synthesized defaults or pixel transforms | positive, malformed, reference, and tiny-limit tests |
| Malformed or oversized ICC profile | Dedicated descriptor/binding/serialized/decoded limits, checksum-before-decompression, no external resolution or color execution, and bounded ICC header screening | attachment/inline, malformed header, reference, and limit tests |
| Thumbnail confused with scientific pixels or used for resource amplification | Separate model/API, restricted formats/channels, dedicated count/binding/dimension/byte limits, and no implicit transforms | synthetic grammar/limit tests plus native raw-range hash |
| Heterogeneous Table amplification, malformed schemas, or external Cell resolution | Dedicated structure/table/field/row/cell/binding/text limits, exact cardinality and type-form validation, document-local Structure resolution, descriptor-only Cell blocks, and no external I/O | positive, malformed, reference, uniqueness, and zero/tiny-limit tests |
| Path traversal, symlink escape or SSRF | External path/URL locations rejected in the initial profile; future resolver is explicit opt-in | API and negative tests |
| File changed during parsing | Stable handle, initial identity/size snapshot, optional final identity check | mutation test |
| Non-finite or invalid numeric metadata | Grammar-aware parsing and explicit invalid state; no default substitution | value corpus |
| Diagnostic memory/data disclosure | Bounded messages; offsets and identifiers only; no large payload echo | diagnostic tests |
| Codec/parser supply-chain compromise | Pin releases/hashes, SBOM, license audit, update policy, CI scanners | release gate |
| Data race/global state | Immutable document model and per-operation state | thread sanitizer and concurrency tests |

## Security-sensitive invariants

1. No allocation is derived from file data before the value and cumulative
   budget have been checked.
2. No compressed block with a declared failed checksum reaches a decoder.
3. No external location causes filesystem or network access in the initial
   profile.
4. Unsupported constructs remain observable and are rejected explicitly; they
   are never treated as empty or zero.
5. Owning reads expose pixels only after the complete geometry, storage model,
   sample format, byte order, block length, decompression, unshuffle, and
   checksum requirements agree. Streaming rows are provisional: each emitted
   row has passed its local decode/transform checks and any declared checksum
   has passed globally, but the image becomes trusted only when the final
   `ImageRowReadSummary` is returned successfully.

## Deferred security work

Distributed units, remote retrieval, XML digital signatures, credential
handling, and persistent caches require separate threat models. They cannot be
enabled by a convenience flag added to the initial reader.
