// Constant buffer layouts shared between C++ and HLSL.
// Only 16-byte vector members are used so that C++ and HLSL packing match.
#ifndef BGN_SHADER_SHARED_H
#define BGN_SHADER_SHARED_H

#ifdef __cplusplus
#include <cstdint>
namespace bgn::gpu {
struct float4 { float x = 0, y = 0, z = 0, w = 0; };
struct uint4 { uint32_t x = 0, y = 0, z = 0, w = 0; };
#define BGN_CBUFFER(name, reg) struct name
#else
#define BGN_CBUFFER(name, reg) cbuffer name : register(reg)
#endif

// Flags in FrameCB::gFrame.w
#define BGN_FLAG_FLOW_VALID        (1u << 0)
#define BGN_FLAG_HISTORY_VALID     (1u << 1)
#define BGN_FLAG_TEXT_BOOST        (1u << 2)
#define BGN_FLAG_SKIN_PROTECT      (1u << 3)
#define BGN_FLAG_MOTION_DEBLUR     (1u << 4)
#define BGN_FLAG_COMPARE_SPLIT     (1u << 5)
#define BGN_FLAG_ADAPTIVE_SHARPEN  (1u << 6)
#define BGN_FLAG_CLEANUP_HQ        (1u << 7)
#define BGN_FLAG_DARK_CLEANUP      (1u << 8)
#define BGN_FLAG_COLOR_AUTO        (1u << 9)
#define BGN_FLAG_PREV_FINAL_VALID  (1u << 10)

// OSD text capacity: 16 chars per uint4
#define BGN_OSD_TEXT_VECTORS 12
#define BGN_OSD_MAX_CHARS (BGN_OSD_TEXT_VECTORS * 16)

// Values of FrameCB::gFrame.y (input encoding of the captured surface)
#define BGN_INPUT_SDR    0u   // 8-bit sRGB (BGRA8)
#define BGN_INPUT_SCRGB  1u   // FP16 scRGB linear (HDR window capture)

// Values of FrameCB::gFrame.z (output encoding)
#define BGN_OUTPUT_SDR   0u   // gamma sRGB 0..1 (R10G10B10A2 / BGRA8 swap chain)
#define BGN_OUTPUT_SCRGB 1u   // linear scRGB (FP16 swap chain, HDR display)

BGN_CBUFFER(FrameCB, b0) {
    float4 gIn;       // processing input  w, h, 1/w, 1/h
    float4 gOut;      // processing output w, h, 1/w, 1/h
    float4 gSrc;      // crop offset x, y in the capture texture; capture texture w, h
    uint4  gFrame;    // frame index, input encoding, output encoding, flags
    float4 gCleanup;  // deblock, deband, denoise, noise sigma estimate
    float4 gTemporal; // strength, clamp gamma, static boost, reserved
    float4 gDeblur;   // strength, motion strength, edge weight, detail weight
    float4 gSharpen;  // strength, output px per input px, text boost, skin protect strength
    float4 gColor0;   // black level, white level, contrast, gamma
    float4 gColor1;   // saturation, vibrance, temperature, highlight recovery
    float4 gColor2;   // shadow detail, local contrast, tone mapping (0/1), color enabled (0/1)
    float4 gAuto;     // auto black point, auto white point, auto amount, scene average luma
    float4 gHdr;      // paper white nits, display peak nits, HDR+ intensity, HDR+ enabled (0/1)
    float4 gFlow;     // flow grid w, h, input px per flow cell, reserved
    float4 gInterp;   // t, static threshold, occlusion threshold, reserved
    float4 gPresent;  // destination rect in back buffer: x, y, w, h
    float4 gPresent2; // back buffer w, h, dither amplitude, compare split position (0..1)
    float4 gStyle;    // monochrome 0..1, split toning 0..1, night light 0..1, color vision mode (0 off, 1 protan, 2 deutan, 3 tritan)
    float4 gStyle2;   // color vision correction strength 0..1, reserved x3
    float4 gOsd;      // OSD origin x, y (back buffer px), pixel scale, enabled (0/1)
    float4 gOsd2;     // OSD columns, rows, background opacity, reserved
    uint4  gOsdText[BGN_OSD_TEXT_VECTORS]; // ASCII, 4 chars per uint (little endian), '\n' = new line
};

// Per-dispatch parameters
BGN_CBUFFER(PassCB, b1) {
    uint4  gPassI;    // generic integer params (e.g. level size, search radius)
    float4 gPassF;    // generic float params
    float4 gPassF2;
};

#ifdef __cplusplus
} // namespace bgn::gpu
#endif

#endif
