#pragma once

#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <NVI/Codec.h>
#include <x264.h>
#include "adaption/Logging.h"

class X264Encoder final
{
private:
    static void NaluProcess(x264_t* h, x264_nal_t* nal, void* opaque);
    static void Logging(void*, int level, const char* fmt, va_list vars);

public:
    X264Encoder();
    ~X264Encoder();

public:
    int32_t Config(const NVIVideoCodecParam& param);
    int32_t Encoding(const NVIVideoImageFrame& in, NVIVideoEncode::OnPacket out, void* user);
    void Release();

private:
    bool PicturePalneCopy(const NVIVideoImageFrame& in, x264_picture_t& out);

private:
    x264_t* m_pHandle;
    x264_picture_t m_picture;
    std::vector<std::unique_ptr<uint8_t[]>> m_vecStreamBuffer;
    uint16_t m_uSliceMode;
    uint16_t m_uSliceCount;
    uint32_t m_uMBsPerSlice;

    const size_t kBufferSize = 1 * 1024 * 1024;
    const uint32_t kMaxFrameSize = 4096 * 2048;
    const uint32_t kMaxFrameRate = 60;
    const uint32_t kSliceLines = 272;
};

//////////////////////////////////////////////////////////////////////////
struct EncodeContext
{
    const NVIVideoEncodedPacket& packet;
    const std::vector<std::unique_ptr<uint8_t[]>>& buffers;
    size_t szExtraOffset = 0ull;  // sps pps data
    NVIVideoEncode::OnPacket pOutput = nullptr;
    void* pUser = nullptr;
    uint32_t uMBsPerSlice = 0u;
    uint32_t uSliceNumber = 0u;
    EncodeContext(NVIVideoEncodedPacket& pkt, const std::vector<std::unique_ptr<uint8_t[]>>& buf)
        : packet(pkt)
        , buffers(buf)
    {
    }
};

int ToX264CSP(NVIPixelFormat format)
{
    switch (format)
    {
    case NVIPixel_I420: return X264_CSP_I420;
    case NVIPixel_NV12: return X264_CSP_NV12;
    case NVIPixel_NV21: return X264_CSP_NV21;
    case NVIPixel_422P: return X264_CSP_I422;
    case NVIPixel_V210: return X264_CSP_V210;
    default: return 0;
    }
}

inline void X264Encoder::NaluProcess(x264_t* h, x264_nal_t* nal, void* opaque)
{
    if (opaque && nal)
    {
        EncodeContext* pContext = reinterpret_cast<EncodeContext*>(opaque);
        if (pContext->uMBsPerSlice == 0u && nal->i_payload > 0)
        {
            return;
        }
        if (nal->i_type == NAL_SEI || nal->i_type == NAL_SPS || nal->i_type == NAL_PPS)
        {
            /*
             * x264会在`x264_encoder_encode`线程先回调输出sps pps sei，
             * 这里先缓存下来等编码出第一个slice后再输出。
             */
            if (!pContext->buffers.empty())
            {
                x264_nal_encode(h, pContext->buffers[0].get() + pContext->szExtraOffset, nal);
                pContext->szExtraOffset += static_cast<size_t>(nal->i_payload);
            }
        }
        else
        {
            const size_t szOffset = static_cast<size_t>(std::ceil(nal->i_first_mb / static_cast<float>(pContext->uMBsPerSlice)));
            if (szOffset < pContext->buffers.size())
            {
                NVIVideoEncodedPacket packet = pContext->packet;
                if (szOffset == 0 && pContext->szExtraOffset > 0ull)
                {
                    x264_nal_encode(h, pContext->buffers[szOffset].get() + pContext->szExtraOffset, nal);
                    packet.buffer.bytes = pContext->buffers[szOffset].get();
                    packet.buffer.size = pContext->szExtraOffset + nal->i_payload;
                }
                else
                {
                    x264_nal_encode(h, pContext->buffers[szOffset].get(), nal);
                    packet.buffer.bytes = nal->p_payload;
                    packet.buffer.size = nal->i_payload;
                }
                packet.info.frame_kind = nal->i_type == NAL_SLICE_IDR ? NVIFrameKind_Intra : NVIFrameKind_Delta;
                packet.slice_offset = static_cast<uint16_t>(szOffset);
                packet.slice_number = 1;
                pContext->pOutput(&packet, pContext->pUser);
                ++pContext->uSliceNumber;
            }
        }
    }
}

