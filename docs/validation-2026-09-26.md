# Omanta 0.1.12 validation

Validated on 26 September 2026, following the 0.1.11 code and security review.

## Fixes found by the checks

- A full destination filesystem could leave a truncated file at its final name, including when replacing an existing file. File copies now finish inside an exclusively created sibling directory before publication. Failed copies and cross-filesystem moves preserve the source and existing destination.
- Rapid trash, undo, trash and restore could miss a GVfs entry when no Trash monitor was active. Refreshing the Trash snapshot after restoration prevents reuse of a stale entry. Enumeration errors are now propagated.
- Mount operations handled password requests but omitted server verification questions. A plain-text dialog now requires an explicit answer; cancellation and window destruction abort the request. Both password and question handlers suppress GIO’s default idle reply, which otherwise rejected a prompt before the user could respond. Tests deliberately leave prompts unanswered across event-loop iterations.
- The full-text-search test leaked an owned ontology GFile. The thumbnail test incorrectly required an optional video thumbnailer. Both fixtures were corrected.
- Developer desktop checks assumed mixed sorting, killed unrelated Omanta processes, and split pointer gestures across virtual devices. They now use explicit fixture settings, refuse a busy session, clean up only their own process, and keep each drag on one device.

## Checks

| Check | Coverage and result |
| --- | --- |
| Automated suites | 22 suites pass on the development machine and in a fresh Arch VM. |
| Sanitizers | All 22 suites pass with Clang AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer. External desktop theme/input/NVIDIA plugins were excluded from the headless test environment; application leaks are not suppressed. |
| Full disk and permissions | Eight isolated cases: copy/move × new/Replace × full/read-only destination. Source and destination hashes, failure signals and staging cleanup verified. |
| Network transfers | Disposable Samba and OpenSSH servers in the VM: authentication, create, copy, rename/undo, move/undo and SHA-256 checks pass for SMB and SFTP. |
| Interrupted transfers | Throttled SMB/SFTP downloads cancelled in progress and interrupted by stopping the server process. Replacements retain existing bytes, remove local partial staging and report failure. |
| Native desktop | Isolated headless Sway Wayland session: keyboard workflow and pointer selection, rubber-band, activation, drag/drop and sidebar navigation pass. |
| Other applications | Actual Wayland drops into a GTK receiver from both icon and list views at 100% and 200% scaling. URI encoding, filenames with spaces/non-ASCII characters and source preservation verified. |
| Removable storage | A fresh 64 MiB emulated USB disk in the VM: cross-filesystem copy/move, rename, undo, hashes, unmount and device removal. |
| Package | Clean Arch installation of the previous package, independent source build and release package lifecycle checks. |
| Dependencies | 81 installed packages owning application/backend shared libraries plus direct dependencies compared with the Arch security feed; 144 advisory matches reviewed. No unresolved applicable match after checking upstream fixes. |

## Repeating the checks

`./bin/test` runs the regular suites. `./bin/test-sanitizers` builds with Clang and runs the same suites with memory, undefined-behaviour and leak checks. Use `OMANTA_SANITIZER_BUILD` to select a separate build directory. Bubblewrap is required for the full-disk/permission suite; its absence is reported as a skip. Video thumbnailers remain optional.

Run integration tests in a disposable account/VM with a private D-Bus session, HOME and XDG directories. Only disposable files and test services were altered in these checks. Keep native desktop tests in a separate compositor so input cannot reach the working desktop.

## Dependency evidence and limits

The [Arch security feed](https://security.archlinux.org/issues/all.json) contained old OpenSSL/libxml2 entries without fixed versions. Installed OpenSSL 3.6.4 is outside the affected ranges for CVE-2022-2068 and CVE-2025-4575, according to the [OpenSSL advisory](https://www.openssl-library.org/news/vulnerabilities-3.0/index.html) and [release notes](https://github.com/openssl/openssl/blob/master/NEWS.md). Installed libxml2 2.15.4 includes the shell and Schematron fixes recorded in the [libxml2 release notes](https://raw.githubusercontent.com/GNOME/libxml2/v2.15.4/NEWS).

These checks cover the exercised code paths and published advisories available on the validation date. They do not establish that the application or its dependencies contain no vulnerabilities. Physical removable devices, MTP cameras/phones, arbitrary remote servers, GPU-specific rendering and power loss during writes were not tested. A disconnected remote destination can prevent cleanup of its hidden staging directory; the operation reports an error and cannot guarantee cleanup until the server is available again.
