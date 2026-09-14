# Bounded image-row reader

`Reader::read_image_rows` is the low-copy path for sequential analysis. It
delivers one planar channel row at a time without allocating the complete
decoded image.

## Minimal consumer

```cpp
class Analyzer final : public mmxisf::ImageRowSink {
public:
  mmxisf::Result<void>
  consume(const mmxisf::ImageRowView& row) override {
    // row.bytes is valid only during this call.
    analyze(row.channel_index, row.row_index, row.bytes);
    return {};
  }
};

Analyzer analyzer;
mmxisf::ImageRowReadOptions options;
options.byte_order = mmxisf::ByteOrderOutput::native;
auto result = reader.read_image_rows(image_index, analyzer, options);
if (!result) {
  analyzer.rollback_partial_image();
}
```

Use the selected `Document::images()[image_index]` descriptor for width, height,
sample format, channel order, traversal, bounds, and optional display
orientation. Row delivery changes neither numeric type nor precision and never
applies display orientation.

## Delivery order and shape

Each callback receives exactly `width * bytes_per_sample` bytes for one channel
and one row. For a Planar source, callbacks are channel-major: every row of
channel zero, then every row of channel one, and so on. For a Normal/interleaved
source, callbacks are source-row-major and channel-minor. Exact zero-based
`channel_index` and `row_index` fields make the order explicit.

The callback span is ephemeral and is reused after `consume` returns. Copy it
only when the consumer needs longer ownership.

## Integrity and partial results

When a checksum is declared, a bounded first pass verifies it before the first
callback. The exact serialized delivery pass is hashed again before success, so
source mutation cannot silently detach the summary from the delivered rows.

Rows remain provisional until `ImageRowReadSummary` is returned. A later codec,
source, mutation, cancellation, or sink failure can leave consumer-owned rows
already accepted. The library cannot roll those back; the consumer must stage
or invalidate partial analysis. An initial checksum mismatch and all geometry
or staging-limit failures occur before any callback.

## Memory bounds

`ImageRowReadOptions::max_row_bytes` independently caps the source row and the
planar output row. `max_subblock_bytes` caps each compressed and decoded
subblock staging buffer. Byte-shuffled input can require a compressed buffer, a
decoded buffer, a shuffle buffer, and row buffers simultaneously, so the
subblock value is not a total peak-memory ceiling. Checksum preverification uses
at most one additional fixed 8 MiB buffer.
