# Security policy

## Supported versions

| Version | Supported |
|---|---|
| 0.0.x | yes |

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private vulnerability reporting on this repository (Security tab, "Report a vulnerability"). Include the version, steps to reproduce and, if the bug is triggered by a file, a minimal sample you own the rights to.

You can expect an acknowledgement within a few days. Auralloy is a spare-time open-source project, so there is no guaranteed fix time, but confirmed memory-safety and file-parsing bugs are prioritised.

## Scope

Auralloy runs locally, makes no network connections and has no account system. The main attack surface is parsing audio files (WAV, FLAC, MP3, via JUCE) and `.alloy` project files. Treat files from untrusted sources with the same caution as in any other audio editor.
