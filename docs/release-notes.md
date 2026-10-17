# Compositor Windows preview 0.1.8

This independent Windows 11 x64 port is based on Compositor 1.0.4 at
`a19db9011282399785dc18efcfded904627bdcc2`. Compositor was created by
[Robbie Tilton](https://github.com/robbietilton/Compositor); the original MIT
copyright and dependency notices accompany this distribution. This independent
port does not imply upstream endorsement.

Version 0.1.3 defaults to the native Windows title bar and system controls.
**View > Appearance > Mac-style title bar** enables the optional colored controls
on the left. Switching is immediate and the preference is saved. The rest of the
interface retains the design introduced in 0.1.2.

The 0.1.2 update applies a consistent dark visual design informed by rendered
Mac references: custom window controls, bundled Inter typography, rounded fields,
menus and tabs, compact adjustment panels, colored Hue/Saturation sliders and
a project picker. Layer rows place visibility, image and mask thumbnails before
the name. The vector tool rail and keyboard shortcuts remain available.

Version 0.1.5 adds canvas text editing and paragraph boxes, CPU layer effects, horizontal PSD and PSB type import, and the destructive Camera Raw Filter. Paragraph fields that need them are stored as project version 12. Camera Raw replaces the active image layer, docks to the document window, and is not saved in the `.comp` project. There is no camera profile or kelvin preset. Guided Upright is a two-line heuristic, its guide lines are not drawn, and geometry is a CPU homography. Full Mac equivalence is not claimed. Version 0.1.4 prepared the public repository and package naming; its editing behavior matched 0.1.3.

Version 0.1.6 keeps a partial text selection on the chosen glyphs and keeps the font and style chosen in the type menus. PSD and PSB import keeps Unicode layer names, and imports the merged composite when layer records cannot be decoded. An installed Photoshop PostScript font name is mapped to its DirectWrite family. While a Camera Raw slider is moving, the preview is limited to 1024 pixels on the long edge and then refines to 2048; OK still commits the full-resolution layer.

The same 0.1.6 preview opens welcome Import image, File > Import Image, the Export Image destination, and Save Project in Windows Explorer. An untitled import or save starts in Downloads. Save Project As starts in the current project folder when that folder exists. Import stays with the project that opened the picker. A second picker is refused, and the status bar reports that a file picker is already open. A JPEG name ending in .png is written as .jpg, and a PNG name ending in .jpg or .jpeg is written as .png, without appending the extension twice. Live text rows and the Type tool show an Aa glyph; the row subtitle is Text, and mask thumbnails stay in place. Filters > Last Filter (Ctrl+Alt+F) repeats the last Filters-menu apply with the same settings as one undo step. The menu item shows the remembered filter name. Image-menu adjustments and Content-Aware Fill are not remembered, and that choice is not stored in the `.comp` project. Filter Apply allows up to 4 GB of transient working memory, enough for a 100-megapixel apply, separate from the 1.2 GB import and merge caps. Color Overlay recolors a layer inside its existing alpha. Help → Command Palette… (Ctrl+Shift+P) lists currently bound commands and tools. Arrow keys move the highlight, and Enter runs the selected command.

Version 0.1.7 shows a navigator at the bottom-right of the canvas when zoom or pan leaves part of the document off-screen. It shows the whole composite and a rectangle for the visible area. Click or drag inside it to move the view. The picture updates shortly after editing pauses, and a brush or retouch stroke keeps the current picture until the stroke ends. The mouse wheel pans, including with Ctrl. Alt and rolling the wheel forward zooms in, and backward zooms out, around the pointer; one notch multiplies or divides the zoom by 1.25. Dragging a layer keeps that layer on screen while its preview finishes, and a later drag does not reuse the previous layer's preview. Auto-select layer starts checked. Camera Raw guided-upright lines stay visible on the preview, aligned with the image while zooming or panning, until Clear Guides, Cancel, or OK.

Version 0.1.8 shows horizontal and vertical rulers at the edges of the canvas. They display document pixels and stay aligned with the image at fit, at actual pixels, and after zooming or panning. View → Rulers (Ctrl+R) shows or hides them, and hiding them returns that margin to the canvas. Drag from the top ruler to add a horizontal guide, or from the left ruler to add a vertical guide. Drag a guide to move it; its position is shown while the pointer moves. Releasing the pointer commits one undo step. Escape cancels the drag. Releasing a new guide back on its ruler, or releasing a guide outside the document on its axis, cancels it and leaves the document unchanged. Opening a project shows the guides saved in it. View → Guides shows or hides guides and adds one at the vertical center when the document has none. View → Snap includes those guides while transforming.

The current source starts a new session with the rulers hidden. **File > Open Project** and the welcome **Open project** button use the Windows folder picker, start in Downloads, and accept one or more `.comp` folders. A 16-bit RGB PSD or PSB layer keeps its samples. Saving that document writes project format v13 and a 16-bit PNG. PNG export stays 16-bit for a visible root layer that is not clipped and has no adjustment or effect. A gradient or fill keeps the extra precision. Brush strokes, filters, and other pixel edits drop the layer to 8-bit, and JPEG export is 8-bit. Transform fields show position, size, and angle without trailing zeros. **File > Export Image** also writes a one-page PDF at the document's print size, with a chosen scale and background in the preview; Cancel leaves an existing file unchanged.

## What you can do

Import PNG, JPEG, TIFF and HEIC images; arrange layers and masks; select,
transform, paint and adjust images; undo and redo; save layered `.comp`
projects; and export flattened PNG or JPEG images. Background removal runs
locally with the included model. No account or model download is needed at
first launch.

The MSI and portable ZIP contain the same application. Portable users open
`Compositor.exe`. Installation, user guide, dependency notices and corresponding
source accompany the package. The build is unsigned; Windows may display an
unknown-publisher warning. Do not disable Windows security to run it.

## Limits of this preview

- Large soft brushes can fall short of 60 updates per second. On a Core Ultra 9
  285K with RTX 5070, two native runs on 4000 × 4000 images with 800 px soft
  brushes measured dispatch p95 of 11.7–15.5 ms on hardware and 19.1–24.4 ms on
  software rendering. Event-to-display-flush p95 was 38.6–50.4 ms; stroke start
  and release reached 427 and 522 ms. Both runs passed 6/10 unchanged 16.7 ms
  dispatch gates. Background CPU load was present; these are neither controlled
  idle measurements nor physical input-to-screen latency measurements. This is
  an explicit preview performance exception.
- Background removal can retain background color around fur and fine strands.
  Transparent objects such as soap bubbles are a known failure. Advanced
  refinement can worsen an edge; inspect before applying and use mask painting
  for cleanup. No-subject detection is not reliable on every textured image.
- Full Mac equivalence, reciprocal project exchange, Photoshop compatibility,
  physical pen input and broad hardware/accessibility certification are
  unverified. Keep backups of complete `.comp` directories when exchanging files.
- Updates are manual. This build ignores development update feeds and does not
  provide an automatic update service. Install a newer MSI or extract a newer
  portable download separately.

Validation retains six source/reference-test discrepancies. The sanitizer build
also has an intermittent process-exit stall after successful test work, including
a captured ASAN allocator wait during CRT shutdown. Its cause remains unresolved.
The distributed executable uses the normal build, whose full regression completed.

Clean Windows installation has not been tested. The user accepts existing local
verification for this preview. Downloads are published in the GitHub releases for this repository.

## Demonstration

The separate demo ZIP contains redistributable inputs, **After the wind.comp**,
its PNG export, and the backdrop generator. The dandelion photograph is public
domain; credits and the editing exercise are in the demo's README.
