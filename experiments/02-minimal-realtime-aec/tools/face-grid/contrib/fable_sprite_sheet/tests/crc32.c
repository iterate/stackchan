#include "crc32.h"

static uint32_t table[256];
static int table_ready;

static void build_table(void)
{
    for (uint32_t index = 0; index < 256u; ++index) {
        uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u)
                ? 0xEDB88320u ^ (value >> 1u)
                : value >> 1u;
        }
        table[index] = value;
    }
    table_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t length)
{
    if (!table_ready) {
        build_table();
    }
    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;
    for (size_t index = 0; index < length; ++index) {
        crc = table[(crc ^ bytes[index]) & 0xFFu] ^ (crc >> 8u);
    }
    return ~crc;
}
