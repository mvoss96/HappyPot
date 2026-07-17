/** @file
 * Minimal, dependency-free 8-bit grayscale PNG encoder for the host preview.
 *
 * Kept as a standalone unit on purpose: the only reason the sim writes PNG (and
 * not the far simpler BMP) is that the Read tool can display PNG but not BMP. If
 * that ever changes, delete this file and have sim.cpp emit a BMP instead —
 * nothing else depends on it besides the single write_gray_png() call.
 *
 * No zlib: the IDAT is a zlib stream built from DEFLATE *stored* (uncompressed)
 * blocks, so we only need CRC-32 (chunk checksums) and Adler-32 (zlib checksum).
 */
#ifndef PNG_WRITER_H
#define PNG_WRITER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

/** CRC-32 over the concatenation a || b.
 * PNG chunks CRC their type followed by their data, hence the two parts.
 *
 * @param a  first buffer (the chunk type)
 * @param na its length
 * @param b  second buffer (the chunk data), may be NULL when nb is 0
 * @param nb its length
 * @return the CRC-32 of a followed by b
 */
static uint32_t pngw_crc32(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
	uint32_t c = 0xFFFFFFFFu;
	auto fold = [&](const uint8_t *p, size_t n)
	{
		for (size_t i = 0; i < n; i++)
		{
			c ^= p[i];
			for (int k = 0; k < 8; k++)
				c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int)(c & 1)));
		}
	};
	fold(a, na);
	fold(b, nb);
	return ~c;
}

/** Write a 32-bit big-endian value, as every PNG length and CRC field is.
 *
 * @param f open output stream
 * @param v the value
 */
static void pngw_put32(FILE *f, uint32_t v)
{
	uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16),
					(uint8_t)(v >> 8), (uint8_t)v};
	fwrite(b, 1, 4, f);
}

/** Write one PNG chunk: length, type, data, CRC.
 *
 * @param f    open output stream
 * @param type four-character chunk type, e.g. "IHDR"
 * @param data payload, may be NULL when len is 0
 * @param len  payload length in bytes
 */
static void pngw_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
	pngw_put32(f, (uint32_t)len);
	fwrite(type, 1, 4, f);
	if (len)
		fwrite(data, 1, len, f);
	pngw_put32(f, pngw_crc32((const uint8_t *)type, 4, data, len));
}

/** Write a grayscale bitmap as an 8-bit PNG.
 *
 * @param path output file, overwritten if it exists
 * @param gray w*h bytes, row-major, 0=black..255=white
 * @param w    width in pixels
 * @param h    height in pixels
 * @retval 0  the file was written
 * @retval -1 the file could not be opened
 */
static int write_gray_png(const char *path, const uint8_t *gray, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;

	static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	fwrite(sig, 1, 8, f);

	uint8_t ihdr[13] = {
		(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
		(uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
		8, 0, 0, 0, 0 // 8-bit grayscale
	};
	pngw_chunk(f, "IHDR", ihdr, sizeof(ihdr));

	// raw = per row: filter byte 0 + w pixels
	size_t raw_len = (size_t)h * (1 + (size_t)w);
	uint8_t *raw = (uint8_t *)malloc(raw_len);
	for (int y = 0, o = 0; y < h; y++)
	{
		raw[o++] = 0;
		memcpy(raw + o, gray + (size_t)y * w, w);
		o += w;
	}

	// zlib stream: header + DEFLATE stored blocks + Adler-32
	size_t nblk = (raw_len + 65534) / 65535;
	if (!nblk)
		nblk = 1;
	uint8_t *idat = (uint8_t *)malloc(2 + nblk * 5 + raw_len + 4);
	size_t p = 0;
	idat[p++] = 0x78;
	idat[p++] = 0x01;
	for (size_t off = 0, left = raw_len; left;)
	{
		size_t n = left > 65535 ? 65535 : left;
		idat[p++] = (n == left) ? 1 : 0;
		idat[p++] = (uint8_t)n;
		idat[p++] = (uint8_t)(n >> 8);
		idat[p++] = (uint8_t)~n;
		idat[p++] = (uint8_t)(~n >> 8);
		memcpy(idat + p, raw + off, n);
		p += n;
		off += n;
		left -= n;
	}
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw_len; i++)
	{
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	uint32_t adler = (b << 16) | a;
	idat[p++] = (uint8_t)(adler >> 24);
	idat[p++] = (uint8_t)(adler >> 16);
	idat[p++] = (uint8_t)(adler >> 8);
	idat[p++] = (uint8_t)adler;

	pngw_chunk(f, "IDAT", idat, p);
	pngw_chunk(f, "IEND", nullptr, 0);
	free(idat);
	free(raw);
	fclose(f);
	return 0;
}

#endif // PNG_WRITER_H
