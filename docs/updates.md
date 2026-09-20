# Windows development updater

The Windows replacement for Sparkle is a standard-user helper and a stable launcher. It implements checking, signed payload verification, staged installation, activation, failure rollback, crash recovery and preservation of project files. It is configured solely for a local development test channel. Production signing credentials, release transport and Authenticode signing are not configured.

The source service is the Sparkle controller in `Compositor/IO/CompositorApplicationDelegate.swift:9–11,30` and Check for Updates command in `Compositor/CompositorApp.swift:84`, baseline `a19db9011282399785dc18efcfded904627bdcc2`. The Windows implementation does not use that Mac feed or its signing key.

## Targets and installation layout

`src/update/Updater.cpp` builds the `compositor_update` library, linking Qt6 Core, `bcrypt` and `winhttp`. `UpdaterMain.cpp` is the console `CompositorUpdater.exe`; `LauncherMain.cpp` is the Windows-subsystem `CompositorLauncher.exe`, additionally linking `shell32` and `user32`. Root-level Qt6Core and MSVC runtime DLLs support these stable programs. Packaging owns those stable files and shortcuts; application releases live in distinct version directories.

```text
<app-owned root>/
  CompositorLauncher.exe
  CompositorUpdater.exe
  Qt6Core.dll and MSVC runtime DLLs
  install.json
  state/active.json
  state/update.lock
  versions/0.1.0/Compositor.exe, runtime/assets, update-receipt.json
  versions/0.2.0/Compositor.exe, runtime/assets, update-receipt.json
```

The launcher determines the root from its own executable path. It resolves the current version, recovers an interrupted activation when necessary, and calls `CreateProcessW` with an explicit executable path and correctly quoted arguments. It forwards Unicode, spaces, quotes, empty arguments and trailing backslashes; `.comp` paths remain directory-package paths. Microsoft documents the need to distinguish the executable path from the command-line buffer. [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).

After placing the initial payload under `versions/0.1.0`, run:

```powershell
./scripts/update-initialize.ps1 -Root <app-owned-root> -AllowTestKey
```

This creates the signed initial file receipt, install marker and active pointer. It refuses to reset an initialized installation. The install marker is `{"schema":1,"product":"compositor-windows"}`. The active pointer has exactly `schema`, `product`, `channel`, `current`, `previous` and `pending`; the initial values use `test`, `0.1.0`, an empty previous version and `false`.

## Trust and feed format

The test credential is a newly generated RSA3072 fixture. Its private counterpart is deliberately committed under `tests/update/fixtures/TEST-ONLY-private-key.xml` and is not a secret. The runtime public modulus is in `TestTrust.h`. Acceptance requires `Options.allowTestKey` or the helper flag `--allow-test-key`; an installation's ordinary launch never fetches a feed or opts into update trust. These fixtures must not become production trust.

`feed.json` contains `keyId`, `payload` and `signature`. Payload/signature are canonical base64. The signature verifies SHA256 of the exact decoded UTF-8 payload bytes with RSA PKCS#1 v1.5 through Windows CNG. The pinned public blob uses a 3072-bit modulus and exponent 65537. Microsoft specifies the RSA pre-hash verification contract and public-blob byte order. [BCryptVerifySignature](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptverifysignature), [BCRYPT_RSAKEY_BLOB](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_rsakey_blob).

The signed object binds `schema:1`, `product:"compositor-windows"`, `channel:"test"`, `version`, and a nonempty `files` array. Each file binds its relative `path`, integer `size` and lowercase SHA256. `Compositor.exe` is mandatory. Versions use exactly three decimal components, no leading zeroes, each at most 65535. Equal versions are unavailable; lower versions and other channels fail. Paths reject traversal, alternate data streams, Windows device names, duplicate case-folded names, reserved metadata paths and invalid Windows filename characters. The default total payload budget is 4 GiB, with at most 8192 files and 2 MiB envelope size.

Create a fresh test feed from any complete application payload:

```powershell
./scripts/update-make-test-feed.ps1 -PayloadDirectory <release-payload> -FeedDirectory <fresh-feed> -Version 0.2.0
CompositorUpdater.exe check --root <install-root> --test-feed <absolute-feed-directory> --allow-test-key
CompositorUpdater.exe install --root <install-root> --test-feed <absolute-feed-directory> --allow-test-key
```

