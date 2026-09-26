# Validation

Results below describe actual local checkpoints, not Mac equivalence or broad hardware certification. Raw logs and dumps are retained locally; generated evidence is excluded from this repository.

| Checkpoint | Result |
| --- | --- |
| Publication preview 0.1.4 | Release application builds from the staged source export using the prepared dependency cache. 24 selected CTest checks, the manual-update policy check, native window-control probe, and 18-step editing workflow pass. |
| Preview 0.1.4 downloads | Six portable deployment checks pass, including hashes, matching source, manual-update distribution, cleaned-PATH health, native editing, and Unicode project reopen/export. Local MSI upgrade and installed cleaned-PATH health exit 0; the installed executable matches the package. |
| Preview 0.1.3 window appearance | Release build; 43 affected UI checks pass. Native title bar switching, persistence, dialogs, and canvas probes pass at 100% and 150%. |
| Preview 0.1.3 installed application | MSI upgrade and runtime health exit 0. The 18-check native editing workflow passes with a cleaned PATH. Four package hashes and 474 source entries match the packaged snapshot. |
| Preview 0.1.2 visual update | 75 focused checks pass; 33 affected panel checks pass after final refinement; seven native visibility accessibility checks pass. |
| Last full normal regression | 926/932 pass. The six retained discrepancies are described in KNOWN-ISSUES.md. |
| Last full ASAN regression | 925/932 pass. Six discrepancies plus an intermittent process-exit timeout remain. |
| Independent Levels cancellation oracle | Pass in normal and ASAN; the original failing frozen comparison remains unchanged. |
| Preview 0.1.0 packaging | 16/16 deployment checks, including payload/source hashes, runtime health, editing/save/export, MSI install/repair/upgrade/downgrade rejection/uninstall, and project preservation. |
| Source rebuild | Offline bootstrap, configure, Release application build, and deployed runtime health pass on the development host. This does not establish byte-identical binaries. |
| Brush performance | Two runs each passed 6/10 existing dispatch gates. Large-brush latency and background-load caveats remain disclosed. |
| Foreground removal | Eight cases executed; seven masks and one no-subject result. Published-cutout agreement passes both modes; fine-edge and transparent-object limitations remain. |

Native workflow checks cover import, layers/masks, transformation, selections, painting, undo/redo, preview cancel/apply, save/reopen, export, and shutdown. Screenshots combine actual Qt rendering with the Direct3D canvas readback where normal window capture cannot include it.

Original assertions and test sources remain available. Optional tests that need external image corpora, historical audit inventories, or a Mac reference capture require those inputs. Their absence must not be represented as passing verification. Fresh Windows and Mac comparisons have not been performed.
