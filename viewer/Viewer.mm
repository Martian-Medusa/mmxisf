// SPDX-License-Identifier: Apache-2.0

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "mmxisf/reader.hpp"
#include "Preview.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

NSString *ns_string(const std::string &value) {
  NSString *result = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
  return result != nil ? result : @"<invalid UTF-8>";
}

NSString *display_string(const std::string &value) {
  constexpr std::size_t kPreviewLimit = 16U * 1024U;
  if (value.size() <= kPreviewLimit) {
    return ns_string(value);
  }
  std::string preview = value.substr(0, kPreviewLimit);
  preview += "\n… [viewer truncated ";
  preview += std::to_string(value.size() - kPreviewLimit);
  preview += " bytes]";
  return ns_string(preview);
}

NSString *expanded_name(const std::string &namespace_uri,
                        const std::string &name) {
  if (namespace_uri.empty()) {
    return ns_string(name);
  }
  return [NSString
      stringWithFormat:@"{%@}%@", ns_string(namespace_uri), ns_string(name)];
}

NSString *geometry_string(const mmxisf::ImageInfo &image) {
  NSMutableArray<NSString *> *axes = [NSMutableArray array];
  for (const auto axis : image.geometry) {
    [axes addObject:[NSString stringWithFormat:@"%llu", axis]];
  }
  return [axes componentsJoinedByString:@" × "];
}

} // namespace

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTableViewDataSource,
                                   NSTableViewDelegate> {
@private
  NSWindow *window_;
  NSImageView *image_view_;
  NSTableView *metadata_table_;
  NSTextField *metadata_title_label_;
  NSTextField *status_label_;
  NSTextField *stretch_value_label_;
  NSSlider *stretch_slider_;
  NSArray<NSDictionary<NSString *, NSString *> *> *rows_;
  NSString *pending_path_;
  std::unique_ptr<mmxisf::Reader> reader_;
  std::optional<mmxisf::RawImage> raw_image_;
  mmxisf::viewer::StretchRange stretch_range_;
}
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  [self buildMenu];
  [self buildWindow];
  [window_ makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  NSArray<NSString *> *arguments = NSProcessInfo.processInfo.arguments;
  if (pending_path_) {
    NSString *path = pending_path_;
    pending_path_ = nil;
    [self loadURL:[NSURL fileURLWithPath:path]];
  } else if (arguments.count > 1 && [arguments[1] hasSuffix:@".xisf"]) {
    [self loadURL:[NSURL fileURLWithPath:arguments[1]]];
  }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
  (void)sender;
  return YES;
}

- (void)application:(NSApplication *)sender
          openFiles:(NSArray<NSString *> *)filenames {
  (void)sender;
  if (filenames.count > 0) {
    if (window_) {
      [self loadURL:[NSURL fileURLWithPath:filenames.firstObject]];
    } else {
      pending_path_ = [filenames.firstObject copy];
    }
    [NSApp replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
  } else {
    [NSApp replyToOpenOrPrint:NSApplicationDelegateReplyFailure];
  }
}

- (void)buildMenu {
  NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
  NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@""
                                                   action:nil
                                            keyEquivalent:@""];
  [mainMenu addItem:appItem];
  NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"mmXISF Viewer PoC"];
  [appMenu addItemWithTitle:@"Quit mmXISF Viewer PoC"
                     action:@selector(terminate:)
              keyEquivalent:@"q"];
  appItem.submenu = appMenu;
  NSApp.mainMenu = mainMenu;
}

