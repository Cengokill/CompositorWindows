# Project format at a19db9011282399785dc18efcfded904627bdcc2

This reference describes the pinned upstream storage contract. macOS wire fixtures and exchange remain unverified; [reference/README.md](../reference/README.md) describes the capture harness. The Windows implementation is in [src/persistence](../src/persistence). The older upstream `docs/project-format.md` stops at v6 and incorrectly describes selection restoration. `EditorSession+Projects.swift:20–37` restores the active layer, resets history, fits the viewport, and starts with no document selection.

`.comp` is a directory containing `manifest.json` and `images/`. `ProjectStore.swift:10–42` defines the actual Codable records; saves default to **version 7**, and the reader accepts versions 1–7. Images are named `<UUID>.png`; masks are `<UUID>.mask.png`. The exact filename comparison uses Foundation's uppercase `UUID.uuidString`. JSON UUID values may be parsed case-insensitively, but asset names are checked against the normalized UUID string.

| Manifest key | Wire value and decoding |
|---|---|
| `format` | Required string `com.compositor.project` |
| `version` | Required integer 1–7; new snapshots use 7 |
| `colorSpace` | Required string `sRGB` |
| `resolution` | Optional finite number 1–9600; absent/null installs as 72 |
| `documentID` | Required UUID string |
| `width`, `height` | Required integers, each 1–30000 |
| `activeLayerID` | Optional UUID string; if present must identify a layer |
| `layers` | Required array, bottom-to-top sibling order; maximum 10000 |

| Layer key | Wire value and decoding |
|---|---|
| `id`, `name`, `isVisible`, `transform` | Required UUID, string, Boolean, object |
| `imageFile` | Optional exact UUID filename; nil permits blank layers |
| `parentID`, `isGroup` | Optional UUID/Boolean; no parent is root, nil group installs false |
| `opacity`, `blendMode` | Optional finite 0–1 number/raw enum string; install defaults 1/Normal |
| `maskFile`, `maskEnabled` | Optional exact mask filename/Boolean; mask enabled defaults true |
| `maskSourceID` | Optional UUID live-alpha dependency |
| `adjustment` | Optional `LayerAdjustment` object; v7 only |
| `maskPlacement` | Optional complete transform; requires a mask file |
| `maskLinked` | Optional Boolean; linked defaults true when a mask exists |
| `shape` | Optional `LayerShapeStyle`; becomes live only with a corresponding image |

The Swift types use synthesized Codable, without `init(from:)` or custom `CodingKeys`. A non-optional stored property with an initializer still requires its key during decoding; an initializer is **not** a missing-key fallback. Optional missing/null keys decode nil. Unknown keys are ignored by the upstream decoder. Optional nil values are omitted by the encoder. The Windows implementation must retain the distinction between valid legacy omission and malformed omission of required fields.

`LayerTransform.swift:18–30` requires `origin`, `size`, `rotation`, `flipX`, `flipY`, and `sampling`. The expected Foundation CGPoint/CGSize wire representation is `[x,y]` and `[width,height]`; the Mac harness records the actual runtime representation before declaring exchange verified. Origin, size and rotation must be finite; size is 1–300000 per side, absolute origin is at most 1000000, and rotation has no numeric limit beyond finiteness. Sampling strings are `Nearest`, `Smooth`, and `High quality`. Coordinates use a top-left document origin and clockwise rotation about the rectangle center. Typed fractions are retained.

The 13 blend strings are `Normal`, `Multiply`, `Screen`, `Overlay`, `Darken`, `Lighten`, `Difference`, `Color Dodge`, `Color Burn`, `Hue`, `Saturation`, `Color`, and `Luminosity` (`LayerAppearance.swift:4–8`). A folder requires full opacity and Normal. Before v3 only these defaults are valid, even when the keys are supplied.

Version gates are validated in `ProjectStore.swift:177–214`: v1 forbids parents and groups; v2 adds groups; v3 permits non-default appearance; v4 permits raster masks; v5 permits live-mask links; v6 permits folder masks; v7 permits adjustment layers. `maskPlacement`, `maskLinked`, `shape`, resolution, and newer optional adjustment settings have no additional explicit version gate in this pinned source. Do not infer gates solely from field chronology. `maskEnabled` requires `maskFile`; `maskPlacement` requires `maskFile` and a valid transform. `maskLinked` alone is accepted, although without a mask it has no installed effect.

Adjustments (`LayerAdjustment.swift:28–66`) require `kind`, `hue`, `saturation`, `lightness`, `colorize`, `levels`, and `curves`. Optional `hsvSettings` overrides the legacy HSV scalars; optional `exposureSettings`, `gradientMapSettings`, and `grainSettings` resolve to their defaults when absent. Kind strings are `Hue/Saturation`, `Levels`, `Curves`, `Exposure`, `Gradient Map`, and `Grain`. All parameter families are validated even when inactive. An adjustment cannot be a group or carry an image file; it can carry masks and can be a clipping target.

