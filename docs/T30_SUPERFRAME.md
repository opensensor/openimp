# T30: IMP_Encoder_Get/SetSuperFrameCfg

Vendor T30 1.0.5 libimp (Ghidra: `IMP_Encoder_SetSuperFrameCfg` 0xc4c60, `GetSuperFrameCfg` 0xc4eb8,
`i264e_super_rc_set/get` 0x5c300/0x5c23c, `i264e_validate_parameters` 0x588ec, `channel_i264e_encoder_init` 0xb7f40,
`IMP_Encoder_CreateChn` 0xbb2b4). Camera A/B (T30X lab camera, audit/t30/b1-all, 640x360 H.264 channel): vendor Get before any Set
mode 2 (REENCODE), I 942080, P 672914, priority 1; a Set of mode 1 / I 400000 / P 200000 returns 0 and a Get right after it still
shows the old values.

## Vendor behaviour (what OpenIMP reproduces)

- Channel >= 6 or NULL: -1. A JPEG channel or a channel that is not created: 0, the structure is not touched (Get) / nothing stored (Set).
- Start values (`i264e_param_default` + `i264e_validate_parameters`): mode 2, priority 1; I = clip((stream buffer - 0x1080) * 8,
  area * 3 / 50, area * 12 / (width > 800 ? 6 : 3)) with area = 16-aligned width * height and the default stream buffer
  `(w * h * 3 / 2) / (w <= 800 and h <= 600 ? 2 : 3) + 0x1080` (`IMP_Encoder_SetStreamBufSize` replaces it);
  P = clip(0xd64925, lo, (int)(I / 1.4f)); B = clip(0xa4d5ce, lo, (int)(P / 1.3f)).
- Set: mode and priority are clipped to 0..2, the I threshold to `(stream buffer - 0x1080) * 8`, P and B are stored as given.
- The OEM stages the values (flag 0x2000) and the rate controller takes them with the next picture. OpenIMP stores and shows them at once.

## Not ported

The OEM controller re-encodes (mode 2) or drops (mode 1) a picture that exceeds the threshold. OpenIMP's T30 controller
(`src/t40/t31_rate_control.c`) has no such step, so the configuration is stored and returned but does not change the stream.
The "vendor OR BETTER" rule is met for the API (Get/Set); the effect needs RE of the OEM `i264e_ratecontrol_is_reenc` (0x65b68) and a controller change.