inline void X264Encoder::Logging(void*, int level, const char* fmt, va_list vars)
{
    if (level > 0)
    {
        char buf[2048];
        va_list args;
        va_copy(args, vars);
        int length = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (length > 0 && length <= 2048)
        {
            LoggingMessage(static_cast<LogLevel>(level + 4), "#X2645/x264 {}", fmt::string_view(buf, static_cast<size_t>(length)));
        }
    }
}

inline X264Encoder::X264Encoder()
    : m_pHandle(nullptr)
    , m_picture({})
    , m_uSliceMode(0)
    , m_uSliceCount(0)
    , m_uMBsPerSlice(0u)
{
}

inline X264Encoder::~X264Encoder()
{
    Release();
}

inline int32_t X264Encoder::Config(const NVIVideoCodecParam& param)
{
    if (m_pHandle)
    {
        return -1;
    }
    if (param.accel && param.accel->type > NVIAccel_Auto)
    {
        return -1;
    }
    if ((param.width * param.height) > kMaxFrameSize)
    {
        return -2;
    }
    int nCSP = ToX264CSP(static_cast<NVIPixelFormat>(param.format));
    if (nCSP == 0)
    {
        return -3;
    }
    int nThreads = (param.height + kSliceLines - 1) / kSliceLines;
    if (param.slice_mode == 0)
    {
        m_uSliceMode = 0u;
    }
    else if (param.slice_mode == NVISliceMode_MultiSlice)
    {
        m_uSliceMode = param.slice_mode | NVISliceMode_InOrder;
    }
    else
    {
        return -4;
    }
    m_uSliceCount = static_cast<uint16_t>(nThreads);
    m_uMBsPerSlice = ((param.width + 15) >> 4) * (kSliceLines >> 4);
    x264_param_t X264Param{};
    X264Param.i_log_level = X264_LOG_NONE;
    x264_param_default_preset(&X264Param, x264_preset_names[0], x264_tune_names[7]);
    //* cpuFlags
    X264Param.i_threads = nThreads /*X264_THREADS_AUTO*/ /*X264_SYNC_LOOKAHEAD_AUTO*/;
    //X264Param.b_sliced_threads = 1; // auto set by x264_param_default_preset()
    //* 视频选项
    X264Param.i_width = static_cast<int>(param.width);
    X264Param.i_height = static_cast<int>(param.height);
    X264Param.i_csp = nCSP;
    //X264Param.i_frame_total = 0;

    // NAL HRD
    X264Param.vui.i_colorprim = param.colorspace.primary;
    X264Param.vui.i_transfer = param.colorspace.transfer;
    X264Param.vui.i_colmatrix = param.colorspace.matrix;
    X264Param.vui.b_fullrange = param.colorspace.range == NVIRange_Full ? 1 : 0;

    //* 流参数
    X264Param.b_repeat_headers = 1;
    X264Param.b_annexb = 1;
    X264Param.i_bframe = 0;
    X264Param.i_slice_count = m_uSliceCount;
    X264Param.i_slice_count_max = m_uSliceCount;

    //* 速率控制参数
    X264Param.rc.i_bitrate = static_cast<int>(param.avg_bitrate);
    X264Param.rc.i_vbv_max_bitrate = static_cast<int>(param.max_bitrate);
    X264Param.rc.i_vbv_buffer_size = static_cast<int>(param.vbv);

    //码率控制模式有ABR（平均码率）、CQP（恒定质量）、CRF（恒定质量因子）.
    //ABR模式下调整i_bitrate，CQP下调整i_qp_constant调整QP值，范围0~51，值越大图像越模糊，默认23.
    //太细致了人眼也分辨不出来，为了增加编码速度降低数据量还是设大些好,CRF下调整f_rf_constant和f_rf_constant_max影响编码速度和图像质量（数据量）；
    //https://zhuanlan.zhihu.com/p/393657940
    //X264Param.rc.i_rc_method = X264_RC_CQP;
    //X264Param.rc.i_qp_constant = 28;
    X264Param.rc.i_rc_method = X264_RC_CRF;
    if (param.quality > 0 && param.quality <= 51)
    {
        X264Param.rc.f_rf_constant = static_cast<float>(param.quality);
    }
    else
    {
        X264Param.rc.f_rf_constant = 23.0f;
    }

    //* muxing parameters
    X264Param.i_fps_num = param.frame_rate_num;
    X264Param.i_fps_den = param.frame_rate_den;
    X264Param.i_timebase_num = 1u;
    X264Param.i_timebase_den = 90000u;
    X264Param.i_keyint_max = static_cast<int>(param.gop);
    X264Param.i_keyint_min = X264Param.i_keyint_max;
    X264Param.b_open_gop = 0;

    //关闭自适应I帧决策。
    X264Param.i_scenecut_threshold = 0;

    //设置亚像素估计的复杂度。值越高越好。级别1-5简单控制亚像素的细化力度。级别6给模式决策开启RDO（码率失真优化模式），
    //级别8给运动矢量和帧内预测模式开启RDO。开启RDO会显著增加耗时。
    //使用小于2的值会开启一个快速的、低质量的预测模式，效果如同设置了一个很小的 –scenecut值。不推荐这样设置。
    //X264Param.analyse.i_subpel_refine = 1;

    //为mb-tree ratecontrol（Macroblock Tree Ratecontrol）和vbv-lookahead设置可用的帧的数量。最大可设置为250。
    //对于mb-tree而言，调大这个值会得到更准确地结果，但也会更慢。
    //mb-tree能使用的最大值是–rc-lookahead和–keyint中较小的那一个。
    X264Param.rc.i_lookahead = 0;

    //i_luma_deadzone[0]和i_luma_deadzone[1]分别对应inter和intra，取值范围1~32
    //这个参数的调整可以对数据量有很大影响，值越大数据量相应越少，占用带宽越低.
    X264Param.analyse.i_luma_deadzone[0] = 32;
    X264Param.analyse.i_luma_deadzone[1] = 32;

    //自适应量化器模式。不使用自适应量化的话，x264趋向于使用较少的bit在缺乏细节的场景里。自适应量化可以在整个视频的宏块里更好地分配比特。它有以下选项：
    //0-完全关闭自适应量化器;1-允许自适应量化器在所有视频帧内部分配比特;2-根据前一帧强度决策的自变量化器（实验性的）。默认值=1
    X264Param.rc.i_aq_mode = 0;

    //为’direct’类型的运动矢量设定预测模式。有两种可选的模式：spatial（空间预测）和temporal（时间预测）。默认：’spatial’
    //可以设置为’none’关闭预测，也可以设置为’auto’让x264去选择它认为更好的模式，x264会在编码结束时告诉你它的选择。
    X264Param.analyse.i_direct_mv_pred = X264_DIRECT_PRED_NONE;

    //开启明确的权重预测以增进P帧压缩。越高级的模式越耗时，有以下模式：
    //0 : 关闭; 1 : 静态补偿（永远为-1）; 2 : 智能统计静态帧，特别为增进淡入淡出效果的压缩率而设计
    //X264Param.analyse.i_weighted_pred = X264_WEIGHTP_NONE;

    //设置全局的运动预测方法，有以下5种选择：dia（四边形搜索）, hex（六边形搜索）, umh（不均匀的多六边形搜索）
    //esa（全局搜索），tesa（变换全局搜索），默认：’hex’
    X264Param.analyse.i_me_method = X264_ME_DIA;

    //merange控制运动搜索的最大像素范围。对于hex和dia，范围被控制在4-16像素，默认就是16。
    //对于umh和esa，可以超过默认的 16像素进行大范围的运行搜索，这对高分辨率视频和快速运动视频而言很有用。
    //注意，对于umh、esa、tesa，增大merange会显著地增加编码耗时。默认：16
    X264Param.analyse.i_me_range = 4;

    //关闭或启动为了心理视觉而降低psnr或ssim的优化。此选项同时也会关闭一些不能通过x264命令行设置的内部的心理视觉优化方法。
    //X264Param.analyse.b_psy = 0;

    //Mixed refs（混合参照）会以8×8的切块为参照取代以整个宏块为参照。会增进多帧参照的帧的质量，会有一些时间耗用.
    //X264Param.analyse.b_mixed_references = 0;

    //通常运动估计都会同时考虑亮度和色度因素。开启此选项将会忽略色度因素换取一些速度的提升。
    X264Param.analyse.b_chroma_me = 1;

    //使用网格编码量化以增进编码效率：0-关闭, 1-仅在宏块最终编码时启用, 2-所有模式下均启用.
    //选项1提供了速度和效率间较好的均衡，选项2大幅降低速度.
    //注意：需要开启 –cabac选项生效.
    //X264Param.b_cabac = 1;
    //X264Param.analyse.i_trellis = 1;

    //停用弹性内容的二进制算数编码（CABAC：Context Adaptive Binary Arithmetic Coder）资料流压缩，
    //切换回效率较低的弹性内容的可变长度编码（CAVLC：Context Adaptive Variable Length Coder）系统。
    //大幅降低压缩效率（通常10~20%）和解码的硬件需求。
    //X264Param.b_cabac = 1;
    //X264Param.i_cabac_init_idc = 0;

    //关闭P帧的早期跳过决策。大量的时耗换回非常小的质量提升。
    X264Param.analyse.b_fast_pskip = 0;

    //DCT抽样会丢弃看上去“多余”的DCT块。会增加编码效率，通常质量损失可以忽略。
    X264Param.analyse.b_dct_decimate = 1;

    //完全关闭内置去块滤镜，不推荐使用。
    //调节H.264标准中的内置去块滤镜。这是个性价比很高的选则, 关闭.
    //X264Param.b_deblocking_filter = 1;
    //X264Param.i_deblocking_filter_alphac0 = 0;
    //X264Param.i_deblocking_filter_beta = 0;

    //去掉信噪比的计算，因为在解码端也可用到.
    X264Param.analyse.b_psnr = 0;  //是否使用信噪比.

    if (m_uSliceMode != 0 && m_uSliceCount > 1)
    {
        X264Param.nalu_process = &X264Encoder::NaluProcess;
        m_vecStreamBuffer.resize(m_uSliceCount);
    }
    else
    {
        m_vecStreamBuffer.resize(1ull);
    }
    for (auto& item : m_vecStreamBuffer)
    {
        item.reset(new uint8_t[kBufferSize]);
    }

    //* 设置Profile.使用main profile
    if (param.profile == 0 || param.profile >= 77u)
    {
        x264_param_apply_profile(&X264Param, x264_profile_names[1]);  // "main"
    }
    else
    {
        x264_param_apply_profile(&X264Param, x264_profile_names[0]);  // "baseline"
    }

    // log callback
    X264Param.pf_log = X264Encoder::Logging;

    //* 打开编码器
    m_pHandle = x264_encoder_open(&X264Param);
    if (m_pHandle)
    {
        //创建X264图像容器
        x264_picture_init(&m_picture);
        LOG_NOTICE("X264Encoder opened handle[{}], libx264 version " LIBX264_VERSION ".", (void*)m_pHandle);
        return 0;
    }
    return -1;
}

