#pragma once

#include <cstddef>
#include <cstdint>

namespace evb {

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
inline uint16_t crc16Ccitt(const uint8_t *data, std::size_t len, uint16_t crc = 0xFFFF)
{
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

} // namespace evb
