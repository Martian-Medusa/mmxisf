# ADR 0003: strict streaming XML boundary

- Status: Accepted; validated in M1 and independent-producer follow-up
- Date: 2026-09-13

## Context

XISF headers are XML 1.0 UTF-8 and may contain large embedded blocks. A DOM-only
parser duplicates memory and can make depth/node budgets difficult to enforce.
The leading initial candidate, pugixml, is small and MIT-licensed but its own
manual documents non-validating behavior including acceptance of duplicate
attributes, some invalid names/references, invalid UTF sequences, and multiple
top-level elements. Those behaviors conflict with strict untrusted-input goals
unless an additional validator is written.

## Decision

Use a SAX/push parser behind a private `XmlEventReader` boundary. Expat is the
leading M1 spike candidate because it is portable, permissively licensed,
strict about XML well-formedness, namespace-aware, and does not require a DOM.

The adapter must reject DOCTYPE before processing declarations, install no
external entity resolver, validate UTF-8, reject duplicate attributes and
multiple roots, and enforce byte/depth/node/attribute/text limits. The domain
builder, not the XML library, enforces XISF grammar and reference resolution.
The XML declaration callback validates semantic XML 1.0/UTF-8 requirements
rather than relying on one literal quote/case spelling; SN-004 records the
narrow external-writer compatibility form.

## Consequences

Large embedded Base64/hex data can be decoded incrementally and third-party XML
types do not leak into the public ABI. M1 must test actual Expat behavior and
dependency packaging before this ADR becomes Accepted. If it fails, compare a
strict libxml2 reader; do not fall back to permissive parsing silently.