- (void)buildWindow {
  window_ = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(100, 100, 1440, 880)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window_.title = @"mmXISF Viewer PoC";
  window_.minSize = NSMakeSize(900, 560);

  NSView *content = window_.contentView;
  NSButton *openButton = [NSButton buttonWithTitle:@"Open XISF…"
                                            target:self
                                            action:@selector(openDocument:)];
  NSButton *autoButton = [NSButton buttonWithTitle:@"Auto Stretch"
                                            target:self
                                            action:@selector(autoStretch:)];
  NSTextField *stretchLabel = [NSTextField labelWithString:@"Stretch"];
  stretch_slider_ = [NSSlider sliderWithValue:100.0
                                     minValue:0.0
                                     maxValue:100.0
                                       target:self
                                       action:@selector(stretchChanged:)];
  stretch_slider_.continuous = NO;
  [stretch_slider_
      setContentHuggingPriority:NSLayoutPriorityDefaultLow
                 forOrientation:NSLayoutConstraintOrientationHorizontal];
  stretch_value_label_ = [NSTextField labelWithString:@"100%"];
  stretch_value_label_.alignment = NSTextAlignmentRight;

  NSStackView *toolbar = [NSStackView stackViewWithViews:@[
    openButton, autoButton, stretchLabel, stretch_slider_, stretch_value_label_
  ]];
  toolbar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  toolbar.spacing = 10.0;
  toolbar.translatesAutoresizingMaskIntoConstraints = NO;

  image_view_ = [[NSImageView alloc] initWithFrame:NSZeroRect];
  image_view_.imageScaling = NSImageScaleProportionallyUpOrDown;
  image_view_.imageAlignment = NSImageAlignCenter;
  image_view_.wantsLayer = YES;
  image_view_.layer.backgroundColor = NSColor.blackColor.CGColor;

  NSScrollView *imageScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  imageScroll.hasVerticalScroller = YES;
  imageScroll.hasHorizontalScroller = YES;
  imageScroll.autohidesScrollers = YES;
  imageScroll.documentView = image_view_;
  image_view_.frame = imageScroll.contentView.bounds;
  image_view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  metadata_table_ =
      [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 900, 600)];
  NSArray<NSArray *> *columns = @[
    @[ @"scope", @"Scope", @80 ], @[ @"kind", @"Kind", @75 ],
    @[ @"name", @"Name", @190 ], @[ @"type", @"Type", @90 ],
    @[ @"value", @"Value", @240 ], @[ @"comment", @"Comment", @220 ]
  ];
  for (NSArray *definition in columns) {
    NSTableColumn *column =
        [[NSTableColumn alloc] initWithIdentifier:definition[0]];
    column.title = definition[1];
    column.width = [definition[2] doubleValue];
    column.minWidth = 55.0;
    [metadata_table_ addTableColumn:column];
  }
  metadata_table_.dataSource = self;
  metadata_table_.delegate = self;
  metadata_table_.usesAlternatingRowBackgroundColors = YES;
  metadata_table_.rowHeight = 22.0;
  metadata_table_.headerView =
      [[NSTableHeaderView alloc] initWithFrame:NSMakeRect(0, 0, 900, 24)];
  metadata_table_.columnAutoresizingStyle =
      NSTableViewUniformColumnAutoresizingStyle;
  NSScrollView *metadataScroll =
      [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 480, 600)];
  metadataScroll.hasVerticalScroller = YES;
  metadataScroll.hasHorizontalScroller = YES;
  metadataScroll.autohidesScrollers = YES;
  metadataScroll.borderType = NSBezelBorder;
  metadataScroll.documentView = metadata_table_;

  metadata_title_label_ =
      [NSTextField labelWithString:@"Metadata — open an XISF file"];
  metadata_title_label_.font =
      [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
  metadata_title_label_.textColor = NSColor.labelColor;
  metadata_title_label_.translatesAutoresizingMaskIntoConstraints = NO;
  metadataScroll.translatesAutoresizingMaskIntoConstraints = NO;

  NSView *metadataPane =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 600)];
  [metadataPane addSubview:metadata_title_label_];
  [metadataPane addSubview:metadataScroll];
  [NSLayoutConstraint activateConstraints:@[
    [metadata_title_label_.leadingAnchor
        constraintEqualToAnchor:metadataPane.leadingAnchor
                       constant:10],
    [metadata_title_label_.trailingAnchor
        constraintEqualToAnchor:metadataPane.trailingAnchor
                       constant:-10],
    [metadata_title_label_.topAnchor
        constraintEqualToAnchor:metadataPane.topAnchor
                       constant:8],
    [metadataScroll.leadingAnchor
        constraintEqualToAnchor:metadataPane.leadingAnchor
                       constant:8],
    [metadataScroll.trailingAnchor
        constraintEqualToAnchor:metadataPane.trailingAnchor
                       constant:-8],
    [metadataScroll.topAnchor
        constraintEqualToAnchor:metadata_title_label_.bottomAnchor
                       constant:8],
    [metadataScroll.bottomAnchor
        constraintEqualToAnchor:metadataPane.bottomAnchor
                       constant:-8]
  ]];

  NSSplitView *split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
  split.vertical = YES;
  split.dividerStyle = NSSplitViewDividerStyleThin;
  [split addSubview:imageScroll];
  [split addSubview:metadataPane];
  [split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:1];
  split.translatesAutoresizingMaskIntoConstraints = NO;

  status_label_ =
      [NSTextField labelWithString:@"Open an uncompressed Gray or RGB XISF "
                                   @"attachment."];
  status_label_.lineBreakMode = NSLineBreakByTruncatingMiddle;
  status_label_.textColor = NSColor.secondaryLabelColor;
  status_label_.translatesAutoresizingMaskIntoConstraints = NO;

  [content addSubview:toolbar];
  [content addSubview:split];
  [content addSubview:status_label_];
  [NSLayoutConstraint activateConstraints:@[
    [toolbar.leadingAnchor constraintEqualToAnchor:content.leadingAnchor
                                          constant:12],
    [toolbar.trailingAnchor constraintEqualToAnchor:content.trailingAnchor
                                           constant:-12],
    [toolbar.topAnchor constraintEqualToAnchor:content.topAnchor constant:10],
    [stretch_slider_.widthAnchor constraintGreaterThanOrEqualToConstant:180],
    [stretch_value_label_.widthAnchor constraintEqualToConstant:42],
    [split.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [split.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [split.topAnchor constraintEqualToAnchor:toolbar.bottomAnchor constant:10],
    [split.bottomAnchor constraintEqualToAnchor:status_label_.topAnchor
                                       constant:-8],
    [status_label_.leadingAnchor constraintEqualToAnchor:content.leadingAnchor
                                                constant:12],
    [status_label_.trailingAnchor constraintEqualToAnchor:content.trailingAnchor
                                                 constant:-12],
    [status_label_.bottomAnchor constraintEqualToAnchor:content.bottomAnchor
                                               constant:-8],
    [status_label_.heightAnchor constraintEqualToConstant:20]
  ]];
  [content layoutSubtreeIfNeeded];
  [split setPosition:NSWidth(split.bounds) * 0.64 ofDividerAtIndex:0];
}

- (void)openDocument:(id)sender {
  (void)sender;
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  panel.allowsMultipleSelection = NO;
  panel.canChooseDirectories = NO;
  UTType *xisfType = [UTType typeWithFilenameExtension:@"xisf"];
  if (xisfType) {
    panel.allowedContentTypes = @[ xisfType ];
  }
  [panel beginSheetModalForWindow:window_
                completionHandler:^(NSModalResponse result) {
                  if (result == NSModalResponseOK) {
                    [self loadURL:panel.URL];
                  }
                }];
}

- (void)autoStretch:(id)sender {
  (void)sender;
  stretch_slider_.doubleValue = 100.0;
  [self stretchChanged:stretch_slider_];
}

- (void)stretchChanged:(id)sender {
  (void)sender;
  stretch_value_label_.stringValue =
      [NSString stringWithFormat:@"%.0f%%", stretch_slider_.doubleValue];
  [self renderImage];
}

- (void)loadURL:(NSURL *)url {
  status_label_.stringValue =
      [NSString stringWithFormat:@"Opening %@…", url.path];
  [window_ displayIfNeeded];
  auto result = mmxisf::Reader::open_file(
      std::filesystem::path(url.fileSystemRepresentation));
  if (!result) {
    [self showError:result.error()];
    return;
  }
  reader_ = std::make_unique<mmxisf::Reader>(std::move(result).value());
  [self rebuildMetadataRowsForPath:url.path];
  [metadata_table_ reloadData];
  metadata_title_label_.stringValue =
      [NSString stringWithFormat:@"Metadata — %lu rows", rows_.count];
  if (reader_->document().images().empty()) {
    raw_image_.reset();
    image_view_.image = nil;
    status_label_.stringValue =
        @"Header parsed; the document has no Image elements.";
    return;
  }
  const auto &info = reader_->document().images().front();
  if (info.color_space != "Gray" && info.color_space != "RGB") {
    [self showMessage:@"Preview unavailable"
               detail:@"The M2 viewer currently renders Gray and RGB images. "
                      @"CIELab metadata is available, but conversion is not."];
    return;
  }
  if (info.sample_format == mmxisf::SampleFormat::complex32 ||
      info.sample_format == mmxisf::SampleFormat::complex64) {
    [self showMessage:@"Preview unavailable"
               detail:@"Complex samples can be read without loss, but this "
                      @"PoC does not map complex values to display intensity. "
                      @"Metadata is available."];
    return;
  }
  auto image = reader_->read_image(0);
  if (!image) {
    [self showError:image.error()];
    return;
  }
  const bool valid_channels =
      (info.color_space == "Gray" && image.value().channels >= 1) ||
      (info.color_space == "RGB" && image.value().channels >= 3);
  if (!valid_channels) {
    [self showMessage:@"Preview unavailable"
               detail:@"The channel count is incompatible with the declared "
                      @"color space. Metadata is available."];
    return;
  }
  raw_image_ = std::move(image).value();
  stretch_range_ = mmxisf::viewer::calculate_stretch_range(*raw_image_);
  stretch_slider_.doubleValue = 100.0;
  stretch_value_label_.stringValue = @"100%";
  [self renderImage];
  window_.title = [NSString
      stringWithFormat:@"%@ — mmXISF Viewer PoC", url.lastPathComponent];
  status_label_.stringValue = [NSString
      stringWithFormat:@"%@  •  %llu × %llu  •  %s  •  %lu metadata entries",
                       url.path, raw_image_->width, raw_image_->height,
                       mmxisf::to_string(raw_image_->sample_format),
                       reader_->document().metadata().size()];
}

- (void)renderImage {
  if (!raw_image_) {
    return;
  }
  const auto width = static_cast<NSInteger>(raw_image_->width);
  const auto height = static_cast<NSInteger>(raw_image_->height);
  const bool is_rgb = raw_image_->channels >= 3;
  const NSInteger preview_channels = is_rgb ? 3 : 1;
  NSBitmapImageRep *bitmap =
      [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                              pixelsWide:width
                                              pixelsHigh:height
                                           bitsPerSample:8
                                         samplesPerPixel:preview_channels
                                                hasAlpha:NO
                                                isPlanar:NO
                                          colorSpaceName:is_rgb
                                                             ? NSDeviceRGBColorSpace
                                                             : NSDeviceWhiteColorSpace
                                            bitmapFormat:0
                                             bytesPerRow:width * preview_channels
                                            bitsPerPixel:8 * preview_channels];
  if (!bitmap) {
    [self showMessage:@"Preview unavailable"
               detail:@"Unable to allocate the 8-bit preview buffer."];
    return;
  }
  const double strength = stretch_slider_.doubleValue / 100.0;
  const double low =
      stretch_range_.linear_low +
      (stretch_range_.auto_low - stretch_range_.linear_low) * strength;
  const double high =
      stretch_range_.linear_high +
      (stretch_range_.auto_high - stretch_range_.linear_high) * strength;
  const double span =
      std::max(high - low, std::numeric_limits<double>::epsilon());
  const double gamma = 1.0 - 0.75 * strength;
  unsigned char *output = bitmap.bitmapData;
  const std::uint64_t count = raw_image_->width * raw_image_->height;
  for (std::uint64_t index = 0; index < count; ++index) {
    for (std::uint64_t channel = 0;
         channel < static_cast<std::uint64_t>(preview_channels); ++channel) {
      double normalized =
          (mmxisf::viewer::sample_value(*raw_image_, index, channel) - low) /
          span;
      if (!std::isfinite(normalized)) {
        normalized = 0.0;
      }
      normalized = std::clamp(normalized, 0.0, 1.0);
      output[index * static_cast<std::uint64_t>(preview_channels) + channel] =
          static_cast<unsigned char>(
              std::lround(std::pow(normalized, gamma) * 255.0));
    }
  }
  NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
  [image addRepresentation:bitmap];
  image_view_.image = image;
}

- (void)rebuildMetadataRowsForPath:(NSString *)path {
  NSMutableArray<NSDictionary<NSString *, NSString *> *> *rows =
      [NSMutableArray array];
  const auto add = ^(NSString *scope, NSString *kind, NSString *name,
                     NSString *type, NSString *value, NSString *comment) {
    [rows addObject:@{
      @"scope" : scope != nil ? scope : @"",
      @"kind" : kind != nil ? kind : @"",
      @"name" : name != nil ? name : @"",
      @"type" : type != nil ? type : @"",
      @"value" : value != nil ? value : @"",
      @"comment" : comment != nil ? comment : @""
    }];
  };
  const auto &document = reader_->document();
  add(@"Document", @"Summary", @"File", @"Path", path, @"");
  add(@"Document", @"Summary", @"XISF version", @"String",
      ns_string(document.version()), @"");
  add(@"Document", @"Summary", @"Header bytes", @"UInt32",
      [NSString stringWithFormat:@"%u", document.header_length()], @"");
  add(@"Document", @"Summary", @"Images", @"Count",
      [NSString stringWithFormat:@"%lu", document.images().size()], @"");
  add(@"Document", @"Summary", @"Extensions", @"Count",
      [NSString stringWithFormat:@"%lu", document.extension_elements().size()],
      @"");
  add(@"Document", @"Summary", @"Ancillary objects", @"Count",
      [NSString stringWithFormat:@"%lu", document.ancillary_objects().size()],
      @"");
  add(@"Document", @"Summary", @"ICC profiles", @"Count",
      [NSString stringWithFormat:@"%lu", document.icc_profiles().size()], @"");
  add(@"Document", @"Summary", @"Thumbnails", @"Count",
      [NSString stringWithFormat:@"%lu", document.thumbnails().size()], @"");
  add(@"Document", @"Summary", @"Table structures", @"Count",
      [NSString stringWithFormat:@"%lu", document.table_structures().size()],
      @"");
  add(@"Document", @"Summary", @"Tables", @"Count",
      [NSString stringWithFormat:@"%lu", document.tables().size()], @"");
  for (std::size_t index = 0; index < document.images().size(); ++index) {
    const auto &image = document.images()[index];
    NSString *scope = [NSString stringWithFormat:@"Image %lu", index];
    add(scope, @"Image", @"id", @"String", ns_string(image.id), @"");
    add(scope, @"Image", @"geometry", @"Axes", geometry_string(image), @"");
    add(scope, @"Image", @"sampleFormat", @"Enum",
        ns_string(image.sample_format_name), @"");
    add(scope, @"Image", @"colorSpace", @"Enum", ns_string(image.color_space),
        @"");
    add(scope, @"Image", @"nominalChannelOrder", @"Sequence",
        [NSString stringWithUTF8String:mmxisf::to_string(
                                           image.nominal_channel_order)],
        @"");
    add(scope, @"Image", @"pixelOrigin", @"Coordinate convention",
        [NSString stringWithUTF8String:mmxisf::to_string(image.pixel_origin)],
        @"");
    add(scope, @"Image", @"pixelTraversal", @"Coordinate convention",
        [NSString stringWithUTF8String:mmxisf::to_string(
                                           image.pixel_traversal)],
        @"");
    add(scope, @"Image", @"orientation", @"Optional display transform",
        image.orientation
            ? [NSString
                  stringWithUTF8String:mmxisf::to_string(*image.orientation)]
            : @"Unavailable (not declared)",
        @"Never applied to scientific pixel decoding");
    if (image.lower_bound && image.upper_bound) {
      add(scope, @"Image", @"bounds", @"Range",
          [NSString stringWithFormat:@"%.17g : %.17g", *image.lower_bound,
                                     *image.upper_bound],
          @"");
    }
    add(scope, @"Image", @"pixelStorage", @"Enum",
        [NSString stringWithUTF8String:mmxisf::to_string(image.pixel_storage)],
        @"");
    add(scope, @"Image", @"byteOrder", @"Enum",
        [NSString stringWithUTF8String:mmxisf::to_string(image.byte_order)],
        @"");
    add(scope, @"Image", @"location", @"Block", ns_string(image.block.raw),
        @"");
    if (!image.compression.empty()) {
      add(scope, @"Image", @"compression", @"Codec",
          ns_string(image.compression), @"");
    }
    if (!image.checksum.empty()) {
      add(scope, @"Image", @"checksum", @"Digest", ns_string(image.checksum),
          @"Verified when pixels are decoded");
    }
  }
  const auto add_metadata = [&](const mmxisf::MetadataEntry &entry,
                                NSString *scope, bool by_reference) {
    NSString *type = ns_string(entry.type);
    if (entry.kind == mmxisf::MetadataEntry::Kind::property) {
      type = [NSString
          stringWithFormat:@"%@ · %s", type,
                           mmxisf::to_string(entry.value_form)];
    }
    NSString *value = entry.value_form ==
                              mmxisf::MetadataEntry::ValueForm::data_block
                          ? ns_string(entry.block.raw)
                          : display_string(entry.value);
    NSString *kind =
        entry.kind == mmxisf::MetadataEntry::Kind::property ? @"Property"
                                                            : @"FITS";
    if (by_reference) {
      kind = [kind stringByAppendingString:@" · Reference"];
    }
    add(scope, kind,
        ns_string(entry.name), type, value, ns_string(entry.comment));
  };
  std::vector<bool> bound_metadata(document.metadata().size(), false);
  for (const auto &binding : document.metadata_bindings()) {
    if (binding.metadata_index >= document.metadata().size()) {
      continue;
    }
    NSString *scope = @"XISF unit";
    if (binding.scope == mmxisf::MetadataBinding::Scope::image &&
        binding.image_index) {
      scope = [NSString stringWithFormat:@"Image %lu", *binding.image_index];
    }
    add_metadata(document.metadata()[binding.metadata_index], scope,
                 binding.by_reference);
    bound_metadata[binding.metadata_index] = true;
  }
  for (std::size_t index = 0; index < document.metadata().size(); ++index) {
    if (bound_metadata[index]) {
      continue;
    }
    const auto &entry = document.metadata()[index];
    NSString *scope = @"Standalone";
    if (entry.scope == mmxisf::MetadataEntry::Scope::xisf_unit) {
      scope = @"XISF unit";
    } else if (entry.scope == mmxisf::MetadataEntry::Scope::image &&
               entry.image_index) {
      scope = [NSString stringWithFormat:@"Image %lu", *entry.image_index];
    }
    add_metadata(entry, scope, false);
  }
  const auto add_ancillary = [&](const mmxisf::AncillaryObject &object,
                                 std::size_t index, NSString *scope,
                                 bool by_reference) {
    NSString *kind = by_reference ? @"Ancillary · Reference" : @"Ancillary";
    add(scope, kind,
        [NSString stringWithUTF8String:mmxisf::to_string(object.kind)],
        @"Validated XISF core object", @"",
        object.uid.empty()
            ? [NSString stringWithFormat:@"Inventory index %lu", index]
            : [NSString stringWithFormat:@"uid=%@ · inventory index %lu",
                                         ns_string(object.uid), index]);
    for (const auto &attribute : object.attributes) {
      add(scope, @"Ancillary attribute",
          expanded_name(attribute.namespace_uri, attribute.name),
          [NSString stringWithFormat:@"On ancillary object %lu", index],
          display_string(attribute.value), @"");
    }
  };
  std::vector<bool> bound_ancillary(document.ancillary_objects().size(), false);
  for (const auto &binding : document.ancillary_bindings()) {
    if (binding.object_index >= document.ancillary_objects().size()) {
      continue;
    }
    add_ancillary(document.ancillary_objects()[binding.object_index],
                  binding.object_index,
                  [NSString stringWithFormat:@"Image %lu", binding.image_index],
                  binding.by_reference);
    bound_ancillary[binding.object_index] = true;
  }
  for (std::size_t index = 0; index < document.ancillary_objects().size();
       ++index) {
    if (!bound_ancillary[index]) {
      add_ancillary(document.ancillary_objects()[index], index, @"Standalone",
                    false);
    }
  }
  const auto add_icc_profile = [&](const mmxisf::IccProfileInfo &profile,
                                   std::size_t index, NSString *scope,
                                   bool by_reference) {
    NSString *kind = by_reference ? @"ICC profile · Reference" : @"ICC profile";
    NSString *descriptor = ns_string(profile.block.raw);
    if (!profile.compression.empty()) {
      descriptor =
          [descriptor stringByAppendingFormat:@" · compression=%@",
                                              ns_string(profile.compression)];
    }
    add(scope, kind, @"ICCProfile", @"Big-endian opaque profile bytes",
        descriptor,
        profile.uid.empty()
            ? [NSString stringWithFormat:@"Inventory index %lu", index]
            : [NSString stringWithFormat:@"uid=%@ · inventory index %lu",
                                         ns_string(profile.uid), index]);
    if (!profile.checksum.empty()) {
      add(scope, @"ICC profile", @"checksum", @"Digest",
          ns_string(profile.checksum), @"Verified when profile bytes are read");
    }
  };
  std::vector<bool> bound_icc_profiles(document.icc_profiles().size(), false);
  for (const auto &binding : document.icc_profile_bindings()) {
    if (binding.profile_index >= document.icc_profiles().size()) {
      continue;
    }
    add_icc_profile(
        document.icc_profiles()[binding.profile_index], binding.profile_index,
        [NSString stringWithFormat:@"Image %lu", binding.image_index],
        binding.by_reference);
    bound_icc_profiles[binding.profile_index] = true;
  }
  for (std::size_t index = 0; index < document.icc_profiles().size(); ++index) {
    if (!bound_icc_profiles[index]) {
      add_icc_profile(document.icc_profiles()[index], index, @"Standalone",
                      false);
    }
  }
  const auto add_thumbnail = [&](const mmxisf::ThumbnailInfo &thumbnail,
                                 std::size_t index, NSString *scope,
                                 bool by_reference) {
    NSString *kind = by_reference ? @"Thumbnail · Reference" : @"Thumbnail";
    NSString *descriptor = [NSString
        stringWithFormat:@"%@ · %@ · %@",
                         geometry_string(thumbnail.image),
                         ns_string(thumbnail.image.sample_format_name),
                         ns_string(thumbnail.image.color_space)];
    add(scope, kind, @"Thumbnail", @"Image-like XISF core object", descriptor,
        thumbnail.uid.empty()
            ? [NSString stringWithFormat:@"location=%@ · inventory index %lu",
                                         ns_string(thumbnail.image.block.raw),
                                         index]
            : [NSString
                  stringWithFormat:@"uid=%@ · location=%@ · inventory index %lu",
                                   ns_string(thumbnail.uid),
                                   ns_string(thumbnail.image.block.raw), index]);
  };
  std::vector<bool> bound_thumbnails(document.thumbnails().size(), false);
  for (const auto &binding : document.thumbnail_bindings()) {
    if (binding.thumbnail_index >= document.thumbnails().size()) {
      continue;
    }
    add_thumbnail(
        document.thumbnails()[binding.thumbnail_index], binding.thumbnail_index,
        [NSString stringWithFormat:@"Image %lu", binding.image_index],
        binding.by_reference);
    bound_thumbnails[binding.thumbnail_index] = true;
  }
  for (std::size_t index = 0; index < document.thumbnails().size(); ++index) {
    if (!bound_thumbnails[index]) {
      add_thumbnail(document.thumbnails()[index], index, @"Standalone", false);
    }
  }
  for (std::size_t index = 0; index < document.table_structures().size();
       ++index) {
    const auto &structure = document.table_structures()[index];
    NSString *scope = structure.table_index
                          ? [NSString stringWithFormat:@"Table %lu",
                                                       *structure.table_index]
                          : @"Standalone";
    add(scope, @"Table structure", @"Structure", @"Ordered fields",
        [NSString stringWithFormat:@"%lu fields", structure.fields.size()],
        structure.uid.empty()
            ? [NSString stringWithFormat:@"Inventory index %lu", index]
            : [NSString stringWithFormat:@"uid=%@ · inventory index %lu",
                                         ns_string(structure.uid), index]);
    for (std::size_t field_index = 0; field_index < structure.fields.size();
         ++field_index) {
      const auto &field = structure.fields[field_index];
      NSString *comment = field.header.empty()
                              ? @""
                              : [NSString stringWithFormat:@"header=%@",
                                                           ns_string(field.header)];
      add(scope, @"Table field", ns_string(field.id), ns_string(field.type),
          field.format.empty() ? @"" : ns_string(field.format), comment);
    }
  }
  const auto add_table = [&](const mmxisf::TableInfo &table, std::size_t index,
                             NSString *scope, bool by_reference) {
    NSString *kind = by_reference ? @"Table · Reference" : @"Table";
    NSString *shape = [NSString stringWithFormat:@"%lu rows",
                                                  table.rows.size()];
    if (table.structure_index &&
        *table.structure_index < document.table_structures().size()) {
      shape = [shape
          stringByAppendingFormat:@" × %lu columns · structure %lu · %@",
                                  document
                                      .table_structures()[*table.structure_index]
                                      .fields.size(),
                                  *table.structure_index,
                                  table.structure_by_reference ? @"Reference"
                                                               : @"Direct"];
    }
    NSString *comment = table.caption.empty() ? ns_string(table.comment)
                                              : ns_string(table.caption);
    comment = table.uid.empty()
                  ? [comment stringByAppendingFormat:@" · inventory index %lu",
                                                    index]
                  : [comment stringByAppendingFormat:@" · uid=%@ · inventory "
                                                     "index %lu",
                                                    ns_string(table.uid), index];
    add(scope, kind, ns_string(table.id), @"Inspectable XISF table", shape,
        comment);
  };
  std::vector<bool> bound_tables(document.tables().size(), false);
  for (const auto &binding : document.table_bindings()) {
    if (binding.table_index >= document.tables().size()) {
      continue;
    }
    add_table(document.tables()[binding.table_index], binding.table_index,
              [NSString stringWithFormat:@"Image %lu", binding.image_index],
              binding.by_reference);
    bound_tables[binding.table_index] = true;
  }
  for (std::size_t index = 0; index < document.tables().size(); ++index) {
    if (!bound_tables[index]) {
      add_table(document.tables()[index], index, @"Standalone", false);
    }
  }
  for (std::size_t index = 0; index < document.extension_elements().size();
       ++index) {
    const auto &extension = document.extension_elements()[index];
    NSString *scope =
        extension.image_index
            ? [NSString stringWithFormat:@"Image %lu", *extension.image_index]
            : @"XISF unit";
    NSString *parent =
        expanded_name(extension.parent_namespace_uri, extension.parent_name);
    if (extension.parent_extension_index) {
      parent = [NSString stringWithFormat:@"%@ · extension %lu", parent,
                                          *extension.parent_extension_index];
    }
    add(scope, @"Extension",
        expanded_name(extension.namespace_uri, extension.name),
        [NSString stringWithFormat:@"Child of %@", parent],
        display_string(extension.text),
        [NSString stringWithFormat:@"Semantic inventory index %lu", index]);
    for (const auto &attribute : extension.attributes) {
      add(scope, @"Extension attribute",
          expanded_name(attribute.namespace_uri, attribute.name),
          [NSString stringWithFormat:@"On extension %lu", index],
          display_string(attribute.value), @"");
    }
  }
  rows_ = [rows copy];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
  (void)tableView;
  return rows_.count;
}

- (NSView *)tableView:(NSTableView *)tableView
    viewForTableColumn:(NSTableColumn *)tableColumn
                   row:(NSInteger)row {
  (void)tableView;
  NSString *value = rows_[static_cast<NSUInteger>(row)][tableColumn.identifier];
  NSTextField *cell = [NSTextField labelWithString:value != nil ? value : @""];
  cell.lineBreakMode = NSLineBreakByTruncatingTail;
  cell.selectable = YES;
  cell.toolTip = value;
  return cell;
}

- (void)showError:(const mmxisf::Error &)error {
  NSString *detail =
      [NSString stringWithFormat:@"%s: %@", mmxisf::to_string(error.code),
                                 ns_string(error.message)];
  status_label_.stringValue = detail;
  [self showMessage:@"Unable to open XISF" detail:detail];
}

- (void)showMessage:(NSString *)message detail:(NSString *)detail {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.messageText = message;
  alert.informativeText = detail;
  alert.alertStyle = NSAlertStyleWarning;
  [alert beginSheetModalForWindow:window_ completionHandler:nil];
}

@end

int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    NSApplication *application = [NSApplication sharedApplication];
    application.activationPolicy = NSApplicationActivationPolicyRegular;
    AppDelegate *delegate = [[AppDelegate alloc] init];
    application.delegate = delegate;
    [application run];
  }
  return 0;
}
