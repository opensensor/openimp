# T40 tests

The stable interactive profiles are:

- `profiles/raptor-live-640.conf`: 640x360 at 30 fps
- `profiles/raptor-live-720.conf`: 1280x720 at 30 fps
- `profiles/raptor-live-1080.conf`: 1920x1080 at 15 fps
- `profiles/raptor-live-1440.conf`: 2560x1440 at 15 fps

`profiles/raptor-dual.conf` exercises the broader producer topology. The
single-stream profiles intentionally exercise the same implementation at
different configured dimensions; the T40 capture and encoder paths do not
select resolution-specific templates.

The source probes under `probes/` correspond to the P0-P2 bring-up gates:

- `p0_system_probe.c`: System state and repeated lifecycle
- `p1_first_frame_probe.c`: ISP/FrameSource first-frame capture
- `p2_public_probe.c`: public encoder lifecycle
- `p2_backend_probe.c`: lower-level recovered codec backend

Device runs require the target firmware's stock TX-ISP and sensor modules plus
the matching AVPU module.
The probe library path can be passed as the first argument; probes that also
compare against the OEM implementation expect it at `/usr/lib/libimp.so`.
