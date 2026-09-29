# Plan: build a fully static `spqk` binary

## Goal and current state

Produce a Linux `build-static/spqk` with no ELF shared-library dependencies. Keep the regular `build/` workflow available. A successful link alone is not enough: confirm the resulting ELF has no `NEEDED` entries and run it on a host without the build-time library packages.

The current `ldd build/spqk` identifies the dependencies to remove:

- Project/direct: `libspdlog.so.1.17`, `libespeak-ng.so.1`, `libboost_program_options.so.1.92.0`, `libboost_json.so.1.92.0`.
- C++ runtime: `libstdc++.so.6`, `libgcc_s.so.1`, `libm.so.6`, `libc.so.6`, and the ELF interpreter `/lib64/ld-linux-x86-64.so.2`.
- Speech/audio chain: `libsonic`, `libpcaudio`, `libasound`, `libpulse-simple`, `libpulse`, `libpulsecommon`, `libdbus`, `libsndfile`, `libxcb`, `libsystemd`, `libasyncns`, `libogg`, `libvorbisenc`, `libFLAC`, `libopus`, `libmpg123`, `libmp3lame`, `libvorbis`, `libXau`, and `libXdmcp`.
- `linux-vdso.so.1` is provided by the kernel and is not a library to bundle.

`subprojects/spdlog.wrap` already pins spdlog 1.17.0, and `meson.build` already obtains it as a Meson subproject. The existing setup compiles a shared spdlog library in `build/subprojects/spdlog-1.17.0/src/`, which explains its entry in `ldd`.

## 1. Define and pin a static build environment

1. Choose one target distribution, architecture, and minimum supported kernel/libc baseline. Build inside a reproducible container or sysroot for that target, with static development archives installed. Record compiler, Meson, Ninja, Boost, espeak-ng, and audio-library versions in the build instructions/CI.
2. Ensure the sysroot provides `.a` archives and headers for espeak-ng, Boost.Program_options, Boost.JSON and its Boost.Container dependency, the selected audio stack, and the C/C++ runtimes. Check archive availability before configuring; distro runtime packages often omit static archives.
3. Use a dedicated `build-static/` directory and a Meson native/cross file if isolating a sysroot. Do not reuse `build/`, whose dependency discovery and existing shared outputs reflect the current dynamic configuration.

## 2. Make spdlog static through its existing Meson wrap

Keep `subprojects/spdlog.wrap` as the source of spdlog. It already fetches/pins the upstream source and the project already calls `subproject('spdlog', ...)`; no system shared spdlog should be selected.

In the spdlog subproject, `compile_library` defaults to true and its Meson logic creates a static library when `default_library` is `static`. Set the project default `default_library=static` for the dedicated setup, or pass the option explicitly to the subproject. Keep `std_format=enabled` and `tests=disabled` as in the current project. The link must consume the archive `libspdlog.a`; make sure the selected C++ standard library supports the requested `std::format` feature, or use spdlog's fmt fallback with a static fmt archive.

## 3. Build/select static Boost.Program_options and Boost.JSON

Provide static Boost archives in the build sysroot, including `libboost_program_options.a`, `libboost_json.a`, and `libboost_container.a` (the current JSON shared library pulls in Boost.Container). Keep the existing Meson `dependency('boost', modules: ...)` structure if the installed Meson Boost dependency supports static selection; set `static: true` for both module dependencies and confirm the generated link command names archives. If discovery still chooses shared Boost libraries, use a Meson dependency override or a small project Meson option to point explicitly at the static archives and Boost headers. Preserve JSON's transitive dependencies and correct link ordering.

Do not assume that static Boost alone makes the executable static: inspect every dependency's resolved link args in Meson's introspection output and the final ELF.

## 4. Build/select static espeak-ng and decide the audio contract

Build espeak-ng and its required libraries as static archives in the same target sysroot; make `dependency('espeak-ng', static: true)` select `libespeak-ng.a`. Confirm the required speech data/voice files are installed or provide a documented data lookup strategy. Static code does not embed the voice data automatically.

The current dependency tree shows that espeak-ng brings in both synthesis helpers (`sonic`, `pcaudio`) and playback/audio backends. Pick and document one of these supported approaches before implementation:

- **Keep live playback:** statically build every enabled backend and transitive dependency shown by `ldd`, including ALSA and/or PulseAudio and their dependencies. Use espeak-ng's static pkg-config metadata (`pkg-config --static --libs espeak-ng`, or equivalent Meson metadata) so private link dependencies are included. Account for optional backend libraries discovered at configure time; repeat the dependency audit after building.
- **Remove live playback from the static target:** configure/build espeak-ng without optional playback backends, and adapt `spqk`'s `--run-demo`/default playback path to write WAV or report that playback is unavailable. This reduces the dependency surface but changes behavior, so it must be an explicit product decision rather than silently dropping libraries.

The first path preserves current functionality but may be difficult on distributions without static audio archives. A “fully static” executable cannot retain dynamic PulseAudio/ALSA modules through a static espeak-ng archive; espeak-ng and all linked backend code must be audited for runtime `dlopen` behavior as well.

## 5. Link the C++ and C runtimes statically

Pass static runtime options at the final executable link: `-static` to the linker, `-static-libstdc++`, and `-static-libgcc` as compiler/linker options supported by the selected compiler. `-static` is the key requirement for resolving libc, libm, and other system libraries from archives and omitting the ELF interpreter; the runtime-specific flags make intent clear and cover toolchains with different driver defaults. Add these through an opt-in Meson option or a target-specific `link_args` list rather than changing the default developer build.

Use a libc toolchain/sysroot that actually supplies static libc and all required archives. Static glibc binaries can still rely on runtime configuration/data such as DNS/NSS modules, locale data, certificates, and speech data; avoid claiming that the executable is self-contained with respect to those files. If the deployment requirement includes those resources, document/package them separately or evaluate a musl target and verify its compatibility with all dependencies.

## 6. Add an opt-in Meson configuration

Add a boolean project option (for example `static_binary`, default `false`) in `meson_options.txt`, then use it to select static dependencies and final link flags. A dedicated setup should have the following shape (exact compiler option syntax depends on the selected toolchain):

```sh
meson setup build-static \
  --buildtype=release \
  -Ddefault_library=static \
  -Dstatic_binary=true
meson compile -C build-static
```

Keep the existing normal setup unchanged. In `meson.build`, apply `static: true` to external dependencies where supported, ensure the spdlog subproject receives the static library setting, and attach `-static`, `-static-libstdc++`, and `-static-libgcc` to the `spqk` executable only when the option is enabled. Prefer checking compiler/linker support at Meson configure time and fail with a clear diagnostic when required static archives are missing. If project dependencies need static private link flags, use static pkg-config metadata rather than hard-coding the current host's library list.

## 7. Verify the result and its deployment behavior

After implementation, configure and compile the dedicated target, then:

1. Inspect the final link command and Meson dependency introspection to verify archives were selected for every dependency.
2. Run `file build-static/spqk`, `readelf -l build-static/spqk`, and `readelf -d build-static/spqk`. The final executable should have no `PT_INTERP` and no `DT_NEEDED` entries. `ldd` should report it as statically linked (some `ldd` variants may say “not a dynamic executable”).
3. Exercise `--help`, `--version`, voice listing, WAV synthesis, and live playback if retained. Test on the target baseline without the development packages installed, and separately check required voice/config data availability.
4. Keep a record of any runtime files, plugins, or external services still required. If any shared object is loaded dynamically at runtime, resolve it or narrow the documented “static” claim.

This document is the implementation plan only; no build or configuration changes are part of this plan-writing task.
