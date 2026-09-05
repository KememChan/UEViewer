#ifndef __BC7_PREP_DECODER_H__
#define __BC7_PREP_DECODER_H__

#if WUTHERING_WAVES

#include "Core.h"

// Decode a BC7Prep payload to raw BC7 blocks (16 bytes per block).
// Returns true on success, false if the payload is corrupt or invalid.
bool DecodeBC7Prep(const byte* payload, int payloadSize, const int32 modes[10], uint32 oodleFlags, byte* output, int outputSize);

#endif // WUTHERING_WAVES

#endif // __BC7_PREP_DECODER_H__
