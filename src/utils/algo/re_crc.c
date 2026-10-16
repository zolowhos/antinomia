// re_crc.c - reflected CRC-16/X-25 and CRC-32/IEEE with lazily built tables.
// Module: util (C11).
// Owns: 256 entry tables per variant, generated once by re_crc_init.
// Depends: re_crc.h only. No I/O, no allocation, tables are file static.
#include "utils/algo/re_crc.h"

static uint16_t g_t16[256];
static uint32_t g_t32[256];
static int g_ready = 0;

static uint16_t crc16_byte(uint16_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (uint16_t)((crc & 1u) ? ((crc >> 1) ^ RE_CRC16_X25_RPOLY) : (crc >> 1));
    return crc;
}

static uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc & 1u) ? ((crc >> 1) ^ RE_CRC32_IEEE_POLY) : (crc >> 1);
    return crc;
}

void re_crc_init(void) {
    if (g_ready)
        return;
    for (unsigned i = 0; i < 256; i++) {
        g_t16[i] = crc16_byte(0, (uint8_t)i);
        g_t32[i] = crc32_byte(0, (uint8_t)i);
    }
    g_ready = 1;
}

uint16_t re_crc16_x25(const void *data, size_t n, uint16_t crc) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++)
        crc = (uint16_t)((crc >> 8) ^ g_t16[(crc ^ p[i]) & 0xffu]);
    return crc;
}

uint16_t re_crc16_x25_final(const void *data, size_t n) {
    return (uint16_t)(re_crc16_x25(data, n, RE_CRC16_X25_INIT) ^ 0xffffu);
}

uint32_t re_crc32_ieee(const void *data, size_t n, uint32_t crc) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++)
        crc = (crc >> 8) ^ g_t32[(crc ^ p[i]) & 0xffu];
    return crc;
}
