# Compositor Windows community preview 0.1.0

This independent Windows 11 x64 port is based on Compositor 1.0.4 at
`a19db9011282399785dc18efcfded904627bdcc2`. Compositor was created by
[Robbie Tilton](https://github.com/robbietilton/Compositor); the original MIT
copyright and dependency notices accompany this distribution. This community
port does not imply upstream endorsement.

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

Clean Windows installation acceptance and publication are pending. This file
describes a release candidate; it is not a claim that a public release has
already passed those requirements.

## Demonstration

The separate demo ZIP contains redistributable inputs, **After the wind.comp**,
its PNG export and native screenshots at 100% and 150% Qt scale. The editing
recording uses the same application bytes as this distribution. Kept action and
processing sequences play at normal speed; operator pauses and private file
dialog navigation are cut. The dandelion photograph is public domain;
credit and source are in the demo's README.
