---
name: device-smoke-cycle
description: Build a library/binary locally, stage it on a Thingino camera, run a consumer against it, capture dmesg/logread/logcat, then reboot.
license: MIT
---
# device-smoke-cycle

Use this skill for a fast iteration loop: build something locally, stage it on the camera, run a consumer that links/executes against the staged copy, collect logs, reboot. Each project wires the specifics; the cycle shape is the same.

## When to use

- You changed a library and want to test the new build against a device-side consumer.
- You want a clean dmesg + logread + optional logcat bundle from a freshly-started consumer.
- You want the device returned to a known state before the next iteration.
- You want comparable, timestamped smoke bundles for regression tracking.

## Project-specific wiring for this repo

- Build command:

```bash
./build-for-device.sh T31
```

- Artifacts:
  - `lib/libimp.so`
  - `lib/libsysutils.so`

- Stage dir on device:
  - `/opt`

- Consumer:

```bash
LD_LIBRARY_PATH=/opt /opt/S31raptor start
```

- Device:
  - IP: `192.168.50.215`
  - User: `root`

- Before each smoke run, preload:
  - `/lib/modules/3.10.14__isvp_swan_1.0__/ingenic/tx-isp-t31.ko`
  - `/lib/modules/3.10.14__isvp_swan_1.0__/ingenic/sensor_gc2053_t31.ko`

- After each experiment:
  - reboot the device

## Workflow

1. Build locally.
2. Stage `lib/libimp.so` and `lib/libsysutils.so` to `/opt`.
3. Start the consumer with `LD_LIBRARY_PATH=/opt`.
4. Wait a few seconds and capture:
   - `dmesg`
   - `logread`
   - `logcat`
   - `/proc/interrupts`
   - `/tmp/openimp-smoke.out`
5. Save the bundle under `logs/<timestamp>-*.log`.
6. Reboot the device so the next iteration starts fresh.

## Notes

- In this repo, the reboot-at-end rule is mandatory for clean iteration.
- Do not skip module preloading after a reboot.
- `logcat` may not always exist; capture it if present but do not make the cycle depend on it.
- Prefer comparing each new run against the latest known-good smoke bundle rather than reading raw logs in isolation.
