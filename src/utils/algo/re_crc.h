// re_crc.h - CRC-16/X-25 and CRC-32/IEEE for FLIRT signatures, ELF and gzip checks.
// Module: util (C11).
// Owns: table construction and the two reflected CRC variants the project needs.
// Depends: none. re_crc_init must run once at startup before any crc call.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define RE_CRC16_X25_INIT 0xffffu
#define RE_CRC16_X25_POLY 0x1021u
// X-25 is a reflected CRC, so the table is built with the bit reversed
// polynomial. Using 0x1021 directly in a reflected loop gives wrong digests.
#define RE_CRC16_X25_RPOLY 0x8408u
#define RE_CRC32_IEEE_INIT 0xffffffffu
#define RE_CRC32_IEEE_POLY 0xedb88320u

// Build the lookup tables. Called once from the entry point. Cheap, and keeps
// 512 lines of table literals out of the source.
void re_crc_init(void);

// Running CRC, pass the init value to start. Feed the result of a previous call
// back in to continue across chunks.
uint16_t re_crc16_x25(const void *data, size_t n, uint16_t crc);

// One shot X-25 including the final xor, which is the form FLIRT stores.
uint16_t re_crc16_x25_final(const void *data, size_t n);

uint32_t re_crc32_ieee(const void *data, size_t n, uint32_t crc);

static inline uint32_t re_crc32_ieee_final(const void *data, size_t n) {
    return re_crc32_ieee(data, n, RE_CRC32_IEEE_INIT) ^ 0xffffffffu;
}

#ifdef __cplusplus
}
#endif
