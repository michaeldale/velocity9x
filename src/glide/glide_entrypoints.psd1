# Glide 2.x exports of GLIDE2X.DLL (docs\plans\glide-2x-wrapper.md).
#
# Names and argument bytes are the export tables of the two retail
# GLIDE2X.DLL files on the Need for Speed II SE disc (SETUP\GLIDENR, Voodoo
# Graphics, and SETUP\GLIDERSH, Voodoo Rush), read with wdump -e on
# 2026-10-08: both export the same 130 stdcall names with the same byte
# counts. Facts only; nothing of either image is used.
#
# Return is the API's return type as the stub must honour it: a float comes
# back in ST(0), not EAX, so a float function cannot share the integer stub.
# Every other return is void or 32 bits in EAX.
#
# Kind 'Written' is defined by hand in glide_dll.c; Kind 'Stub' is generated
# by scripts\lib\glide-exports.ps1 (log the first call, count, return zero).
# The 50 Written entries are exactly the Glide imports of NFS2SEA.EXE on the
# same disc, read from its import names on 2026-10-08 (Phase 0 census).
@{
    Entries = @(
        @{ Name = 'ConvertAndDownloadRle'; Bytes = 64; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAADrawLine'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAADrawPoint'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAADrawPolygon'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAADrawPolygonVertexList'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAADrawTriangle'; Bytes = 24; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAlphaBlendFunction'; Bytes = 16; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grAlphaCombine'; Bytes = 20; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grAlphaControlsITRGBLighting'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grAlphaTestFunction'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grAlphaTestReferenceValue'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grBufferClear'; Bytes = 12; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grBufferNumPending'; Bytes = 0; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grBufferSwap'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grCheckForRoom'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grChromakeyMode'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grChromakeyValue'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grClipWindow'; Bytes = 16; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grColorCombine'; Bytes = 20; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grColorMask'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grConstantColorValue'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grConstantColorValue4'; Bytes = 16; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grCullMode'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDepthBiasLevel'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDepthBufferFunction'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDepthBufferMode'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDepthMask'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDisableAllEffects'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grDitherMode'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDrawLine'; Bytes = 8; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDrawPlanarPolygon'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grDrawPlanarPolygonVertexList'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grDrawPoint'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grDrawPolygon'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grDrawPolygonVertexList'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grDrawTriangle'; Bytes = 12; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grErrorSetCallback'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grFogColorValue'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grFogMode'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grFogTable'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grGammaCorrectionValue'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grGlideGetState'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grGlideGetVersion'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grGlideInit'; Bytes = 0; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grGlideSetState'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grGlideShamelessPlug'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grGlideShutdown'; Bytes = 0; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grHints'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grLfbConstantAlpha'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grLfbConstantDepth'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grLfbLock'; Bytes = 24; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grLfbReadRegion'; Bytes = 28; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grLfbUnlock'; Bytes = 8; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grLfbWriteColorFormat'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grLfbWriteColorSwizzle'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grLfbWriteRegion'; Bytes = 32; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grRenderBuffer'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grResetTriStats'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSplash'; Bytes = 20; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSstConfigPipeline'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSstControl'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grSstIdle'; Bytes = 0; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grSstIsBusy'; Bytes = 0; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grSstOrigin'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSstPerfStats'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSstQueryBoards'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grSstQueryHardware'; Bytes = 4; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grSstResetPerfStats'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSstScreenHeight'; Bytes = 0; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grSstScreenWidth'; Bytes = 0; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grSstSelect'; Bytes = 4; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grSstStatus'; Bytes = 0; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grSstVideoLine'; Bytes = 0; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grSstVidMode'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grSstVRetraceOn'; Bytes = 0; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grSstWinClose'; Bytes = 0; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grSstWinOpen'; Bytes = 28; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grTexCalcMemRequired'; Bytes = 16; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grTexClampMode'; Bytes = 12; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexCombine'; Bytes = 28; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexCombineFunction'; Bytes = 8; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexDetailControl'; Bytes = 16; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexDownloadMipMap'; Bytes = 16; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexDownloadMipMapLevel'; Bytes = 32; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexDownloadMipMapLevelPartial'; Bytes = 40; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexDownloadTable'; Bytes = 12; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexDownloadTablePartial'; Bytes = 20; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexFilterMode'; Bytes = 12; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexLodBiasValue'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexMaxAddress'; Bytes = 4; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grTexMinAddress'; Bytes = 4; Return = 'u32'; Kind = 'Written' }
        @{ Name = 'grTexMipMapMode'; Bytes = 12; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexMultibase'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexMultibaseAddress'; Bytes = 20; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexNCCTable'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'grTexSource'; Bytes = 16; Return = 'void'; Kind = 'Written' }
        @{ Name = 'grTexTextureMemRequired'; Bytes = 8; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'grTriStats'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'gu3dfGetInfo'; Bytes = 8; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'gu3dfLoad'; Bytes = 8; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guAADrawTriangleWithClip'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guAlphaSource'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guColorCombineFunction'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guDrawPolygonVertexListWithClip'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guDrawTriangleWithClip'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guEncodeRLE16'; Bytes = 16; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guEndianSwapBytes'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guEndianSwapWords'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guFogGenerateExp'; Bytes = 8; Return = 'void'; Kind = 'Written' }
        @{ Name = 'guFogGenerateExp2'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guFogGenerateLinear'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guFogTableIndexToW'; Bytes = 4; Return = 'float'; Kind = 'Stub' }
        @{ Name = 'guMovieSetName'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guMovieStart'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guMovieStop'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guMPDrawTriangle'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guMPInit'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guMPTexCombineFunction'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guMPTexSource'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guTexAllocateMemory'; Bytes = 60; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guTexChangeAttributes'; Bytes = 48; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guTexCombineFunction'; Bytes = 8; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guTexCreateColorMipMap'; Bytes = 0; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guTexDownloadMipMap'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guTexDownloadMipMapLevel'; Bytes = 12; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guTexGetCurrentMipMap'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guTexGetMipMapInfo'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guTexMemQueryAvail'; Bytes = 4; Return = 'u32'; Kind = 'Stub' }
        @{ Name = 'guTexMemReset'; Bytes = 0; Return = 'void'; Kind = 'Stub' }
        @{ Name = 'guTexSource'; Bytes = 4; Return = 'void'; Kind = 'Stub' }
    )
}
