# Known issues and release decisions

Target: Windows 11 x64 community preview of pinned Compositor 1.0.4.
Full Mac equivalence is unverified. No public release is established.

| Issue | Current decision and evidence |
| --- | --- |
| Clean Windows acceptance | Blocked: no available VM or enabled Sandbox. Local installation tests can finish but cannot satisfy this publication gate. |
| Sanitizer shutdown | Clean ASAN is 925/932. Mask/filter routing printed PASS then hit its 30-second exit limit; an exact repeat exited in 2.03 s. Rotated viewport repeat exited three times, then stalled in the ASAN allocator during UCRT process detach; dump retained in evidence/integration/preview48/asan-viewport-rotated-stall/. Root cause and relation to the mask timeout remain unproven. This instrumented exit failure is retained as a preview verification exception; the distributed normal executable completes its regression. Original43 dump and twelve executor controls remain preserved. |
| Large soft brushes | Historical42 has 9/25 passing fixed 16.7 ms gates. Fresh native measurements are pending; no guarantee of 60 Hz responsiveness is advertised. |
| Foreground removal | Offline model remains useful for distinct opaque subjects; fur can retain background color and bubbles can lose transparency. Advanced refinement is not uniformly better. No-subject detection is not a general classifier. Retain the eight-case quality corpus and publish these limitations; current rerun is pending. |
| Source/test conflicts | Normal preview48 retains thumbnail cursor, Gaussian border alpha, M/L shortcut and deleted upstream cursor-observer conflicts. Production behavior follows the pinned source in the first four; the last observer no longer exists upstream. These are disclosed test discrepancies, not verified Mac parity. |
| Levels baseline conflict | The partial-alpha double-conversion defect is repaired; original Levels inversion and new selection regression pass. The frozen cancellation reference still contains the old defect and fails unchanged. A separate mathematical Levels oracle passes in normal and ASAN, including exact images, cancellation and callback exceptions. |
| Updates/signing | Preview application enforces manual updates before reading feeds. MSI/portable omit development update helpers and keys. Production signing and a public update service are deferred. Never advise disabling Windows security. |
| Hardware/accessibility | Physical pen, multiple monitors, Narrator, high contrast, IME and broader GPUs remain unverified. Native automation is not human acceptance. |

Mac reference fixtures and reciprocal project exchange are unavailable and deferred
for this preview. Do not request them again or claim full Photoshop compatibility.
Newer upstream features are outside this release.

Original Debug42 allocation-injection terminations and rejected45/46 performance
experiments remain in their evidence directories. The historical blend-preview
harness crash was repaired; it is distinct from the ASAN process-detach stall.

No known project corruption, ordinary-use crash or serious security defect can be
waived as a preview limitation. See [VALIDATION.md](VALIDATION.md) for current
measurements and [PROGRESS.md](PROGRESS.md) for the next actions.
