#!/bin/bash
# Verify that all required IMP symbols for thingino-streamer are now available

echo "Verifying IMP symbols for thingino-streamer..."
echo ""
echo "Note: sft_* font functions are already in the device's libsysutils.so"
echo ""

# List of required IMP symbols from the error message
REQUIRED_SYMBOLS=(
    "IMP_ISP_Tuning_GetContrast"
    "IMP_ISP_Tuning_GetBacklightComp"
    "IMP_ISP_Tuning_GetSaturation"
    "IMP_Encoder_SetChnQp"
    "IMP_ISP_Tuning_GetEVAttr"
    "IMP_Encoder_SetChnGopLength"
    "IMP_Encoder_SetChnEntropyMode"
    "IMP_ISP_Tuning_GetAeComp"
    "IMP_ISP_Tuning_GetSharpness"
    "IMP_ISP_Tuning_GetWB_Statis"
    "IMP_ISP_Tuning_GetHiLightDepress"
    "IMP_ISP_Tuning_GetWB_GOL_Statis"
    "IMP_ISP_Tuning_GetBrightness"
    "IMP_ISP_Tuning_GetBcshHue"
    "IMP_Encoder_SetMaxStreamCnt"
    "IMP_Encoder_SetStreamBufSize"
)

MISSING=0
FOUND=0

for symbol in "${REQUIRED_SYMBOLS[@]}"; do
    # Check in libimp.so
    if nm -D lib/libimp.so 2>/dev/null | grep -q " T $symbol$"; then
        echo "✓ $symbol"
        ((FOUND++))
    else
        echo "✗ $symbol - NOT FOUND"
        ((MISSING++))
    fi
done

echo ""
echo "Summary:"
echo "  Found: $FOUND/${#REQUIRED_SYMBOLS[@]}"
echo "  Missing: $MISSING/${#REQUIRED_SYMBOLS[@]}"
echo ""

if [ $MISSING -eq 0 ]; then
    echo "✓ All required IMP symbols are available!"
    echo ""
    echo "You can now copy lib/libimp.so to your device to replace the existing one."
    exit 0
else
    echo "✗ Some symbols are still missing"
    exit 1
fi

