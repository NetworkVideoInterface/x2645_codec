#pragma once

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <NVI/Codec.h>
#include <x265.h>
#include "adaption/Logging.h"

class X265Encoder final
{
public:
    X265Encoder();
    ~X265Encoder();

public:
    int32_t Config(const NVIVideoCodecParam& param);
    int32_t Encoding(const NVIVideoImageFrame& in, NVIVideoEncode::OnPacket out, void* user);
    void Release();

private:
    bool PicturePalneCopy(const NVIVideoImageFrame& in, x265_picture& out);

private:
    const x265_api* m_pAPI;
    x265_encoder* m_pHandle;
    x265_param* m_pParam;
    std::vector<std::unique_ptr<uint8_t[]>> m_vecStreamBuffer;

    const size_t kBufferSize = 2 * 1024 * 1024;
    const uint32_t kMaxFrameSize = 8192 * 8192;
    const uint32_t kMaxFrameRate = 60;
};

//////////////////////////////////////////////////////////////////////////

static int ToX265CSP(NVIPixelFormat format)
{
    switch (format)
    {
    case NVIPixel_I420: return X265_CSP_I420;
    case NVIPixel_420P10LE: return X265_CSP_I420;
    case NVIPixel_420P10BE: return X265_CSP_I420;
    case NVIPixel_422P: return X265_CSP_I422;
    case NVIPixel_422P10LE: return X265_CSP_I422;
    case NVIPixel_422P10BE: return X265_CSP_I422;
    default: return 0;
    }
}

static int FormatBitDepth(NVIPixelFormat format)
{
    if (format == NVIPixel_420P10LE || format == NVIPixel_420P10BE || format == NVIPixel_422P10LE || format == NVIPixel_422P10BE)
    {
        return 10;
    }
    return 8;
}

inline X265Encoder::X265Encoder()
    : m_pAPI(nullptr)
    , m_pHandle(nullptr)
    , m_pParam(nullptr)
{
}

inline X265Encoder::~X265Encoder()
{
    Release();
}

inline int32_t X265Encoder::Config(const NVIVideoCodecParam& param)
{
    Release();
    if (param.accel && param.accel->type > NVIAccel_Auto)
    {
        return -1;
    }
    if ((param.width * param.height) > kMaxFrameSize)
    {
        return -2;
    }
    int nCSP = ToX265CSP(static_cast<NVIPixelFormat>(param.format));
    if (nCSP == 0)
    {
        return -3;
    }
    const int nBitDepth = FormatBitDepth(static_cast<NVIPixelFormat>(param.format));
    m_pAPI = x265_api_get(nBitDepth);
    if (m_pAPI == nullptr)
    {
        m_pAPI = x265_api_get(0);
        if (m_pAPI == nullptr)
        {
            return -4;
        }
    }
    m_pParam = m_pAPI->param_alloc();
    m_pAPI->param_default(m_pParam);
    m_pAPI->param_default_preset(m_pParam, x265_preset_names[0], x265_tune_names[3]);  // "ultrafast", "zerolatency"
    x265_param& enc = *m_pParam;
    //* cpuFlags
    enc.frameNumThreads = 1;  // for ZeroLatency
    //* 视频选项
    enc.sourceWidth = static_cast<int>(param.width);
    enc.sourceHeight = static_cast<int>(param.height);
    enc.internalCsp = nCSP;
    enc.sourceBitDepth = nBitDepth;
    if (x265_max_bit_depth < enc.sourceBitDepth)
    {
        return -5;
    }
    // NAL HRD
    enc.vui.bEnableVideoSignalTypePresentFlag = 1;
    enc.vui.bEnableVideoFullRangeFlag = param.colorspace.range == NVIRange_Full ? 1 : 0;
    if ((param.colorspace.primary <= NVIPrimary_SMPTEST432 && param.colorspace.primary != NVIPrimary_Unspecified) ||
        (param.colorspace.transfer <= NVITransfer_ARIB_STD_B67 && param.colorspace.transfer != NVITransfer_Unspecified) ||
        (param.colorspace.matrix <= NVIMatrix_BT2100_ICTCP && param.colorspace.matrix != NVIMatrix_Unspecified))
    {
        enc.vui.bEnableColorDescriptionPresentFlag = 1;
        enc.vui.colorPrimaries = param.colorspace.primary;
        enc.vui.transferCharacteristics = param.colorspace.transfer;
        enc.vui.matrixCoeffs = param.colorspace.matrix;
        if (param.colorspace.transfer == NVITransfer_ARIB_STD_B67)
        {
            enc.preferredTransferCharacteristics = param.colorspace.transfer;
        }
    }
    //* 流参数
    enc.bRepeatHeaders = 1;
    enc.bAnnexB = 1;
    enc.bEnableAccessUnitDelimiters = 0;
    enc.bframes = 0;
    enc.maxSlices = 1;  // for ZeroLatency
    enc.keyframeMax = static_cast<int>(param.gop);
    enc.keyframeMin = enc.keyframeMax;
    enc.fpsNum = param.frame_rate_num;
    enc.fpsDenom = param.frame_rate_den;
    enc.bOpenGOP = 0;
    enc.bEnablePsnr = 0;
    //码率控制模式有ABR（平均码率）、CQP（恒定质量）、CRF（恒定质量因子）.
    //ABR模式下调整i_bitrate，CQP下调整i_qp_constant调整QP值，范围0~51，值越大图像越模糊，默认32.
    enc.rc.rateControlMode = X265_RC_CRF;
    if (param.quality > 0 && param.quality <= 51)
    {
        enc.rc.rfConstant = static_cast<float>(param.quality);
    }
    enc.rc.bitrate = static_cast<int>(param.avg_bitrate);
    enc.rc.vbvMaxBitrate = static_cast<int>(param.max_bitrate);
    if (param.vbv > 0)
    {
        enc.rc.vbvBufferSize = static_cast<int>(param.vbv);
    }
    else
    {
        enc.rc.vbvBufferSize = static_cast<int>(param.max_bitrate) / enc.fpsNum * enc.fpsDenom * 10;
    }
    enc.rc.aqMode = 0;
    enc.bDisableLookahead = 1;
    enc.maxCUSize = 64;
    if (enc.sourceBitDepth == 10)
    {
        m_pAPI->param_apply_profile(&enc, x265_profile_names[1]);  // "main10"
    }
    else
    {
        m_pAPI->param_apply_profile(&enc, x265_profile_names[0]);  // "main"
    }
    // 打开编码器
    m_pHandle = m_pAPI->encoder_open(&enc);
    if (m_pHandle)
    {
        // NAL buffer
        m_vecStreamBuffer.resize(1ull);
        for (auto& item : m_vecStreamBuffer)
        {
            item.reset(new uint8_t[kBufferSize]);
        }
        LOG_NOTICE("X265Encoder opened handle[{}], libx265 version " LIBX265_VERSION ".", (void*)m_pHandle);
        return 0;
    }
    return -256;
}

