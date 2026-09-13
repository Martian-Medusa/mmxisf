# Specification interpretation notes

These notes record ambiguities or cross-section tensions in the pinned XISF 1.0
specification. They do not modify the specification.

## SN-001: inline pixel-block wording

- Sections: 7.2, 10.3, 11.5
- Status: conservative interpretation adopted for planning

Section 7.2 says a baseline decoder reads pixel data from blocks with inline,
embedded, and attachment locations. Section 11.5 says an `Image` element cannot
serialize pixel data as an inline block because `Image` can have child elements;
embedded is the permitted XML-resident form. Section 10.3 defines inline blocks
for elements without children, such as properties.

Interpretation:

- reject a direct `Image location="inline:..."` as invalid;
- support `Image location="embedded"` and attachment for pixels;
- support inline generic/property data blocks before claiming the complete
  baseline decoder profile;
- retain a test proving that invalid direct inline images do not become an
  accepted extension accidentally.

This resolves the planning matrix without weakening either the Image grammar or
the baseline decoder's generic block-location coverage. A future first-party
erratum overrides this interpretation.

## SN-002: SHA-1 meaning

- Section: 10.5
- Status: implementation rule

The specification requires SHA-1 support for an implementation claiming data
block checksum support. `mmxisf` will implement it for format compatibility but
will label it legacy integrity evidence. It does not provide an authenticity or
modern collision-resistance claim. Stronger supported digest declarations are
verified when present.
