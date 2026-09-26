# Working on Compositor for Windows

Read README.md and PROGRESS.md. Consult KNOWN-ISSUES.md and VALIDATION.md when the task concerns a release or a recorded failure. Read only the subsystem code and documentation relevant to the task.

This is an independent Windows preview based on Robbie Tilton's Compositor. Keep the upstream pin in dependencies.lock.json accurate. Preserve original attribution and licenses. Staying aligned with upstream is the maintenance intent; do not claim that newer Mac features or full parity are already verified.

Visual quality matters. Use the rendered Mac design as a reference and maintain cohesive custom interior controls. Native Windows title bars are the default; Mac-style window controls are an optional saved preference.

The code itself is the implementation reference. Read and link to it instead of writing a second description of its behavior. Documentation should explain user workflows, build steps, external contracts, and decisions that cannot be inferred from the code. People and coding agents use the same documentation.

Keep coordination to these four files: AGENTS.md, PROGRESS.md, VALIDATION.md, and KNOWN-ISSUES.md. Update them in place when necessary. Do not create new handoffs, agent reports, task ledgers, completion matrices, or diaries. Keep generated logs, test results, screenshots used only for verification, and local experiments out of Git. A deliberate product screenshot or a real test fixture is different: give it a purpose and provenance.

Choose work that addresses the user's current request. Avoid speculative refactors, new verification frameworks, and repeated checks without a new change or failure. Use the existing CMake/CTest targets and checks appropriate to the change. Preserve known failures accurately; a deferred comparison is not a pass. Fresh Windows hardware and Mac fixtures are not prerequisites for this preview's publication.

Do not publish social announcements or propose announcement angles unless asked. Do not use “community” as project branding. Preserve historical commits and compatibility identifiers; do not rewrite history to alter old wording.
