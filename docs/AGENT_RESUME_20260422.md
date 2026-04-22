# OpenIMP Resume: 2026-04-22

## Current checkpoint

- Working code checkpoint commit before this handoff bundle: `c5f9d65` (`Restore AVPU stream priming path`)
- Device was rebooted at the end of the last completed experiment so the next iteration can start fresh.
- Primary current outcome:
  - `avpu.0 = 5` is restored and reproducible from a clean boot.
  - `rvd` still fails with `FAILED (no ring after 5s)`.

## Most important proof log

- Clean-boot repro:
  - `logs/20260422T170923Z-start.log`
  - `logs/20260422T170923Z-smoke.log`

Key lines in `logs/20260422T170923Z-smoke.log`:

- `enc_chn->stream_frame_size=65536`
- `RegisterChn linked`
- `StartRecvPic prime chn=0 grp=0 rc=0`
- `PushNewFrame`
- `ListModulesNeeded lane=0 post-get-stream-buffers ... ok=1`
- `/proc/interrupts`: `70:          5   jz-intc  avpu.0`
- Final user-visible failure remains: `rvd  FAILED (no ring after 5s)`

## Code state that restored AVPU

### `src/video/imp_encoder.c`

- `IMP_Encoder_RegisterChn()` now:
  - links channel into the group
  - calls `IMP_System_BindIfNeeded(...)`
  - auto-calls `IMP_Encoder_StartRecvPic(arg2)`

- `IMP_Encoder_StartRecvPic()` now:
  - primes stream buffers via `AL_Codec_Encode_PrimeStreamBuffers(...)`
  - logs `StartRecvPic prime ...`

### `src/alcodec/lib_codec.c`

- `AL_Codec_Encode_PrimeStreamBuffers()` was reworked so it does not call the public `AL_Encoder_PutStreamBuffer()` path too early.
- The current behavior:
  - gets a stream buffer from the codec stream pool
  - inserts the `AL_TBuffer *` into the encoder's ring
  - resolves the real channel-manager context with `GetChMngrCtx(sched + 4, chan)`
  - queues the stream entry with `AL_EncChannel_PushStreamBuffer(...)`
- This is the change that got us back from `ok=0` / `avpu=0` to `ok=1` / `avpu=5`.

### `src/alcodec/ChannelMngr.c`

- Still contains the scheduler mutex release/reacquire behavior around `AL_EncChannel_Encode(...)`.
- This file was part of the restored working tree and should be kept as-is unless there is a very targeted reason to change it.

## Known-good workflow for each experiment

Always start from a fresh boot and preload stock modules before the smoke run.

### Device

- IP: `192.168.50.215`
- User: `root`
- Password: `Ami23plop`

### Stock modules to preload

- `/lib/modules/3.10.14__isvp_swan_1.0__/ingenic/tx-isp-t31.ko`
- `/lib/modules/3.10.14__isvp_swan_1.0__/ingenic/sensor_gc2053_t31.ko`

### Build

```bash
./build-for-device.sh T31
```

### Stage libraries to device

```bash
cat lib/libimp.so | sshpass -p Ami23plop ssh -F /dev/null -o StrictHostKeyChecking=no root@192.168.50.215 'cat >/opt/libimp.so'
cat lib/libsysutils.so | sshpass -p Ami23plop ssh -F /dev/null -o StrictHostKeyChecking=no root@192.168.50.215 'cat >/opt/libsysutils.so'
```

### Start smoke

```bash
sshpass -p Ami23plop ssh -F /dev/null -o StrictHostKeyChecking=no root@192.168.50.215 \
  "insmod /lib/modules/3.10.14__isvp_swan_1.0__/ingenic/tx-isp-t31.ko 2>/dev/null || true; \
   insmod /lib/modules/3.10.14__isvp_swan_1.0__/ingenic/sensor_gc2053_t31.ko 2>/dev/null || true; \
   rm -f /tmp/openimp-smoke.out; \
   LD_LIBRARY_PATH=/opt /opt/S31raptor start > /tmp/openimp-smoke.out 2>&1; \
   echo EXIT:$?"
```

### Collect logs

```bash
sshpass -p Ami23plop ssh -F /dev/null -o StrictHostKeyChecking=no root@192.168.50.215 \
  "sleep 8; \
   echo '===DMESG==='; dmesg 2>/dev/null; \
   echo '===LOGREAD==='; logread 2>/dev/null; \
   echo '===LOGCAT==='; logcat 2>/dev/null; \
   echo '===INTERRUPTS==='; cat /proc/interrupts 2>/dev/null; \
   echo '===PROC_OUT==='; cat /tmp/openimp-smoke.out 2>/dev/null"
```

### End every experiment with a reboot

```bash
sshpass -p Ami23plop ssh -F /dev/null -o StrictHostKeyChecking=no root@192.168.50.215 reboot
```

## Source-of-truth constraints

- Do not change the kernel driver as part of this line of work.
- OEM behavior and OEM decomps are the source of truth.
- The legacy OpenIMP attempt is still relevant because it previously managed to trigger AVPU interrupts even while producing bad stream data.
- The current goal is not "get any stream"; it is to restore the OEM-aligned end-to-end path.

## Suggested next debugging target

The next problem is after AVPU launch, not before it.

What is already working:

- Frames reach the encoder path.
- `group_update` runs.
- `PushNewFrame` runs.
- Stream buffer acquisition succeeds (`ok=1`).
- AVPU is launched and interrupts fire.

What still appears broken:

- Ring publication / final output handoff after encoding completion.

Good next focus areas:

1. Compare the current `EndEncoding` / output publication path against OEM decomps.
2. Use BN MCP on `libimp.so` to inspect the exact OEM post-encode / stream output path.
3. Diff any current behavior against the earlier "legacy openimp gets AVPU interrupts" state only when it informs the OEM path.

## MCP / decomp notes

- Binary Ninja MCP server for OEM `libimp.so` was reported as:
  - server id / port backing `libimp.so`: `b0fd4ce6` / `9009`
- User repeatedly requested using smart BN MCP more aggressively for key methods.

## Handoff summary

If resuming on another machine, begin from this sequence:

1. Pull this commit.
2. Build with `./build-for-device.sh T31`.
3. Run one clean smoke cycle exactly as described above.
4. Confirm the repro still shows `avpu.0 = 5`.
5. Then move directly into OEM-guided post-encode / ring-publication debugging.