The feed has `feed.json` and `payload/<signed-path>`. Existing update receipts are omitted and regenerated. `-ReceiptPath` can generate a receipt without copying a payload, as used by initialization. Local absolute directories and `http://127.0.0.1:<port>/.../` are supported. Loopback HTTP uses WinHTTP with no proxy and no redirects; external hosts, credentials, query strings, fragments and production URLs are rejected. Signed metadata is verified before payload processing; every payload is hashed while streaming and again from staged disk before activation.

## Installation and recovery protocol

The helper validates the app-owned marker, locks update state, and holds ordinary-directory handles along the root and operation paths. Reparse points are rejected. It creates a fresh `versions/stage-<UUID>` tree using exclusive file creation. After all lengths/hashes and the exact file inventory pass, it writes a signed receipt and renames the completed staging directory to its unused version name. Version directories are retained and never replaced in place.

Activation writes and flushes a new pointer file, then replaces `active.json` using `MoveFileExW`. The active state first selects the new version with `previous` and `pending:true`. It runs that version's fixed `Compositor.exe --update-health-check` command in a hidden process, then clears `pending` only after exit 0. Health checks default to a 120-second timeout and poll cancellation. The application implements its own runtime/model/codec checks in that branch.

A pre-activation failure leaves the old pointer. A post-activation exception restores it; a process crash leaves a pending journal that the launcher or `CompositorUpdater recover --root ...` rolls back conservatively. A crash after a successful health check but before finalization also rolls back. Verified unused versions can be retried only when their signed receipts and actual file inventory match the same release.

This is a recoverable multi-step protocol. Only the pointer-file replacement is treated as the activation switch. It does not claim that several filesystem operations are one atomic transaction or that filesystem APIs guarantee recovery from every storage/power failure. Microsoft distinguishes file replacement from directory rename and disallows replacing an existing directory with `MOVEFILE_REPLACE_EXISTING`. [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).

## Uninstall contract

Packaging invokes the stable helper before removing its own launcher/helper/runtime/shortcuts/registration:

```powershell
CompositorUpdater.exe --uninstall-payloads --root <install-root> --allow-test-key
```

The helper visits immediate version directories. A version's signed receipt, version name and safe file paths must validate. It removes only listed files whose current length and SHA256 still match. Deletion uses the verified file handle, so it does not reopen a different path for deletion. Any `.comp` path component is preserved even if listed. Modified package files, unknown files, untrusted versions, unverified partial stages and reparse points are retained and reported in JSON `retainedPaths`. It then attempts only empty-directory removal; it never recursively deletes the install root or user directories. The signed initial receipt lets it safely remove the initial release as well as later releases. Stable files and state remain packaging-owned.

## Reproducible evidence and limits

Run `scripts/update-build.ps1`, then `tests/update/run_checks.ps1 -SkipBuild`, or let the latter build by default. MSVC C++20 `/W4 /WX` builds the core, real helper and real launcher. The fixtures are two native executables: one succeeds at the fixed health command, one exits 7. They exercise helper behavior; they do not substitute for updating the full packaged application on a clean machine.

The latest recorded complete run at `tests/update/results/run-20260920-011904-e9f2d6de/results.json` passed **19 cases**. Cases cover explicit test trust; signed checking/install/real health execution; equal/downgrade/channel/version rules; signature rejection; payload tampering before execution; signed unsafe/duplicate paths; cancellation mid-download; real health failure rollback; failure after directory rename and retry; actual `ExitProcess` interruption after activation and launcher rollback; Unicode/quoted/trailing-backslash argument forwarding; a real active-pointer sharing violation; install-root junction rejection; a real loopback HTTP install and mid-transfer disconnect; unchanged project bytes inside and outside the install root; verified baseline/future-version uninstall; and preservation of modified payloads, projects within a version directory, untrusted versions and junction targets. Every case is required, with no silent fixture skips. Logs retain failures and resulting state; repeated runs use fresh workspace-local install/feed trees.

Production signing and a Windows release channel remain external inputs. Authenticode, a clean standard-user Windows VM, actual full-application upgrade/uninstall, power-loss storage testing and launcher/helper self-updates remain release gates. The helper runs without elevation or ACL/security changes. Its trust model protects against untrusted feeds and unsafe archive/path handling; it does not attempt to defend a per-user application from another process already able to replace that user's application files. Retained stages/versions can consume disk space; cleanup deliberately preserves unknown or changed files.

