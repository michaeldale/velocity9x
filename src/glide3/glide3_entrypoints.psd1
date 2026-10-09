# Glide 3.x exports of GLIDE3X.DLL (docs\plans\glide-3x-wrapper.md).
#
# Names and argument bytes are the export table of 3dfx's own GLIDE3X.DLL
# (the Voodoo3/4/5 Win9x driver), read with wdump -e on 2026-10-10: 99
# stdcall entry points, the gdbg_* debug helpers left out. Return types are
# the prototypes in 3dfx's glide.h and glideutl.h; grSstConfigPipeline,
# grSstVidMode and the tx* texture utilities have no public prototype and
# are treated as void. Facts only; nothing of either is used.
#
# Return is what a stub must honour: a float comes back in ST(0), not EAX.
# Pointer, handle, FxBool and integer returns are all 32 bits in EAX (u32).
#
# Use 'Diablo' marks the 36 Glide imports of Diablo II's D2Glide.dll (the
# 1.0 shareware demo), read from its import names on 2026-10-10; its
# D2VidTst.exe imports 10 of the same. The census logs every export.
@{
    Entries = @(
        @{ Name = 'grAADrawTriangle'; Bytes = 24; Return = 'void'; Use = 'Other' }
        @{ Name = 'grAlphaBlendFunction'; Bytes = 16; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grAlphaCombine'; Bytes = 20; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grAlphaControlsITRGBLighting'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grAlphaTestFunction'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grAlphaTestReferenceValue'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grBufferClear'; Bytes = 12; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grBufferSwap'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grChromakeyMode'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grChromakeyValue'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grClipWindow'; Bytes = 16; Return = 'void'; Use = 'Other' }
        @{ Name = 'grColorCombine'; Bytes = 20; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grColorMask'; Bytes = 8; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grConstantColorValue'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grCoordinateSpace'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grCullMode'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDepthBiasLevel'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDepthBufferFunction'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDepthBufferMode'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDepthMask'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grDepthRange'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDisable'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDisableAllEffects'; Bytes = 0; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDitherMode'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grDrawLine'; Bytes = 8; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grDrawPoint'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grDrawTriangle'; Bytes = 12; Return = 'void'; Use = 'Other' }
        @{ Name = 'grDrawVertexArray'; Bytes = 12; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grDrawVertexArrayContiguous'; Bytes = 16; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grEnable'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grErrorSetCallback'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grFinish'; Bytes = 0; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grFlush'; Bytes = 0; Return = 'void'; Use = 'Other' }
        @{ Name = 'grFogColorValue'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grFogMode'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grFogTable'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grGet'; Bytes = 12; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grGetProcAddress'; Bytes = 4; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grGetString'; Bytes = 4; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grGlideGetState'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grGlideGetVertexLayout'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grGlideInit'; Bytes = 0; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grGlideSetState'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grGlideSetVertexLayout'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grGlideShutdown'; Bytes = 0; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grLfbConstantAlpha'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grLfbConstantDepth'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grLfbLock'; Bytes = 24; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grLfbReadRegion'; Bytes = 28; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grLfbUnlock'; Bytes = 8; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grLfbWriteColorFormat'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grLfbWriteColorSwizzle'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'grLfbWriteRegion'; Bytes = 36; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grLoadGammaTable'; Bytes = 16; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grQueryResolutions'; Bytes = 8; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grRenderBuffer'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grReset'; Bytes = 4; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grSelectContext'; Bytes = 4; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grSetNumPendingBuffers'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grSplash'; Bytes = 20; Return = 'void'; Use = 'Other' }
        @{ Name = 'grSstConfigPipeline'; Bytes = 12; Return = 'void'; Use = 'Other' }
        @{ Name = 'grSstOrigin'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grSstSelect'; Bytes = 4; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grSstVidMode'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'grSstWinClose'; Bytes = 4; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grSstWinOpen'; Bytes = 28; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grStippleMode'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grStipplePattern'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexCalcMemRequired'; Bytes = 16; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grTexClampMode'; Bytes = 12; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexCombine'; Bytes = 28; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grTexDetailControl'; Bytes = 16; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexDownloadMipMap'; Bytes = 16; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grTexDownloadMipMapLevel'; Bytes = 32; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexDownloadMipMapLevelPartial'; Bytes = 40; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grTexDownloadTable'; Bytes = 8; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grTexDownloadTablePartial'; Bytes = 16; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexFilterMode'; Bytes = 12; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grTexLodBiasValue'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexMaxAddress'; Bytes = 4; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grTexMinAddress'; Bytes = 4; Return = 'u32'; Use = 'Diablo' }
        @{ Name = 'grTexMipMapMode'; Bytes = 12; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexMultibase'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexMultibaseAddress'; Bytes = 20; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexNCCTable'; Bytes = 4; Return = 'void'; Use = 'Other' }
        @{ Name = 'grTexSource'; Bytes = 16; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grTexTextureMemRequired'; Bytes = 8; Return = 'u32'; Use = 'Other' }
        @{ Name = 'grVertexLayout'; Bytes = 12; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'grViewport'; Bytes = 16; Return = 'void'; Use = 'Other' }
        @{ Name = 'gu3dfGetInfo'; Bytes = 8; Return = 'u32'; Use = 'Other' }
        @{ Name = 'gu3dfLoad'; Bytes = 8; Return = 'u32'; Use = 'Other' }
        @{ Name = 'guFogGenerateExp'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'guFogGenerateExp2'; Bytes = 8; Return = 'void'; Use = 'Other' }
        @{ Name = 'guFogGenerateLinear'; Bytes = 12; Return = 'void'; Use = 'Other' }
        @{ Name = 'guFogTableIndexToW'; Bytes = 4; Return = 'float'; Use = 'Other' }
        @{ Name = 'guGammaCorrectionRGB'; Bytes = 12; Return = 'void'; Use = 'Diablo' }
        @{ Name = 'txImgQuantize'; Bytes = 24; Return = 'void'; Use = 'Other' }
        @{ Name = 'txMipQuantize'; Bytes = 20; Return = 'void'; Use = 'Other' }
        @{ Name = 'txPalToNcc'; Bytes = 8; Return = 'void'; Use = 'Other' }
    )
}