| Nested object | Required shape and validation |
|---|---|
| `HueSaturationSettings` | `range`, `colorize`, `invertRange`, `adjustments`, `bands`. Ranges: Master/Reds/Yellows/Greens/Cyans/Blues/Magentas. Enum-keyed Swift dictionaries encode as alternating key/value arrays, not JSON objects; actual Mac probes remain pending. Empty adjustments are valid. Each adjustment has `hue`, `saturation`, `lightness`; finite limits ±360, ±100, ±100. Bands have finite `falloffStart`, `rangeStart`, `rangeEnd`, `falloffEnd`; validator does not impose ordering/range limits. |
| `LevelsSettings` | `channel` (RGB/Red/Green/Blue), `ranges` array of exactly four objects. Each has black [0,254], white [black+1,255], gamma [0.1,9.99], outputBlack/outputWhite [0,255]. Validation requires equality to normalized values. Reversed output endpoints are valid. |
| `CurvesSettings` | `channel`, `channels` array of four arrays. Each has 2–32 `{x,y}` points, finite [0,255], first x=0, last x=255, strictly increasing x. |
| `ExposureSettings` | `exposure` [-20,20], `offset` [-0.5,0.5], `gamma` [0.01,9.99]. Defaults 0/0/1. |
| `GradientMapSettings` | `shadows`, `highlights` each `{red,green,blue}` finite [0,1]; `reversed` Boolean. Defaults black/white/false. |
| `GrainSettings` | `amount` [0,100], `size` [0.5,20], `roughness` [0,100], `seed` UInt32. Defaults 25/1.5/50/0. |
| `LayerShapeStyle` | `kind` Rectangle/Ellipse, `red`, `green`, `blue`, `cornerRadius` numbers. No shape-specific validation is called by `ProjectStore.validate`; do not silently claim stronger upstream constraints. |

Hierarchy validation (`LayerGroups.swift:31–49`) rejects duplicate IDs, missing/non-group parents, cycles, and image-bearing groups. A leaf may have 64 group ancestors; a group may have at most 63. Traversal groups records by `parentID` and preserves their array order as sibling order. Visibility inherits through ancestors. Folder masks multiply descendants while folders remain pass-through.

Live graph validation (`LiveLayerMask.swift:3–21`) follows every node's `maskSourceID` path and admits at most **256 nodes**, including the starting target. Missing nodes, repeated nodes, group endpoints and adjustment sources are rejected. Adjustment targets are permitted. The validator does not require the source to be a lower sibling or in the same folder. UI operations separately create/release contiguous clipping stacks (`LiveLayerMask.swift:27–62, 207–252`). A hidden or black source still supplies alpha; source opacity, transform, raster mask and upstream live links contribute. Deleting a source offers bake/cancel/unlink, and baking retains the target's raster mask and appearance.

`ProjectStore.swift:116–168, 216–231` limits the manifest to 4 MiB and each encoded asset to 512 MiB; assets must decode as one PNG frame with depth at most 8. Images and masks each have their own cumulative 100000000-pixel budget, each side 1–30000. A valid canvas may exceed 100 million pixels until an operation requiring a full raster/export enforces its own budget. Grayscale masks must be monochrome, 8-bit, no alpha, and not a CGImage mask (`LayerMask.swift:28–31`). Disabled masks are persisted. Asset and metadata files must resolve inside the package and be regular, non-symlink files. Windows reparse and replacement handling requires its own verified policy.

The save routine validates the full snapshot, encodes all assets, then asks Foundation to replace a staged directory package atomically. Windows cannot claim the same atomic-directory guarantee from two renames. A recoverable staged write with an explicit journal and interruption tests is required; power-loss durability and macOS exchange remain separate verification items.

## Windows reader through version 12

The dependency pin stays at the 1.0.4 contract above. The Windows reader also accepts versions 8–12 and writes the lowest version that can represent the document. A plain document still saves as version 7. Versions 1–7 decode with the same gates as before.

| Version | Additional content |
|---|---|
| 8 | Folder opacity other than 1, and document guides |
| 9 | Gaussian Blur, Motion Blur, and Add Noise adjustment layers |
| 10 | Text `colorRuns` |
| 11 | Text `fontRuns` |
| 12 | Text paragraph fields: `alignment`, `tracking`, `leading`, `boxSize`, or a non-regular `fontStyle` |

A version 7 file that carries folder opacity other than 1, guides, or a blur/noise adjustment is rejected. Invert, Black & White, and Color Balance adjustments stay valid at version 7.

Blend names keep the original 13, then append `Linear Burn`, `Linear Dodge`, `Soft Light`, `Hard Light`, `Vivid Light`, `Linear Light`, `Pin Light`, `Hard Mix`, `Exclusion`, `Subtract`, and `Divide`. A folder may store opacity 0–1 from version 8 and still has to stay in Normal. Its opacity multiplies descendants.

Additive fields do not raise the version by themselves: `effects`, base `text` without runs, and a line `shape` (`lineWidth`, `start`, `end`). Hidden effects keep their parameters. `colorRuns` and `fontRuns` use UTF-16 code-unit `location` and `length`. A text object without `boxSize` is point text, left aligned, with tracking 0 and leading 0 (automatic, 120% of the font size). `alignment` is `Left`, `Center`, or `Right`. `boxSize` is `{width, height}` in layer pixels, each side from 16 to 30000. `tracking` is −100 to 1000 and `leading` is 0 to 5000. `fontStyle` on the text object or a font run names an installed variant; `Regular` and `Normal` are omitted. These paragraph fields are written only when they differ from those defaults, and only then does the document become version 12. A lower version that still carries them is rejected. Guides are `{id, horizontal, position}` and are separate from the transient transform snap lines. Mac exchange of these fields remains unverified.
