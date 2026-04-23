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

## Evening update

Two more concrete results were established after the checkpoint above.

### 1. External AVPU probing is not viable on this target

- `tools/avpu_peek.static.mips` was built successfully and staged to the camera.
- A timed smoke bundle at `logs/20260422T195551Z-smoke.log` showed that a second process cannot sample `/dev/avpu` while `libimp` owns it:
  - `open /dev/avpu failed: No such device`
- This is not "bad timing"; it happened at multiple offsets during the active run.
- Conclusion:
  - any delayed AVPU register sampling has to happen in-process through the existing `ip_ctrl` path.

### 2. The scheduler GC re-arm bug was real, but it was not the final blocker

- In `src/alcodec/ChannelMngr.c`, `Process()` clears the shared per-core GC flag to `0` when it turns the clock gate off.
- The original `CheckAndEncode()` logic only requested `AL_EncCore_TurnOnGC()` when that flag was already nonzero, which is backwards for the post-turnoff state.
- The retained fix is:
  - `if ((uint32_t)(*s4_2) == 0U) { AL_ModuleArray_AddModule(...); *s4_2 = 1; }`
- Clean repro with that fix:
  - `logs/20260422T195946Z-smoke.log`
- Important proof lines from that log:
  - `CheckAndEncode gc-scan core=0 mod=1 flag_ptr=... flag=0`
  - followed by `wr fd=10 reg=0x83f4 val=0x00010001`
  - then `encode2 launch ...`
  - then `enc-snap tag=enc2-post ... status=0x00000010`
  - still `/proc/interrupts: avpu.0 = 6`
  - still `rvd  FAILED (no ring after 5s)`
- Conclusion:
  - phase 1 was definitely being launched from a worse clock-gate state before this fix.
  - restoring the GC re-arm improves parity, but phase 1 still does not produce a new AVPU interrupt.

### Reverted experiment

- I also tested a narrower sequencing change in `src/alcodec/Scheduler.c` that forced `0x83f4 = 1` immediately before `AL_EncCore_Encode2()`.
- Clean repro:
  - `logs/20260422T200329Z-smoke.log`
- That run proved the write happened:
  - `encode2 launch ...`
  - `wr fd=10 reg=0x83f4 val=0x00000001`
  - `startenc core=0 write 0x83e0=... 0x83e4=0x00000008`
- But it did not change behavior:
  - still no post-encode2 IRQ
  - still `status=0x00000010`
  - still `avpu.0 = 6`
  - still no ring
- That explicit pre-`encode2` write was reverted, so it is **not** part of the current baseline.

### Current baseline at handoff

- Retained source change from this evening:
  - `src/alcodec/ChannelMngr.c`: fixed `CheckAndEncode()` so a cleared shared GC flag causes `TurnOnGC` to be requested again.
- Reverted source change:
  - `src/alcodec/Scheduler.c`: explicit `0x83f4 = 1` before `encode2`.
- Best current clean comparison logs:
  - `logs/20260422T195946Z-smoke.log` — retained GC re-arm fix baseline
  - `logs/20260422T200329Z-smoke.log` — explicit pre-`encode2` 83f4 write experiment, reverted after proving no gain
- Strongest current statement:
  - the remaining blocker is no longer "phase 1 launched with the core clock still off".
  - the remaining blocker is still "phase 1 launches, `0x83f8` goes to `0x10`, and no completion IRQ/ring publication ever follows."

## Late-night update

Three narrower scheduler iterations were run after the evening checkpoint above.

### 1. The direct `UpdateCommand(..., slice_core, core)` materializer experiment was a dead end

- Clean repro:
  - `logs/20260422T201846Z-smoke.log`
- That experiment changed the phase-1 command list substantially:
  - `w00=84400c11`
  - `w1b=f0000000`
  - `w1c=03330702`
  - `w1d=00077014`
- But it did not change the actual failure:
  - still `encode2 launch ...`
  - still `enc-snap tag=enc2-post ... status=0x00000010`
  - still `/proc/interrupts: avpu.0 = 6`
  - still `rvd  FAILED (no ring after 5s)`
- That path was backed out.

### 2. The standalone Enc2 slice reconstruction is back on the retained 5-core baseline

- Two follow-up runs tightened the old custom `PrepareSliceParamForCore -> CmdRegsEnc1ToSliceParam -> RepairStandaloneAvcEnc2Slice` path:
  - `logs/20260422T202929Z-smoke.log`
  - `logs/20260422T203217Z-smoke.log`
