# Product direction

Read `docs/PRODUCT_SCOPE.md` before proposing or implementing changes.

The target is a Digitakt II-inspired VST3 instrument for Windows x64 in Ableton
Live. The user prioritizes faithful interface behavior and musical workflow,
not exact hardware DSP. Samples are imported by the user. Overbridge and
hardware connection/maintenance features are out of scope.

Keep the existing VST3 identity and parameter identifiers stable unless a
product change requires otherwise. On 10 October 2026 the user explicitly
removed backward compatibility as a requirement: do not spend work preserving
old-version state or audio parity. Verify save/reopen and rendering of the
current version. Develop on `development/0.3-machines-modulation`; do not
modify, merge into, or push `main` for this task.

Ground interface and workflow changes in the supplied manual and firmware
release notes. Record their versions and source sections, and distinguish
documented behavior from assumptions. Do not claim documents were received
until their contents can actually be read.

Use the existing checkout. Build and validate code changes with
`bash scripts/build.sh`. Windows builds use `scripts/build-windows.ps1` and
the GitHub Actions workflow. Documentation-only changes do not require
rebuilding the plugin.