inline int32_t X264Encoder::Encoding(const NVIVideoImageFrame& in, NVIVideoEncode::OnPacket out, void* user)
{
    if (m_pHandle == nullptr)
    {
        return -1;
    }
    x264_picture_init(&m_picture);
    if (!PicturePalneCopy(in, m_picture))
    {
        return -2;
    }
    m_picture.i_type = in.info.frame_kind == NVIFrameKind_Intra ? X264_TYPE_IDR : X264_TYPE_AUTO;
    NVIVideoEncodedPacket packet{};
    packet.info = in.info;
    packet.slice_mode = m_uSliceMode;
    packet.slice_count = m_uSliceCount;
    packet.pixel_format = in.buffer.format;
    EncodeContext context(packet, m_vecStreamBuffer);
    context.pOutput = out;
    context.pUser = user;
    context.uMBsPerSlice = m_uMBsPerSlice;
    m_picture.opaque = &context;
    int iNal = -1;
    x264_nal_t* pNals = nullptr;
    x264_picture_t picOut{};
    int nEncode = x264_encoder_encode(m_pHandle, &pNals, &iNal, &m_picture, &picOut);
    if (nEncode > 0 && context.uSliceNumber == 0u && out)
    {
        packet.info.frame_kind = X264_TYPE_IDR == picOut.i_type || X264_TYPE_I == picOut.i_type ? NVIFrameKind_Intra : NVIFrameKind_Delta;
        uint8_t* pData = m_vecStreamBuffer[0].get();
        size_t& szData = packet.buffer.size;
        szData = 0ull;
        for (int i = 0; i < iNal; ++i)
        {
            memcpy(pData + szData, pNals[i].p_payload, pNals[i].i_payload);
            szData += static_cast<size_t>(pNals[i].i_payload);
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

inline void X264Encoder::Release()
{
    if (m_pHandle)
    {
        x264_encoder_close(m_pHandle);
        m_pHandle = nullptr;
    }
}

inline bool X264Encoder::PicturePalneCopy(const NVIVideoImageFrame& in, x264_picture_t& out)
{
    out.img.i_csp = ToX264CSP(static_cast<NVIPixelFormat>(in.buffer.format));
    switch (out.img.i_csp)
    {
    case X264_CSP_I420: out.img.i_plane = 3; break;
    case X264_CSP_NV12: out.img.i_plane = 2; break;
    case X264_CSP_I422: out.img.i_plane = 3; break;
    default: return false;
    }
    for (int i = 0; i < out.img.i_plane; ++i)
    {
        out.img.plane[i] = const_cast<uint8_t*>(in.buffer.planes[i]);
        out.img.i_stride[i] = static_cast<int>(in.buffer.strides[i]);
    }
    return true;
}
