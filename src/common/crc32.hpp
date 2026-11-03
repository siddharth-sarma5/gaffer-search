#pragma once

#include <cstdint>
#include <span>

namespace gaffer {

// CRC-32 (IEEE 802.3, the zlib/PNG/gzip variant: reflected polynomial
// 0xEDB88320, initial value and final XOR 0xFFFFFFFF). Check value:
// crc32("123456789") == 0xCBF43926.
//
// Used as the index-file checksum. It catches accidental corruption (bit
// flips, truncation, a partial copy) cheaply. It is not a cryptographic hash
// and does not protect against deliberate tampering.
//
// `previous` lets a long buffer be checksummed in pieces:
//   crc32(b, crc32(a)) == crc32(a ++ b)
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data,
                                  std::uint32_t previous = 0) noexcept;

}  // namespace gaffer
