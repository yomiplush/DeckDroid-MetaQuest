# Quest performance Implementation Plan

> Execute inline using executing-plans; this session has a dedicated fresh clone and a maintenance branch.

**Goal:** Maintain the fork on upstream 0.6.7 and deliver Quest 3, Quest 3S and Quest 2 experimental APKs.
**Architecture:** Reuse upstream hardware compatibility; add device-aware initial display defaults and build-target metadata. Prepare runtime once and package each target from the same inputs.
**Tech Stack:** Kotlin, Android Gradle, Python, C/C++, Docker, Rust.
**Spec:** `docs/superpowers/specs/2026-10-31-quest-performance-design.md`

## Global constraints
- Android arm64, min SDK 26, target SDK 28; existing upstream APIs and drivers.
- Preserve user settings and upstream licenses. No performance claims without measurements.
- APK target must not override actual hardware detection. Do not push or publish.

### Task 1: Device display defaults
Files: `QuestExperimental.kt`, `SessionDisplay.kt`, `ui/ResolutionRow.kt`, existing corresponding unit tests.
- [x] Add initialization tests asserting 1080p on Quest 3/3S, 720p on Quest 2, and preservation of explicit/custom/legacy settings.
- [x] Add resolution-options test for an optional extended heights list including 1440p, retaining screen/follow options.
- [x] Run targeted JVM tests and observe expected failures.
- [x] Apply defaults once through existing SessionPrefs resolution choices; parameterize preset heights and wire Quest choices into ResolutionRow.
- [x] Run targeted JVM and native Quest tests.

### Task 2: Three build targets
Files: `app/build.gradle`, `tools/quest/build-apks.sh`, `README.md`.
- [x] Add validated `questTarget` property (`quest3`, `quest3s`, `quest2`), unique package IDs, app labels, BuildConfig metadata and version suffix.
- [x] Add script to prepare inputs once then assemble, validate and copy three target APKs with hashes. Fail on any validation error.
- [x] Restore fork disclosure and funding metadata; describe settings, public test signature and hardware-unvalidated status.

- [x] Embed and verify all six Quest add-on packs so unpublished local hashes can install from the APK; test valid installation and corruption cleanup.

### Task 3: Verification and delivery
- [x] Prepare complete runtime/native/store inputs with tools/build_local.sh and local SDK/NDK/JDK17.
- [x] Run build-apks.sh and validate runtime payloads, JNI libraries, alignment and signatures for all three APKs.
- [x] Record source revision, test results, package IDs, sizes and SHA256 in output directory.
- [x] Inspect git diff and deliver APK links and source without claiming hardware validation.
