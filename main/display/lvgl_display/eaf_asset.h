#pragma once

#include <stddef.h>
#include <stdint.h>

// EAF-BOUNDS: validate our bounded palette/RLE profile before the vendor parser.
// Assets can arrive over OTA; a matching magic/checksum alone is not sufficient.
inline uint32_t EafRead32(const uint8_t* p) {
    return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline bool ValidateEafAsset(const uint8_t* data, size_t size) {
    if (!data || size < 16 || size > 8 * 1024 * 1024 || data[0] != 0x89 || data[1] != 'E' ||
        data[2] != 'A' || data[3] != 'F')
        return false;
    const uint32_t count = EafRead32(data + 4);
    const uint32_t length = EafRead32(data + 12);
    if (!count || count > 2000 || length != size - 16 || count * 8 > length)
        return false;
    uint32_t checksum = 0;
    for (size_t i = 16; i < size; ++i)
        checksum += data[i];
    if (checksum != EafRead32(data + 8))
        return false;
    const size_t base = 16 + count * 8;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t frame_size = EafRead32(data + 16 + i * 8);
        const uint32_t offset = EafRead32(data + 20 + i * 8);
        // ZZ + _S frame header + 8 block lengths + 256 BGRA palette entries.
        constexpr size_t kHeader = 2 + 18 + 32 + 1024;
        if (offset > size - base || frame_size > size - base - offset || frame_size < kHeader)
            return false;
        const auto* frame = data + base + offset;
        if (frame[0] != 'Z' || frame[1] != 'Z' || frame[2] != '_' || frame[3] != 'S' ||
            frame[11] != 8 || frame[12] != 128 || frame[13] || frame[14] != 128 || frame[15] ||
            frame[16] != 8 || frame[17] || frame[18] != 16 || frame[19])
            return false;
        size_t pos = kHeader;
        for (int block = 0; block < 8; ++block) {
            const uint32_t bytes = EafRead32(frame + 20 + block * 4);
            if (bytes < 3 || bytes > frame_size - pos || !(bytes & 1) || frame[pos] != 0)
                return false;
            uint32_t pixels = 0;
            for (size_t p = pos + 1; p < pos + bytes; p += 2) {
                if (!frame[p])
                    return false;
                pixels += frame[p];
                if (pixels > 128 * 16)
                    return false;
            }
            if (pixels != 128 * 16)
                return false;
            pos += bytes;
        }
    }
    return true;
}
