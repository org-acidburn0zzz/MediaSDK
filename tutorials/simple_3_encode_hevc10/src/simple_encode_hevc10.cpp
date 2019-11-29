// Copyright (c) 2019 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "common_utils.h"
#include "cmd_options.h"

static void usage(CmdOptionsCtx* ctx)
{
    printf(
        "Encodes INPUT and optionally writes OUTPUT. If INPUT is not specified\n"
        "simulates input with empty frames filled with the color.\n"
        "\n"
        "Usage: %s [options] [INPUT] [OUTPUT]\n", ctx->program);
}

int main(int argc, char** argv)
{
    mfxStatus sts = MFX_ERR_NONE;
    bool bEnableInput;  // if true, removes all YUV file reading (which is replaced by pre-initialized surface data). Workload runs for 1000 frames.
    bool bEnableOutput; // if true, removes all output bitsteam file writing and printing the progress
    CmdOptions options;

    // =====================================================================
    // Intel Media SDK encode pipeline setup
    // - In this example we are encoding an HEVC (H.265) 10bit stream
    // - For simplistic memory management, system memory surfaces are used to store the raw frames
    //   (Note that when using HW acceleration video surfaces are prefered, for better performance)
    // - This program is used for HEVC 10bit, so please make sure the YUV file is in P010 format
    // - For P010 format, refer to this article:
    //   https://msdn.microsoft.com/en-us/library/windows/desktop/bb970578(v=vs.85).aspx

    //1. Read options from the command line (if any is given)
    memset(&options, 0, sizeof(CmdOptions));
    options.ctx.options = OPTIONS_ENCODE;
    options.ctx.usage = usage;
    // Set default values:
    options.values.impl = MFX_IMPL_AUTO_ANY;

    // here we parse options
    ParseOptions(argc, argv, &options);

    if (!options.values.Width || !options.values.Height) {
        printf("error: input video geometry not set (mandatory)\n");
        return -1;
    }
    if (!options.values.Bitrate) {
        printf("error: bitrate not set (mandatory)\n");
        return -1;
    }
    if (!options.values.FrameRateN || !options.values.FrameRateD) {
        printf("error: framerate not set (mandatory)\n");
        return -1;
    }
    if (!options.values.c10bit) {
        printf("error: 10bit not set (mandatory)\n");
        return -1;
    }

    bEnableInput = (options.values.SourceName[0] != '\0');
    bEnableOutput = (options.values.SinkName[0] != '\0');
    // Open input P010 YUV file
    fileUniPtr fSource(nullptr, &CloseFile);
    if (bEnableInput) {
        fSource.reset(OpenFile(options.values.SourceName, "rb"));
        MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);
    }
    // Create output elementary stream (ES) H.265 file
    fileUniPtr fSink(nullptr, &CloseFile);
    if (bEnableOutput) {
        fSink.reset(OpenFile(options.values.SinkName, "wb"));
        MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);
    }

    //2. Initialize Intel Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW acceleration if available (on any adapter)
    // - Version 1.0 is selected for greatest backwards compatibility.
    // OS specific notes
    // - On Windows both SW and HW libraries may present
    // - On Linux only HW library only is available
    //   If more recent API features are needed, change the version accordingly
    mfxIMPL impl = options.values.impl;
    mfxVersion ver = { { 0, 1 } };
    MFXVideoSession session;

    sts = Initialize(impl, ver, &session, NULL);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK encoder
    MFXVideoENCODE mfxENC(session);

    //3. Set required video parameters for encode
    // - In this example we are encoding an HEVC (H.265) 10bit stream
    mfxVideoParam mfxEncParams;
    memset(&mfxEncParams, 0, sizeof(mfxEncParams));

    mfxEncParams.mfx.CodecId = MFX_CODEC_HEVC;
    mfxEncParams.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN10; //Parameter for HEVC 10bit
    mfxEncParams.mfx.CodecLevel = MFX_LEVEL_HEVC_51; //Parameter for HEVC 10bit
    mfxEncParams.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams.mfx.TargetKbps = options.values.Bitrate;
    mfxEncParams.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
    mfxEncParams.mfx.FrameInfo.FrameRateExtN = options.values.FrameRateN;
    mfxEncParams.mfx.FrameInfo.FrameRateExtD = options.values.FrameRateD;
    mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_P010; //Parameter for HEVC 10bit
    mfxEncParams.mfx.FrameInfo.BitDepthChroma = 10; //Parameter for HEVC 10bit
    mfxEncParams.mfx.FrameInfo.BitDepthLuma = 10; //Parameter for HEVC 10bit
    mfxEncParams.mfx.FrameInfo.Shift = 1; //Parameter for HEVC 10bit
    mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    mfxEncParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams.mfx.FrameInfo.CropX = 0;
    mfxEncParams.mfx.FrameInfo.CropY = 0;
    mfxEncParams.mfx.FrameInfo.CropW = options.values.Width;
    mfxEncParams.mfx.FrameInfo.CropH = options.values.Height;
    // Width must be a multiple of 16
    // Height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(options.values.Width);
    mfxEncParams.mfx.FrameInfo.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams.mfx.FrameInfo.PicStruct) ?
        MSDK_ALIGN16(options.values.Height) :
        MSDK_ALIGN32(options.values.Height);

    mfxEncParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    //4. Load the HEVC plugin
    mfxPluginUID codecUID;
    bool success = true;
    codecUID = msdkGetPluginUID(MFX_IMPL_HARDWARE, MSDK_VENCODE, mfxEncParams.mfx.CodecId);
    if (AreGuidsEqual(codecUID, MSDK_PLUGINGUID_NULL)) {
        printf("Get Plugin UID for HEVC is failed.\n");
        success = false;
    }

    printf("Loading HEVC plugin: %s\n", ConvertGuidToString(codecUID));

    //On the success of get the UID, load the plugin
    if (success) {
        sts = MFXVideoUSER_Load(session, &codecUID, ver.Major);
        if (sts < MFX_ERR_NONE) {
            printf("Loading HEVC plugin failed\n");
            success = false;
        }
    }

    // Validate video encode parameters (optional)
    // - In this example the validation result is written to same structure
    // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video parameters are not supported,
    //   instead the encoder will select suitable parameters closest matching the requested configuration
    sts = mfxENC.Query(&mfxEncParams, &mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query number of required surfaces for encoder
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = mfxENC.QueryIOSurf(&mfxEncParams, &EncRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxU16 nEncSurfNum = EncRequest.NumFrameSuggested;

    //5. Allocate surfaces for encoder
    // - Width and height of buffer must be aligned, a multiple of 32
    // - Frame surface array keeps pointers all surface planes and general frame info
    // - For HEVC 10 bit, the bytes per pixel is doubled, so the width is multiplied by 2.
    mfxU16 width = (mfxU16) MSDK_ALIGN32(EncRequest.Info.Width * 2);
    mfxU16 height = (mfxU16) MSDK_ALIGN32(EncRequest.Info.Height);
    mfxU8 bitsPerPixel = 12;        // P010 format is a 24 bits per pixel format but we have double the width
    mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
    std::vector<mfxU8> surfaceBuffersData(surfaceSize * nEncSurfNum);
    mfxU8* surfaceBuffers = surfaceBuffersData.data();

    // Allocate surface headers (mfxFrameSurface1) for encoder, noticed the width has been doubled for P010.
    std::vector<mfxFrameSurface1> pEncSurfaces(nEncSurfNum);
    for (int i = 0; i < nEncSurfNum; i++) {
        memset(&pEncSurfaces[i], 0, sizeof(mfxFrameSurface1));
        pEncSurfaces[i].Info = mfxEncParams.mfx.FrameInfo;
        pEncSurfaces[i].Data.Y = &surfaceBuffers[surfaceSize * i];
        pEncSurfaces[i].Data.U = pEncSurfaces[i].Data.Y + width * height;
        pEncSurfaces[i].Data.V = pEncSurfaces[i].Data.U + 1;
        pEncSurfaces[i].Data.Pitch = width;
        if (!bEnableInput) {
            ClearYUVSurfaceSysMem(&pEncSurfaces[i], width, height);
        }
    }

    //6. Initialize the Media SDK encoder
    sts = mfxENC.Init(&mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    mfxVideoParam par;
    memset(&par, 0, sizeof(par));
    sts = mfxENC.GetVideoParam(&par);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    //7. Prepare Media SDK bit stream buffer
    mfxBitstream mfxBS;
    memset(&mfxBS, 0, sizeof(mfxBS));
    mfxBS.MaxLength = par.mfx.BufferSizeInKB * 1000;
    std::vector<mfxU8> bstData(mfxBS.MaxLength);
    mfxBS.Data = bstData.data();

    // ===================================
    //8. Start encoding the frames
    //

    mfxTime tStart, tEnd;
    mfxGetTime(&tStart);

    int nEncSurfIdx = 0;
    mfxSyncPoint syncp;
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main encoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts) {
        nEncSurfIdx = GetFreeSurfaceIndex(pEncSurfaces);   // Find free frame surface
        MSDK_CHECK_ERROR(MFX_ERR_NOT_FOUND, nEncSurfIdx, MFX_ERR_MEMORY_ALLOC);

        sts = LoadRaw10BitFrame(&pEncSurfaces[nEncSurfIdx], fSource.get());
        MSDK_BREAK_ON_ERROR(sts);

        for (;;) {
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, &pEncSurfaces[nEncSurfIdx], &mfxBS, &syncp);

            if (MFX_ERR_NONE < sts && !syncp) {     // Repeat the call if warning and no output
                if (MFX_WRN_DEVICE_BUSY == sts)
                    MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
            } else if (MFX_ERR_NONE < sts && syncp) {
                sts = MFX_ERR_NONE;     // Ignore warnings if output is available
                break;
            } else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
                // Allocate more bitstream buffer memory here if needed...
                break;
            } else
                break;
        }

        if (MFX_ERR_NONE == sts) {
            sts = session.SyncOperation(syncp, 60000);      // Synchronize. Wait until encoded frame is ready
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            ++nFrame;
            if (bEnableOutput) {
                sts = WriteBitStreamFrame(&mfxBS, fSink.get());
                MSDK_BREAK_ON_ERROR(sts);

                printf("Frame number: %d\r", nFrame);
                fflush(stdout);
            } else mfxBS.DataLength = 0;
        }
    }

    // MFX_ERR_MORE_DATA means that the input file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    //
    // Stage 2: Retrieve the buffered encoded frames
    //
    while (MFX_ERR_NONE <= sts) {
        for (;;) {
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, NULL, &mfxBS, &syncp);

            if (MFX_ERR_NONE < sts && !syncp) {     // Repeat the call if warning and no output
                if (MFX_WRN_DEVICE_BUSY == sts)
                    MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
            } else if (MFX_ERR_NONE < sts && syncp) {
                sts = MFX_ERR_NONE;     // Ignore warnings if output is available
                break;
            } else
                break;
        }

        if (MFX_ERR_NONE == sts) {
            sts = session.SyncOperation(syncp, 60000);      // Synchronize. Wait until encoded frame is ready
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            ++nFrame;
            if (bEnableOutput) {
                sts = WriteBitStreamFrame(&mfxBS, fSink.get());
                MSDK_BREAK_ON_ERROR(sts);

                printf("Frame number: %d\r", nFrame);
                fflush(stdout);
            } else mfxBS.DataLength = 0;
        }
    }

    // MFX_ERR_MORE_DATA indicates that there are no more buffered frames, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxGetTime(&tEnd);
    double elapsed = TimeDiffMsec(tEnd, tStart) / 1000;
    double fps = ((double)nFrame / elapsed);
    printf("\nExecution time: %3.2f s (%3.2f fps)\n", elapsed, fps);

    // ===================================================================
    //9. Clean up resources
    //  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
    //    some surfaces may still be locked by internal Media SDK resources.

    mfxENC.Close();
    // session closed automatically on destruction

    Release();

    return 0;
}
