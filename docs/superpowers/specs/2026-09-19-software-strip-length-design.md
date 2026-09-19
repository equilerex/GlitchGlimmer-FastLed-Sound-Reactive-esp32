# Software Strip Length Design

## Goal

Let the browser choose a software-only strip length and LED pitch that are passed into the shared GlitchGlimmer animation library, so WASM animations and compositor layers respond to the configured software geometry. The physical board's pin and buffer macros remain unchanged.

## Contract

Each simulated strip has four separate values:

- `capacity`: the fixed buffer capacity compiled for the target. Device capacity remains `LED_0_NUM`/`LED_1_NUM`; the WASM target gets a larger simulation capacity.
- `lengthM`: the software strip length, clamped to `0.1..20.0` metres.
- `pitchMm`: LED spacing, initialized from the selected profile and manually overridable, clamped to `0.5..100.0` mm.
- `activeCount`: the software LED count, derived as `floor(lengthM * 1000 / pitchMm)` and clamped to capacity. A future explicit count override may be added only if it remains distinct from capacity.

The shared `LEDStrip` receives `activeCount` as its runtime `length`; animations and layers must use that length. Buffers and FastLED registrations continue to use capacity. The device path initializes active length from the compile-time physical count. The WASM path initializes active length from the browser-controlled default and exports get/set accessors.

The browser path is a continuous arc-length path. When the path reaches the viewport edge, additional path segments wrap around the stage; the camera zoom changes only the view transform. Pixel placement uses active count and pitch, with pixels outside the camera viewport clipped by canvas drawing.

Profiles remain rows in `profiles.js`. Each profile supplies pitch, die, sigma, and visual defaults for pixel size, glow size, intensity, and exposure. Selecting a profile applies those defaults only when the corresponding setting is still profile-controlled; a manual override remains intact until the user resets or explicitly reapplies profile defaults.

## Non-goals

- Changing physical board LED counts or pin assignments.
- Making the device allocate a 20 m virtual strip.
- Resampling a fixed WASM frame after rendering to fake animation geometry.
- Adding per-profile branches to the lit renderer.

## Future stretch goal

Support editing animation behavior in the browser without rebuilding the firmware/WASM module. This is intentionally separate from runtime strip geometry: the current C++ animation catalog is compiled into WASM, so live editing requires either a browser-side animation API/DSL that targets the same frame contract or an in-browser compiler pipeline. The runtime-length work must leave a clean frame and geometry boundary for either option.

## Verification

- Pure tests cover length/count derivation, pitch validation, profile visual defaults, wrapped path sampling, and pixel placement.
- Native/device builds prove the capacity/active-length API remains GNU++11-compatible.
- WASM exports are checked for the runtime count/length round trip.
- Browser tests verify profile selection, length/pitch controls, persisted settings, and no layout regressions.