- First fix:
  - preserve the original prepared slice geometry while deriving the standalone Enc2 slice.
- Result:
  - recovered the old 1080p late-entropy fields:
    - `sp78=000a`
    - `w1b=000a0c80`
    - `w1e=00001fdf`
  - restored the full per-core slice windows:
    - `0..8159`, `8160..16319`, `16320..24479`, `24480..32639`, tail slice `32640`

### 3. The remaining Enc1 `w00` delta was real and was also restored

- The only remaining CL mismatch after the step above was:
  - current `w00=81400c11` / `01400c11`
  - retained baseline `w00=80400c11` / `00400c11`
- Root cause from local logging:
  - `SliceParamToCmdRegsEnc1` derives `w00` bits `[26:24]` from slice byte `b3`.
  - Our prepared AVC slice was carrying `b3` low bits = `5` instead of the retained baseline low bits = `4`.
- Narrow fix:
  - clamp only the low 3-bit AVC `b3` field in `PrepareSliceParamForCore()`, leaving the upper bits intact.
- Clean repro after that fix:
  - `logs/20260422T203616Z-smoke.log`
- Important proof lines from that run:
  - `encode1 pre-FillSliceParam ... ch4e=20`
  - `encode1 post-enc1-pack core=0 ... w0=80400c11`
  - `enc2-cmd core=0 ... w00=80400c11 w1b=000a0c80 w1c=21330d06 w1d=00077000 w1e=00001fdf`
- Strong conclusion:
  - phase-1 command-list materialization has now been walked back onto the retained baseline shape.
  - restoring that exact CL still does **not** produce a post-`encode2` completion interrupt or a ring.

### What did not matter

- The new OEM-parity `EndEncoding` final-output interrupt disable loop is now present in `src/alcodec/Scheduler.c`.
- It has not triggered in any of the failing runs above because phase 1 never reaches the final output path.
- So it is a parity fix worth keeping, but it is not the explanation for the current stall.

### Updated boundary

- The current debugging boundary is no longer "bad phase-1 command list generation".
- The current debugging boundary is now:
  - phase 1 launches with a baseline-matching CL,
  - `enc2-post` still reports `status=0x00000010`,
  - `avpu.0` still stops at `6`,
  - no additional completion IRQ/ring publication follows.
- The next useful target should therefore move out of slice packing and into:
  - post-launch interrupt delivery / masking,
  - phase-1 completion status consumption,
  - or any remaining OEM scheduler differences between `encode2 launch` and `HandleCoreInterrupt` on completion.

## Follow-up update

One more pair of iterations tightened the boundary again without moving the actual fault.

### 1. The full logged Enc2 body is still identical to the retained GC-fix baseline

- Clean restored-baseline repro:
  - `logs/20260422T204908Z-smoke.log`
- Comparing `20260422T204908Z-smoke.log` against the retained GC re-arm baseline `logs/20260422T195946Z-smoke.log` shows that the **entire logged Enc2 window** is identical, not just the headline words:
  - `w00=80400c11`
  - `w01=ff0860ef`
  - `w1b=000a0c80`
  - `w1c=21330d06`
  - `w1d=00077000`
  - `w1e=00001fdf`
  - `w20..w3f` also match line-for-line, including:
    - `w20=067e8000`
    - `w21=069e2400`
    - `w23=07208680`
    - `w24=w25=071f6000`
    - `w27=07202000`
    - `w2e=071f9300`
    - `w2f=07208400`
    - `w30=0720c000`
    - `w31=w33=w3d=w3f=0000fe80`
- The post-launch state is also unchanged:
  - `enc-snap tag=enc2-post ... mask=0x00011115 pending=0x00000000 status=0x00000010`
  - `/proc/interrupts: avpu.0 = 6`
  - `rvd  FAILED (no ring after 5s)`
- Conclusion:
  - the remaining blocker is **not** in any currently logged Enc2 command/body word drift.

### 2. Large CoreManager-side progress instrumentation is too invasive for this port

- I briefly added delayed post-launch `encode2` progress snapshots in `src/alcodec/CoreManager.c` to sample status a few milliseconds after launch.
- Clean repro with that instrumentation:
  - `logs/20260422T204632Z-smoke.log`
- That run regressed materially:
  - `/proc/interrupts: avpu.0 = 0`
  - the run never even reached the usual phase-1 launch boundary
  - the smoke failed much earlier than the retained baseline
