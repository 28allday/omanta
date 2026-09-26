# Omanta 0.1.13 validation

Validated on 26 September 2026, following the [0.1.12 validation](validation-2026-09-26.md).

## Fixes

- Replacing a regular file with a folder could delete the original destination before copying the folder's contents. If that copy failed, the old file was lost. Replace now rejects file/folder type conflicts with a clear error before changing either item. Ordinary file replacement and folder merging remain supported. The transfer also rechecks folder destinations during execution, preserving a conflicting file created after planning.
- When a mount backend aborted a pending password or verification request, the prompt stayed open and blocked subsequent requests. Omanta now releases the matching request, closes its dialog and clears an entered password. Unrelated or late aborts cannot dismiss another operation's prompt, and an already-aborted request receives no extra reply.

## Checks

- All 22 automated suites pass normally and with Clang AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer, with no skipped cases. Tests use a disposable HOME and private D-Bus session. External desktop theme/input/NVIDIA plugins are excluded from sanitizer runs; application leaks are not suppressed.
- The fault-recovery suite covers 12 isolated copy/move cases on full or read-only destinations. The four added folder-over-file cases verify that the original source and destination hashes are preserved. The new full-disk cases reproduced destination loss before the fix.
- Eight type-conflict cases cover copy/move, file-over-folder/folder-over-file and top-level/nested conflicts. A separate test creates a conflicting file after planning and verifies that it survives both the failed transfer and Undo of earlier completed work.
- Mount tests cover password and verification aborts, overlapping requests, late duplicate signals, subsequent answers and callbacks after destruction. The new abort tests failed before the fix. Tests using the application's actual QML confirm that both dialogs close, the password is cleared and the next prompt works.

These are focused follow-up checks. The earlier VM, SMB/SFTP, removable-storage and native Wayland checks documented for 0.1.12 were not rerun for this change. The automated UI checks use offscreen rendering.
