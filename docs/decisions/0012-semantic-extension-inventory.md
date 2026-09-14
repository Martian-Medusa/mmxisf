# ADR 0012: bounded semantic extension inventory

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

XISF permits non-core XML elements in namespaces other than the XISF
namespace. Silently discarding them prevents an inspector or integrating
application from reporting that producer-specific information exists. Storing
raw XML, however, would imply lexical round-trip fidelity, retain namespace
prefix and entity spelling details that Expat intentionally normalizes, and
create a second potentially large copy of untrusted header data.

## Decision

`Document::extension_elements()` exposes a document-order semantic inventory
of non-XISF-namespace elements. Each record stores:

- namespace URI and local name;
- immediate parent namespace URI and local name;
- an optional index when the immediate parent is also an inventoried extension;
- the containing image index, when applicable;
- namespace-aware attributes and direct character data.

Namespace declarations are not attributes. XML prefixes, attribute quote and
order significance, entity spelling, comments, processing instructions, and
the distinction between ordinary text and CDATA are outside the contract.
Parent text excludes the text of nested elements. Consequently, this model is
appropriate for inspection and policy decisions, not byte-identical XML
reconstruction.

Construction is bounded independently by maximum extension element count,
cumulative extension attribute count, and cumulative copied semantic string
bytes. The existing XML header, depth, node, and per-element attribute limits
remain authoritative. Standard attachment descriptors on extension elements
continue to participate in file-range and zero-unused-space validation, but
the library does not decode or interpret those extension payloads.

## Consequences

Consumers can disclose extension presence and inspect normalized content
without depending on Expat or raw XML. Unknown extensions remain non-executable
data, do not trigger network access, and do not silently alter scientific pixel
semantics. A future writer API for extensions requires a separate design and
must not claim round-trip preservation from this inventory alone.