inline int32_t X265Encoder::Encoding(const NVIVideoImageFrame& in, NVIVideoEncode::OnPacket out, void* user)
{
    if (m_pHandle == nullptr)
    {
        return -1;
    }
    x265_picture picIn;
    x265_picture_init(m_pParam, &picIn);
    if (!PicturePalneCopy(in, picIn))
    {
        return -2;
    }
    picIn.pts = static_cast<int64_t>(in.info.tick.value);
    picIn.bitDepth = FormatBitDepth(static_cast<NVIPixelFormat>(in.buffer.format));
    picIn.sliceType = in.info.frame_kind == NVIFrameKind_Intra ? X265_TYPE_IDR : X265_TYPE_AUTO;
    NVIVideoEncodedPacket packet{};
    packet.info = in.info;
    packet.pixel_format = in.buffer.format;
    uint32_t uNal = -1;
    x265_nal* pNals = nullptr;
    x265_picture picOut{};
    int nEncode = m_pAPI->encoder_encode(m_pHandle, &pNals, &uNal, &picIn, &picOut);
    if (nEncode > 0 && uNal > 0u)
    {
        packet.info.frame_kind = X265_TYPE_IDR == picOut.sliceType || X265_TYPE_I == picOut.sliceType ? NVIFrameKind_Intra : NVIFrameKind_Delta;
        uint8_t* pData = m_vecStreamBuffer[0].get();
        size_t& szData = packet.buffer.size;
        szData = 0ull;
        for (int i = 0; i < uNal; ++i)
        {
            if (szData + pNals[i].sizeBytes > kBufferSize)
            {
                m_pAPI->encoder_intra_refresh(m_pHandle);
                LOG_ERROR("The x265 encoded nal size overflow");
                return -3;
            }
            else
            {
                memcpy(pData + szData, pNals[i].payload, pNals[i].sizeBytes);
                szData += static_cast<size_t>(pNals[i].sizeBytes);
            }
        }
        packet.buffer.bytes = pData;
        packet.slice_mode = 0;
        packet.slice_count = 1;
        packet.slice_offset = 0;
        packet.slice_number = 1;
        out(&packet, user);
    }
    return nEncode;
}

inline void X265Encoder::Release()
{
    if (m_pAPI)
    {
        if (m_pHandle)
        {
            m_pAPI->encoder_close(m_pHandle);
            m_pHandle = nullptr;
        }
        if (m_pParam)
        {
            m_pAPI->param_free(m_pParam);
            m_pParam = nullptr;
        }
        m_pAPI = nullptr;
    }
}

inline bool X265Encoder::PicturePalneCopy(const NVIVideoImageFrame& in, x265_picture& out)
{
    out.colorSpace = ToX265CSP(static_cast<NVIPixelFormat>(in.buffer.format));
    int planes = 0;
    switch (out.colorSpace)
    {
    case X265_CSP_I420: planes = 3; break;
    case X265_CSP_NV12: planes = 2; break;
    case X265_CSP_I422: planes = 3; break;
    default: return false;
    }
    for (int i = 0; i < planes; ++i)
    {
        out.planes[i] = const_cast<uint8_t*>(in.buffer.planes[i]);
        out.stride[i] = static_cast<int>(in.buffer.strides[i]);
    }
    return true;
}