- That diagnostic instrumentation was fully backed out before the restored-baseline repro above.
- Practical takeaway:
  - this MIPS port is layout-sensitive enough that broad `CoreManager.c` instrumentation can perturb behavior by itself.
  - future diagnostics in that area should stay very small and local, or prefer scheduler/log correlation over wide new helper code in `CoreManager.c`.

## Handoff summary

If resuming on another machine, begin from this sequence:

1. Pull this commit.
2. Build with `./build-for-device.sh T31`.
3. Run one clean smoke cycle exactly as described above.
4. Confirm the repro still shows `avpu.0 = 6` with the retained GC re-arm fix.
5. Then move directly into OEM-guided phase-1 completion / ring-publication debugging.

## 2026-04-22 Late Addendum

The debugging boundary moved twice after the handoff summary above.

### 1. A real allocator crash was found and patched locally

- High-value crash repro:
  - `logs/20260422T211911Z-smoke.log`
- That run no longer looked like a post-`encode2` stall.
- Instead, `rvd` segfaulted during encoder bring-up before any AVPU interrupt fired:
  - kernel reported `SIGSEGV`
  - `epc` resolved into `rc_oiii`
  - `ra` resolved into `AL_RateCtrl_Init`
- Root cause from local addr2line / objdump:
  - `rc_oiii()` dereferences the allocator vtable from `*(rc_ctx + 0x24)`
  - the allocator object pointer was non-null
  - but the allocator's first word, the vtable pointer, was null
  - crash instruction was effectively `lw t9, 4(vtable)` with `vtable == NULL`

### 2. Current retained local code changes for that crash

- `src/alcodec/EncSchedulerCpu.c`
  - snapshots `AL_GetDefaultAllocator()->vtable` into a private `AL_TAllocator` copy before `AL_SchedulerEnc_Init()`
  - falls back to the DMA allocator only if that snapshot is already invalid
  - logs the allocator/vtable/function pointers at scheduler-create time
- `src/alcodec/RateCtrl_11.c`
  - logs allocator pointer, vtable, `Alloc`, and `GetVirtualAddr` at `AL_RateCtrl_Init()` entry
- `src/alcodec/RateCtrl_16.c`
  - `rc_oiii()` now logs allocator/vtable entry state
  - if allocator or vtable is null, it returns failure instead of hard-segfaulting
- `src/alcodec/lib_codec.c`
  - adds earlier `AL_Codec_Encode_Create()` checkpoints:
    - entry
    - post-copy
    - post-defaults
    - post-basic-validate

These changes build locally and stage correctly to the camera.

### 3. The allocator patch has not yet been exercised on a clean comparable run

Three later smoke bundles did **not** get back to the old `AL_RateCtrl_Init` crash boundary:

- `logs/20260422T212841Z-smoke.log`
- `logs/20260422T213045Z-smoke.log`
- `logs/20260422T213304Z-smoke.log`

Observed behavior:

- no AVPU activity:
  - `/proc/interrupts`: `avpu.0 = 0`
- no `SchCpu.Create`, `pre-Encoder_Create`, or `RC: Init entry` logs
- runs either stopped around:
  - `pre-codec-create`
  - or much earlier in frame-source binding

The most recent smoke (`logs/20260422T213304Z-smoke.log`) is especially important:

- it shows repeated frame-source wait loops:
  - `libimp/FS: thread-wait-bind ch=0 ... observers=0`
  - `libimp/FS: thread-wait-bind ch=1 ... observers=0`
- that means the run never reached encoder creation at all
- so it says nothing about whether the allocator snapshot/fallback fixed the earlier crash

### 4. Practical current state

- The allocator crash is real and the local patch set is in place.
- The camera is currently bouncing between two earlier startup failure buckets:
  - frame-source bind never completing (`thread-wait-bind ... observers=0`)
  - or codec create stalling before the first internal `AL_Codec_Encode_Create()` checkpoints appear
- Because of that, the latest clean-boot smokes are not comparable to the earlier `avpu.0 = 6` / `encode2-post status=0x10` baseline.

### 5. Best next step from this exact workspace

Do **not** assume the allocator patch failed. It simply has not been re-exercised yet.

Resume with:

1. one more clean smoke after a reboot, but first confirm the frame-source bind path is progressing and not stuck at `observers=0`
2. if the run reaches `pre-codec-create`, use the new `AL_Codec_Encode_Create()` checkpoints to find the exact early stop
3. once a run reaches `RC: Init entry` again, verify whether:
   - the scheduler snapshot log appears
   - `AL_RateCtrl_Init` now sees a non-null allocator vtable
   - the old `rc_oiii` segfault is gone
