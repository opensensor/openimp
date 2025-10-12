# Frame Release Fix for Software Frame Generation

## Problem

After implementing software frame generation mode, the streamer was still reporting **0.00 fps** even though frames were being generated and encoded. The logs showed:

```
[FrameSource] frame_capture_thread chn=1: SOFTWARE MODE - generated frame #1 (0x7787e250)
[Encoder] encoder_update: Frame available from FrameSource, frame=0x7787e250
[Codec] Process: SW encode frame 640x360
[HW_Encoder] Software encoding: frame 1, type=I, length=4096
...
[I 147634846] FRAME_MGR: Actual frame processing rate: 0.00 fps (0 frames in 5.01 seconds)
```

Only **2 frames total** were generated (one per channel), then frame generation stopped completely.

## Root Cause

The VBM (Video Buffer Manager) pool has a limited number of buffers per channel. When `VBMGetFrame()` was called, it showed:

```
[VBM] GetFrame: chn=1, frame=0x7787e250 (idx=0, 0 remaining)
[VBM] GetFrame: chn=0, frame=0x7771c1a0 (idx=0, 0 remaining)
```

**"0 remaining"** means the pool only has 1 buffer per channel. After that buffer was taken by the frame_capture_thread and passed to the encoder, there were no more buffers available for subsequent frames.

The frames were **never being released** back to the VBM pool. In the hardware mode, the thingino-streamer application calls `IMP_FrameSource_ReleaseFrame()` after processing each frame, but in software mode, this wasn't happening because the frames were being consumed internally by the encoder.

## Solution

Modified `encoder_update()` in `src/imp_encoder.c` to **automatically release frames** after encoding:

```c
static int encoder_update(void *module, void *frame) {
    // ... validation code ...

    int frame_processed = 0;

    /* Process frame in encoder */
    for (int i = 0; i < MAX_ENC_CHANNELS; i++) {
        EncChannel *chn = &g_EncChannel[i];

        if (chn->chn_id >= 0 && chn->recv_pic_started && chn->codec != NULL) {
            /* Queue frame to codec for encoding */
            if (AL_Codec_Encode_Process(chn->codec, frame, NULL) == 0) {
                LOG_ENC("encoder_update: Queued frame to channel %d", i);

                /* Signal eventfd to wake up stream thread */
                if (chn->eventfd >= 0) {
                    uint64_t val = 1;
                    ssize_t n = write(chn->eventfd, &val, sizeof(val));
                    (void)n;
                }

                frame_processed = 1;
                break;  /* Only process frame in one channel */
            }
        }
    }

    /* Release the frame back to VBM pool so it can be reused
     * This is critical for software frame generation mode where frames
     * are allocated from a limited pool and must be recycled. */
    extern int IMP_FrameSource_ReleaseFrame(int chnNum, void *frame);
    
    /* Extract channel number from module - for now we'll try both channels */
    for (int chn = 0; chn < 2; chn++) {
        if (IMP_FrameSource_ReleaseFrame(chn, frame) == 0) {
            LOG_ENC("encoder_update: Released frame %p back to channel %d", frame, chn);
            break;
        }
    }

    return frame_processed ? 0 : -1;
}
```

## Key Changes

1. **Added frame_processed flag**: Track whether the frame was successfully encoded
2. **Added break statement**: Only process frame in one encoder channel (prevents double-encoding)
3. **Added automatic frame release**: Call `IMP_FrameSource_ReleaseFrame()` after encoding to return the buffer to the VBM pool
4. **Try both channels**: Since we don't know which channel the frame came from, try releasing to both channels (the correct one will succeed)

## Expected Behavior

After this fix:

1. **Frame generation continues**: VBM buffers are recycled, allowing continuous frame generation at 20fps
2. **Frame manager reports correct fps**: Should show ~20.00 fps instead of 0.00 fps
3. **RTSP streaming works**: Clients can connect and receive video streams
4. **Logs show continuous activity**:
   ```
   [FrameSource] SOFTWARE MODE - generated frame #1
   [FrameSource] SOFTWARE MODE - generated frame #2
   [FrameSource] SOFTWARE MODE - generated frame #3
   ...
   [I timestamp] FRAME_MGR: Actual frame processing rate: 20.00 fps
   ```

## Testing

1. Copy the rebuilt `lib/libimp.so` to the device:
   ```bash
   scp lib/libimp.so root@<device-ip>:/usr/lib/
   ```

2. Restart the streamer:
   ```bash
   ssh root@<device-ip> "killall streamer; /usr/bin/streamer > /tmp/streamer.log 2>&1 &"
   ```

3. Monitor logs:
   ```bash
   ssh root@<device-ip> "tail -f /tmp/streamer.log | grep -E '(SOFTWARE MODE|frame processing rate)'"
   ```

4. Test RTSP stream:
   ```bash
   ffplay rtsp://<device-ip>:554/ch0
   ```

## Related Fixes

This fix builds on previous fixes:

1. **Software Frame Generation** (`FRAMESOURCE_FIX.md`): Added automatic software mode when hardware is unavailable
2. **Encoder Buffer Management** (`ENCODER_FIX.md`): Fixed dynamic allocation of stream buffers
3. **Frame Release** (this fix): Ensures frames are recycled back to the VBM pool

All three fixes are required for the software-only mode to work correctly.

## Files Modified

1. **`src/imp_encoder.c`** - Added automatic frame release in `encoder_update()`
2. **`src/imp_framesource.c`** - Added `IMP_FrameSource_ReleaseFrame()` function

## Symbol Export

The `IMP_FrameSource_ReleaseFrame` function is now properly exported from `libimp.so`:

```bash
$ nm -D lib/libimp.so | grep ReleaseFrame
0000000000009c40 T IMP_FrameSource_ReleaseFrame
```

This allows the encoder to call it to release frames back to the VBM pool.

## Build

```bash
cd /home/matteius/openimp
make clean && make
```

## Deployment

Copy the rebuilt library to your device:

```bash
scp lib/libimp.so root@<device-ip>:/usr/lib/
ssh root@<device-ip> "killall streamer; /usr/bin/streamer > /tmp/streamer.log 2>&1 &"
```

Or use the deployment script:

```bash
./deploy_to_device.sh <device-ip>
```

The library is ready to deploy! 🎉

