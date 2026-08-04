#include "png_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Host-only minimal PNG writer: 8-bit RGB, zlib stream made of stored
 * (uncompressed) deflate blocks. Enough for review images; not part of the
 * portable renderer.
 */

static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
        }
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!crc_ready) {
        crc_init();
    }
    uint32_t c = crc;
    for (size_t i = 0; i < len; ++i) {
        c = crc_table[(c ^ buf[i]) & 0xFFU] ^ (c >> 8);
    }
    return c;
}

static void put_be32(FILE *f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8),
        (uint8_t)v,
    };
    fwrite(b, 1, 4, f);
}

static void write_chunk(FILE *f, const char *type, const uint8_t *data,
                        size_t len) {
    put_be32(f, (uint32_t)len);
    fwrite(type, 1, 4, f);
    if (len) {
        fwrite(data, 1, len, f);
    }
    uint32_t crc = crc_update(0xFFFFFFFFU, (const uint8_t *)type, 4);
    crc = crc_update(crc, data, len);
    put_be32(f, crc ^ 0xFFFFFFFFU);
}

int png_write_rgb(const char *path, const uint8_t *rgb, int width,
                  int height) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(width >> 24);
    ihdr[1] = (uint8_t)(width >> 16);
    ihdr[2] = (uint8_t)(width >> 8);
    ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24);
    ihdr[5] = (uint8_t)(height >> 16);
    ihdr[6] = (uint8_t)(height >> 8);
    ihdr[7] = (uint8_t)height;
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* colour type RGB */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    /* Raw scanlines with filter byte 0. */
    size_t stride = (size_t)width * 3 + 1;
    size_t raw_len = stride * (size_t)height;
    uint8_t *raw = malloc(raw_len);
    if (!raw) {
        fclose(f);
        return -1;
    }
    for (int y = 0; y < height; ++y) {
        raw[(size_t)y * stride] = 0;
        memcpy(raw + (size_t)y * stride + 1, rgb + (size_t)y * width * 3,
               (size_t)width * 3);
    }

    /* zlib: header + stored deflate blocks + adler32. */
    size_t max_block = 65535;
    size_t nblocks = (raw_len + max_block - 1) / max_block;
    size_t idat_len = 2 + raw_len + nblocks * 5 + 4;
    uint8_t *idat = malloc(idat_len);
    if (!idat) {
        free(raw);
        fclose(f);
        return -1;
    }
    size_t o = 0;
    idat[o++] = 0x78;
    idat[o++] = 0x01;
    size_t remaining = raw_len;
    const uint8_t *src = raw;
    while (remaining > 0) {
        size_t chunk = remaining > max_block ? max_block : remaining;
        idat[o++] = (remaining == chunk) ? 1 : 0;
        idat[o++] = (uint8_t)(chunk & 0xFFU);
        idat[o++] = (uint8_t)(chunk >> 8);
        idat[o++] = (uint8_t)(~chunk & 0xFFU);
        idat[o++] = (uint8_t)((~chunk >> 8) & 0xFFU);
        memcpy(idat + o, src, chunk);
        o += chunk;
        src += chunk;
        remaining -= chunk;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; ++i) {
        a = (a + raw[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    uint32_t adler = (b << 16) | a;
    idat[o++] = (uint8_t)(adler >> 24);
    idat[o++] = (uint8_t)(adler >> 16);
    idat[o++] = (uint8_t)(adler >> 8);
    idat[o++] = (uint8_t)adler;

    write_chunk(f, "IDAT", idat, o);
    write_chunk(f, "IEND", 0, 0);
    free(idat);
    free(raw);
    fclose(f);
    return 0;
}
