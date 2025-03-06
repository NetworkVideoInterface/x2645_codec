#pragma once

#include <NVI/Codec.h>

NVI_API NVIVideoEncode VideoEncodeAlloc(uint32_t codec);

NVI_API void SetLogging(void (*logging)(int level, const char* message, unsigned int length));
