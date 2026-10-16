# FlashNetwork bridge (arm64 <-> x86_64)

Flashforge ships its network plugin (`libFlashNetwork.dylib`: cloud login, LAN/WAN printers, camera
URLs, MQTT, presets, AI) only as an **x86_64** binary. A native arm64 Flash Studio cannot load it
in-process, so this bridge hosts the original plugin in a small x86_64 helper that runs under Rosetta
and forwards the plugin's C API over a unix socket.

```
 Flash Studio (arm64)                          fnet_helper (x86_64, Rosetta)
 ┌─────────────────────────┐   unix socket    ┌─────────────────────────────┐
 │ libFlashNetwork.dylib   │ ───────────────► │ dlopen(libFlashNetwork_x64) │
 │  (shim, same C API)     │ ◄─────────────── │  executes the real calls    │
 └─────────────────────────┘  calls/callbacks └─────────────────────────────┘
```

* The API is plain C (`FlashNetwork.h`) and struct layouts are identical on both architectures
  (LP64, `#pragma pack(8)`), so structs are shipped as raw bytes followed by the data behind each
  pointer.  `gen/gen.py` parses `src/slic3r/GUI/FlashForge/FlashNetwork.h` and generates all marshalling code (95 forwarded
  functions, 30 `fnet_free*` implemented locally, 70 struct descriptors).
* Callbacks (progress, status, MQTT read, settings, client-id update) are proxied back to the shim:
  synchronous ones wait for the app's return value, status/read ones are asynchronous and ordered.
* Ownership: the helper serialises every plugin-owned result and frees it immediately with the
  plugin's own free function; the shim hands the app plain `malloc`ed copies and the `fnet_free*`
  exports free those.  No cross-process frees.
* If the app dies the helper sees EOF and exits.  If the helper dies, calls return `FNET_ERROR`
  (a later `fnet_initlize` respawns it).

## Build / install

```
./package.sh <built Orca-Flashforge.app> "<output>.app"      # builds, copies, ad-hoc signs
```

The original plugin is read from `/Applications/Flash Studio.app` (or pass its path as 3rd argument).
It is proprietary and is not rebuilt or modified, only copied next to the app as
`libFlashNetwork_x64.dylib`.

## Tests

`make tests` builds the fake plugin and the drivers; then run
`out/test_fake_x64 out/fake/libfake_x64.dylib` and `out/test_fake_arm64 out/fakebridge/libFlashNetwork.dylib`
and compare the sorted output (`diff <(sort a) <(sort b)`).

* `test_api.cpp` – real plugin, direct (x86_64) vs. through the bridge (arm64): outputs identical.
* `test_fake.cpp` + `fake_impl.cpp` – deterministic fake plugin covering nested structs, arrays,
  string arrays, bytes with NULs, `long long`, all MQTT payload types, the connection thread and
  every callback type.  Run direct and bridged and compare sorted output.

The API surface covered is the one declared in the 1.7.15 header.  The 1.7.19 plugin exports four
extra functions (`connectionSendByHttp`, `connectionSendMultiByHttp`, `getBusComUrl`,
`getUpdateComUrl`) that the 1.7.15 app does not call; if a newer app uses them, add their
prototypes to `FlashNetwork.h` and re-run `gen/gen.py` (via `make`).