4. only after that returns to a comparable boundary, continue the original AVPU phase-1 completion work

## 2026-04-22 Late-Night AVPU Addendum

The current workspace moved well past the allocator/startup bucket above. The
encoder is back on the 5-core AVPU baseline, and the active blocker is now
purely phase-1 completion.

### Latest retained smoke bundles

- `logs/20260422T223509Z-smoke.log`
  - restored the useful baseline after a failed callback-wait experiment
  - phase 1 reaches `EndAvcEntropy`
  - `OutputSlice` still publishes `end=0`, `bytes=0`
  - `EndEncoding non-gate queued-output` and `GetNextFrameToOutput return queued`
  - `rvd FAILED (no ring after 5s)`
- `logs/20260422T223915Z-smoke.log`
  - added a stream-buffer probe in `OutputSlice`
  - the stream buffer was still filled with the untouched canary pattern:
    - `aa 5a aa 5a ...`
  - this proves the zero-byte result is not just a missing CL byte-count update;
    the entropy stage has not written real payload by finalization time
- `logs/20260422T224053Z-smoke.log`
  - tested deferring early `EndAvcEntropy` instead of finalizing immediately
  - result:
    - the early irq4 still fires once
    - no later phase-1 completion IRQ arrives
    - leaving `mod1` enabled does not recover a second interrupt
    - `GetNextFrameToOutput` stays empty
- `logs/20260422T224233Z-smoke.log`
  - removed that deferral experiment
  - restored the better diagnostic baseline:
    - phase 1 callback enters
    - output event is queued
    - bytes remain zero
    - stream buffer is still untouched canary data

### What is now proven

- `EndAvcEntropy` is reached too early for real completion:
  - `enc2-state tag=EndAvcEntropy core=0 status=0x00000010 end=0x00000000 st104=0x00000000 st1e4=0x00000000`
- The encoder finalizes while Enc2 still looks busy:
  - `enc2+1ms`, `enc2+10ms`, and `enc2+100ms` all keep `0x83f8 = 0x00000010`
- The stream buffer itself is still untouched at finalization time:
  - `OutputSlice stream-probe ... first_nz=0`
  - `OutputSlice stream-probe-bytes ... aa 5a aa 5a ...`
- Skipping that early callback does not reveal a later real completion IRQ.
  - Keeping the phase-1 interrupt enabled produced no later irq2/irq4 completion before the smoke window ended.

### Practical consequence

The active bug is no longer "output publication is broken." The publication
path is alive again. The real blocker is earlier:

- Enc2 launch succeeds enough to set `0x83f8` bit `0x10`
- but the hardware never reaches a visible writeback/completion state
- and never produces real stream payload in the staged buffer

### Current local changes that should be kept

- `src/alcodec/Scheduler.c`
  - fixed the `OutputSlice()` per-core status buffer overflow
  - preserves the separate Enc2 stream-end read at `0xf8`
  - logs final-output progression and a low-risk stream-buffer probe
- `src/alcodec/CoreManager.c`
  - keeps the small `enc2-state` snapshot in `EndAvcEntropy`
  - does **not** keep the failed blocking-wait or failed callback-deferral experiment

### Best next target from this exact workspace

Resume from `logs/20260422T224233Z-smoke.log` and focus on why Enc2 never
progresses past the early-busy state after launch. The most likely buckets are:

1. remaining Enc2 command/slice materialization drift versus the OEM scheduler path
2. missing launch-side AVPU state that OEM applies between `encode2` submission and actual entropy processing
3. a core/register sequence difference that leaves Enc2 stuck busy without ever writing payload

## Side-By-Side Legacy Reference

Per user direction, the historical commit:

- `bc7c4cb` — `AVPU continuous encoding + RTSP stream reaching clients`

has been materialized as a detached sibling worktree at:

- `/home/matteius/openimp-bc7c4cb-avpu-rtsp`

This is important because that revision is still on the older flat AVPU path.
It does **not** use the later `src/alcodec/` scheduler/core-manager stack now
under active bring-up. The first comparison targets for the next session should
therefore be:

1. `/home/matteius/openimp-bc7c4cb-avpu-rtsp/src/codec.c`
   versus current `src/codec.c`
2. `/home/matteius/openimp-bc7c4cb-avpu-rtsp/src/imp_encoder.c`
   versus current `src/imp_encoder.c`
3. `/home/matteius/openimp-bc7c4cb-avpu-rtsp/src/al_avpu.c`
   and `src/al_avpu.h`
