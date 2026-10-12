# Compositor Windows preview 0.1.6

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

Version 0.1.6 keeps a partial text selection on the chosen glyphs and keeps the font and style chosen in the type menus. PSD and PSB import keeps Unicode layer names, converts 16-bit RGB channels to 8-bit, and imports the merged composite when layer records cannot be decoded. The import report lists those substitutions. An installed Photoshop PostScript font name is mapped to its DirectWrite family. While a Camera Raw slider is moving, the preview is limited to 1024 pixels on the long edge and then refines to 2048; OK still commits the full-resolution layer.

The same 0.1.6 preview opens welcome Import image, File > Import Image, the Export Image destination, and Save Project in Windows Explorer. An untitled import or save starts in Downloads. Save Project As starts in the current project folder when that folder exists. Import stays with the project that opened the picker. A second picker is refused, and the status bar reports that a file picker is already open. A JPEG name ending in .png is written as .jpg, and a PNG name ending in .jpg or .jpeg is written as .png, without appending the extension twice. Live text rows and the Type tool show an Aa glyph; the row subtitle is Text, and mask thumbnails stay in place. Filters > Last Filter (Ctrl+Alt+F) repeats the last Filters-menu apply with the same settings as one undo step. The menu item shows the remembered filter name. Image-menu adjustments and Content-Aware Fill are not remembered, and that choice is not stored in the `.comp` project. Filter Apply allows up to 4 GB of transient working memory, enough for a 100-megapixel apply, separate from the 1.2 GB import and merge caps. Color Overlay recolors a layer inside its existing alpha. Help → Command Palette… (Ctrl+Shift+P) lists currently bound commands and tools. Arrow keys move the highlight, and Enter runs the selected command.

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
