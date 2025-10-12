# Thingino Streamer Setup with OpenIMP

This guide explains how to use the OpenIMP libraries with the thingino-streamer binary.

## What Was Added

We've implemented **22 missing symbols** that the thingino-streamer binary requires:

- **11 ISP Tuning Get functions** - Image quality parameter getters
- **5 Encoder functions** - Advanced encoder configuration  
- **6 Font rendering functions** - Text/OSD font support (sft_* functions)

See [ADDED_SYMBOLS.md](ADDED_SYMBOLS.md) for detailed documentation of each function.

## Build Instructions

### 1. Build OpenIMP Libraries

```bash
cd /home/matteius/openimp
make clean
make
```

This creates:
- `lib/libimp.so` - Main IMP library with ISP and Encoder functions
- `lib/libsysutils.so` - Sysutils library with font rendering functions

### 2. Verify Symbols

```bash
./verify_symbols.sh
```

You should see:
```
✓ All required symbols are available!
```

### 3. Install Libraries (Optional)

To install system-wide:

```bash
sudo make install PREFIX=/usr/local
```

Or to a custom location:

```bash
make install PREFIX=/path/to/install
```

## Using with Thingino Streamer

### Option 1: Use LD_LIBRARY_PATH

```bash
cd /home/matteius/openimp/thingino-streamer
LD_LIBRARY_PATH=/home/matteius/openimp/lib:$LD_LIBRARY_PATH ./streamer
```

### Option 2: Copy Libraries to System Path

```bash
# Copy to device's library path
cp /home/matteius/openimp/lib/libimp.so /usr/lib/
cp /home/matteius/openimp/lib/libsysutils.so /usr/lib/
ldconfig  # Update library cache

# Now run streamer normally
cd /home/matteius/openimp/thingino-streamer
./streamer
```

### Option 3: Copy Libraries to Streamer Directory

```bash
cd /home/matteius/openimp/thingino-streamer
cp ../lib/libimp.so .
cp ../lib/libsysutils.so .

# Run with local libraries
LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH ./streamer
```

## Cross-Compilation for Target Device

If you need to cross-compile for the actual T31 device:

```bash
# Set your cross-compiler
export CROSS_COMPILE=mipsel-linux-

# Build for T31
make clean
make PLATFORM=T31 CROSS_COMPILE=$CROSS_COMPILE

# Strip debug symbols for smaller size
make strip
```

Then copy the libraries to your device:

```bash
scp lib/libimp.so root@<device-ip>:/usr/lib/
scp lib/libsysutils.so root@<device-ip>:/usr/lib/
```

## Troubleshooting

### Symbol Still Missing

If you get "symbol not found" errors:

1. Verify the symbol is exported:
   ```bash
   nm -D lib/libimp.so | grep <symbol_name>
   nm -D lib/libsysutils.so | grep <symbol_name>
   ```

2. Check library path:
   ```bash
   ldd ./streamer
   ```

3. Make sure both libraries are in the same directory or in LD_LIBRARY_PATH

### Streamer Crashes or Hangs

The current implementation provides stub functions that return sensible defaults. Some limitations:

1. **ISP Get Functions**: Return default values, don't read from actual hardware
2. **Encoder Functions**: Store configuration but don't configure actual hardware encoder
3. **Font Functions**: Provide API but don't render actual glyphs (clear area instead)

For production use, you may need to:
- Integrate with actual ISP hardware via `/dev/tx-isp` ioctls
- Implement real hardware encoder configuration
- Add a real font rendering library (FreeType, stb_truetype, etc.)

### Check What Symbols Streamer Needs

```bash
# List all undefined symbols in streamer binary
nm -u ./streamer | grep IMP_
nm -u ./streamer | grep sft_
```

## Implementation Status

### ✅ Fully Implemented
- All 22 required symbols are exported
- Libraries compile without errors
- Symbols are properly linked

### ⚠️ Stub Implementations
- ISP Get functions return defaults (not reading from hardware)
- Font rendering functions provide API but don't render actual glyphs
- Encoder functions store config but don't configure hardware

### 🔧 For Production
To make this production-ready, you would need to:

1. **ISP Functions**: Add ioctl calls to `/dev/tx-isp` to read actual hardware state
2. **Font Rendering**: Integrate FreeType or stb_truetype for actual glyph rendering
3. **Encoder**: Add hardware encoder configuration via AL_Codec interface

## Testing

Run the verification script to ensure all symbols are available:

```bash
./verify_symbols.sh
```

Expected output:
```
Verifying symbols for thingino-streamer...

✓ IMP_ISP_Tuning_GetContrast (libimp.so)
✓ IMP_ISP_Tuning_GetBacklightComp (libimp.so)
✓ sft_render (libsysutils.so)
... (all 22 symbols)

Summary:
  Found: 22/22
  Missing: 0/22

✓ All required symbols are available!
```

## Next Steps

1. **Test the streamer**: Try running it with the OpenIMP libraries
2. **Check logs**: Look for any runtime errors or warnings
3. **Monitor behavior**: See if video streaming works as expected
4. **Iterate**: If issues arise, we can enhance the stub implementations

## References

- [ADDED_SYMBOLS.md](ADDED_SYMBOLS.md) - Detailed documentation of added symbols
- [CURRENT_STATUS.md](CURRENT_STATUS.md) - Overall OpenIMP implementation status
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Technical implementation details

## Support

If you encounter issues:

1. Check the error message for specific missing symbols
2. Verify libraries are in the correct path
3. Use `ldd` to check library dependencies
4. Check `dmesg` for kernel-level errors (if on actual device)