4. the historical notes in:
   - `/home/matteius/openimp-bc7c4cb-avpu-rtsp/thingino-streamer.md`
   - `/home/matteius/openimp-bc7c4cb-avpu-rtsp/ALIGNMENT_FIXES.md`
   - `/home/matteius/openimp-bc7c4cb-avpu-rtsp/STREAM_BUFFER_SIZE_FIX.md`

Useful current observations about that legacy tree:

- `src/codec.c` in `bc7c4cb` still carries the earlier RTSP/GetStream-oriented AVPU logic.
- `src/imp_encoder.c` in `bc7c4cb` still contains the old `stream_thread` / `IMP_Encoder_GetStream`
  integration that reached clients.
- That revision explicitly discusses cache-flush failure on T31 and the move toward uncached command-list
  handling, which may still be relevant to the current stuck-busy phase-1 behavior.

## 2026-04-22 Late Session Addendum

### Toolchain path restored

- `build-for-device.sh` now auto-detects the real T31 toolchain path instead of
  hardcoding the stale `...-192.168.50.215/host/bin` suffix.
- The currently valid local path is:
  - `/home/matteius/thingino-firmware-opensensor-master/output/master/wyze_cam3_t31x_gc2053_rtl8189ftv-3.10.14-uclibc/host/bin/`

### Legacy-side comparison result that directly affected the active fix

- The standalone Enc2 CL must not trust the live slice object's `0xfc` field.
- In the current alcodec bring-up, the live slice `0xfc` can be in a different
  unit system than the Enc2 cmd word `w1e` expects.
- The safer source is the value reconstructed by:
  - `CmdRegsEnc1ToSliceParam(cmd_regs, slice_enc2, ...)`
- `src/alcodec/Scheduler.c` now:
  - logs `encode1 enc2-fc`
  - uses cmd-derived `slice_enc2[0xfc]` for standalone AVC Enc2
  - falls back to the live slice value only if the cmd-derived value is zero

### Smoke run: heuristic mistake that should not be reused

Log bundle:

- `logs/20260422T225412Z-start.log`
- `logs/20260422T225412Z-smoke.log`

What happened:

- I briefly tried a heuristic that preferred the live slice value when it
  matched a supposed "full-frame" product.
- That was wrong because the live slice dimensions were not in the same unit as
  Enc2 `w1e`.

Observed signals:

- `encode1 enc2-fc core=0 live=129600 cmd=8160 full=129600 final=129600`
- `enc2-cmd ... w1e=0001fa3f`
- stream still untouched:
  - `OutputSlice stream-end ... end=0x0`
  - `OutputSlice stream-probe ... first_nz=0`
  - `OutputSlice stream-probe-bytes ... aa 5a aa 5a ...`

Conclusion:

- Do not derive standalone Enc2 `0xfc` from the live slice geometry product.

### Smoke run: current corrected baseline

Log bundle:

- `logs/20260422T225630Z-start.log`
- `logs/20260422T225630Z-smoke.log`

Key corrected signals:

- `encode1 enc2-fc core=0 live=510 cmd=8160 final=8160`
- `enc2-cmd core=0 ... w1e=00001fdf`
- `enc2-state tag=EndAvcEntropy core=0 status=0x00000010 end=0x00000000`
- `/proc/interrupts` still shows:
  - `avpu.0 = 6`

What did **not** improve:

- `OutputSlice stream-end ... end=0x0`
- `OutputSlice stream-probe ... first_nz=0`
- `OutputSlice stream-probe-bytes ... aa 5a aa 5a ...`
- output publication still queues:
  - `EndEncoding non-gate queued-output`
  - `GetNextFrameToOutput return queued`

Meaning:

- The standalone Enc2 `0xfc -> w1e` regression is now repaired back to the
  known-good `0x1fdf` baseline.
- This was a real cleanup step, but it is **not** the remaining blocker.
- The next blocker is still earlier: Enc2 is launched, phase 1/2 callbacks
  happen, but no stream payload is ever written into the buffer.

### Best next target after this exact state

Focus on why the stream buffer remains untouched even with `w1e` restored:

1. compare current post-Enc2 launch sequencing against the flat legacy path,
   especially anything done around separate Enc2 submission and post-`CL_PUSH`
   AVPU state
2. inspect whether the current path is still materializing the wrong standalone
   Enc2 slice/grid fields besides `0xfc`
3. revisit cacheability / writeback assumptions for the stream destination and
   intermediate data, since the legacy `bc7c4cb` notes explicitly call out T31
   cache-flush failure as a stream-corruption root cause
