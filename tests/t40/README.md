# T40 tests

`profiles/raptor-live-640.conf` is the stable interactive profile.
`profiles/raptor-live-1080.conf` reproduces the unresolved main-stream AVPU
payload/completion issue. `profiles/raptor-dual.conf` exercises the broader
producer topology.

The source probes under `probes/` correspond to the P0-P2 bring-up gates:

- `p0_system_probe.c`: System state and repeated lifecycle
- `p1_first_frame_probe.c`: ISP/FrameSource first-frame capture
- `p2_public_probe.c`: public encoder lifecycle
- `p2_backend_probe.c`: lower-level recovered codec backend

Device runs require the matching open TX-ISP, sensor, and AVPU modules.
The probe library path can be passed as the first argument; probes that also
compare against the OEM implementation expect it at `/usr/lib/libimp.so`.
