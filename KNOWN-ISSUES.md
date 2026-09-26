# Known issues and release decisions

Target: Windows 11 x64 community preview of pinned Compositor 1.0.4.
Full Mac equivalence is unverified. No public release is established.

| Issue | Current decision and evidence |
| --- | --- |
| Clean Windows acceptance | Blocked: no available VM or enabled Sandbox. Local installation tests can finish but cannot satisfy this publication gate. |
| Sanitizer shutdown | Clean ASAN is 925/932. Mask/filter routing printed PASS then hit its 30-second exit limit; an exact repeat exited in 2.03 s. Rotated viewport repeat exited three times, then stalled in the ASAN allocator during UCRT process detach; dump retained in evidence/integration/preview48/asan-viewport-rotated-stall/. Root cause and relation to the mask timeout remain unproven. This instrumented exit failure is retained as a preview verification exception; the distributed normal executable completes its regression. Original43 dump and twelve executor controls remain preserved. |
| Large soft brushes | Two current 10-profile native runs each pass 6/10 unchanged 16.7 ms dispatch gates. On Core Ultra 9 285K/RTX 5070, 4000 px documents with 800 px soft brushes give hardware dispatch p95 11.7–15.5 ms and WARP 19.1–24.4 ms; large-brush event-to-DwmFlush p95 is 38.6–50.4 ms. Worst stroke start/release is 427/522 ms. Initial system CPU counters were 29%/23%, so these are not controlled-idle measurements. Accept this bounded preview performance exception; advertise no 60 Hz or physical-input latency guarantee. Historical42 remains 9/25. |
| Foreground removal | Current eight-case rerun completes: seven masks and grass no_subject. Published-cutout agreement passes both modes. Hair/fur/dandelion retain edge colors; bubble transparency fails; Advanced is not uniformly better. Inference is 2.132–2.189 s, peak about 1405 MiB. These disclosed quality limits are accepted for the preview; seven cases lack independent alpha and human acceptance remains pending. |
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
