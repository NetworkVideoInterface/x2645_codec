#include "Codec.h"
#include "X264Encoder.hpp"

class X264EncoderDelegate
{
public:
    static int32_t Config(void* encoder, const NVIVideoCodecParam* param)
    {
        if (encoder == nullptr || param == nullptr)
        {
            return -1;
        }
        if (param->codec != NVICodec_AVC)
        {
            return -1;
        }
        X264Encoder* pEncoder = reinterpret_cast<X264Encoder*>(encoder);
        return pEncoder->Config(*param);
    }
    static int32_t Encoding(void* encoder, const NVIVideoImageFrame* in, NVIVideoEncode::OnPacket out, void* user)
    {
        if (encoder == nullptr)
        {
            return -1;
        }
        if (in == nullptr)
        {
            return 0;
        }
        X264Encoder* pEncoder = static_cast<X264Encoder*>(encoder);
        return pEncoder->Encoding(*in, out, user);
    }
    static int32_t Release(void* encoder)
    {
        if (encoder == nullptr)
        {
            return 0;
        }
        X264Encoder* pEncoder = static_cast<X264Encoder*>(encoder);
        delete pEncoder;
        return 0;
    }
};

#ifdef ENABLE_X265
#include "X265Encoder.hpp"

class X265EncoderDelegate
{
public:
    static int32_t Config(void* encoder, const NVIVideoCodecParam* param)
    {
        if (encoder == nullptr || param == nullptr)
        {
            return -1;
        }
        if (param->codec != NVICodec_HEVC)
        {
            return -1;
        }
        X265Encoder* pEncoder = reinterpret_cast<X265Encoder*>(encoder);
        return pEncoder->Config(*param);
    }
    static int32_t Encoding(void* encoder, const NVIVideoImageFrame* in, NVIVideoEncode::OnPacket out, void* user)
    {
        if (encoder == nullptr)
        {
            return -1;
        }
        if (in == nullptr)
        {
            return 0;
        }
        X265Encoder* pEncoder = static_cast<X265Encoder*>(encoder);
        return pEncoder->Encoding(*in, out, user);
    }
    static int32_t Release(void* encoder)
    {
        if (encoder == nullptr)
        {
            return 0;
        }
        X265Encoder* pEncoder = static_cast<X265Encoder*>(encoder);
        delete pEncoder;
        return 0;
    }
};
#endif

NVIVideoEncode VideoEncodeAlloc(uint32_t codec)
{
    NVIVideoEncode encode{};
    if (codec == NVICodec_AVC)
    {
        encode.encoder = new X264Encoder();
        encode.Config = &X264EncoderDelegate::Config;
        encode.Encoding = &X264EncoderDelegate::Encoding;
        encode.Release = &X264EncoderDelegate::Release;
    }
#ifdef ENABLE_X265
    if (codec == NVICodec_HEVC)
    {
        encode.encoder = new X265Encoder();
        encode.Config = &X265EncoderDelegate::Config;
        encode.Encoding = &X265EncoderDelegate::Encoding;
        encode.Release = &X265EncoderDelegate::Release;
    }
#endif
    return encode;
}

void SetLogging(void (*logging)(int level, const char* message, unsigned int length))
{
    SetLoggingFunc(logging);
}
