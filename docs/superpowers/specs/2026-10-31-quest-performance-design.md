# Quest maintenance and APK design

Base: upstream `b07eca3c` (DroidDeck 0.6.7). Preserve the original fork on `origin/main`; develop on `maintenance/quest-performance` in this session's dedicated clone.

Preserve the original fork’s session-scoped Wi-Fi multicast lock for desktop LAN discovery, with a regression test for idempotent acquisition and teardown.

Use upstream Quest 2/3/3S detection, GPUOBJ adapters, presentation fixes, suspend handling and FEX optimizations. Do not stack the old fork's global ioctl shim or hard 1920px cap over those implementations.

Quest 3 and 3S share XR2 Gen 2 and 8GB RAM. Their initial resolution becomes 1080p; Quest 2 retains 720p and its A6xx driver. Defaults apply only once and preserve existing user resolution/shape settings. Add 1440p as a selectable Quest 3/3S resolution when the reported panel can accommodate it. Keep existing custom/match/follow resolution controls without a fork-specific cap. Do not promise higher clocks or measured FPS without hardware; maintain normal OS thermal control and existing refresh/FPS controls.

Embed the six verified add-on pack archives in Quest APK assets; prefer these over network downloads, reject hash/size mismatches without fallback, and retain upstream extraction/required-file checks. This avoids referring to unpublished local content hashes on upstream hosting.

Produce three arm64 APKs using the same sources and automatic hardware detection, with distinct app names and package IDs under `com.yomiplush.deckdroid`. APK target is build metadata; actual detected hardware always decides its driver. Keep upstream's test signing key for these experimental local builds and document that it is public, not a private production identity. Preserve GPL and fork disclosure. Do not publish or push.

Validation: targeted JVM tests for device defaults, preservation of settings and resolution choices; upstream native Quest regression tests; runtime asset and ELF checks; APK signatures/package labels/checksums. Build success does not establish device compatibility. All three outputs are explicitly hardware-unvalidated.
