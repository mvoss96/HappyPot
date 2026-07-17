/** @file
 * Minimal, dependency-free 8-bit grayscale BMP encoder for the host preview.
 *
 * Standalone by design (like png_writer.h): the sim emits both PNG and BMP for
 * every screen. PNG is what the Read tool can currently display; BMP is here for
 * external viewers / in case BMP display is supported later. Drop this file and
 * the write_gray_bmp() call to stop emitting BMP.
 */
#ifndef BMP_WRITER_H
#define BMP_WRITER_H

#include <cstdint>
#include <cstdio>

/** Write a 16-bit little-endian value.
 *
 * @param f open output stream
 * @param v the value
 */
static void bmpw_put16(FILE *f, uint16_t v)
{
	uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
	fwrite(b, 1, 2, f);
}

/** Write a 32-bit little-endian value.
 *
 * @param f open output stream
 * @param v the value
 */
static void bmpw_put32(FILE *f, uint32_t v)
{
	uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
	fwrite(b, 1, 4, f);
}

/** Write a grayscale bitmap as an 8-bit BMP.
 * 256-entry gray palette; rows are stored bottom-up and padded to 4 bytes.
 *
 * @param path output file, overwritten if it exists
 * @param gray w*h bytes, row-major top-down, 0=black..255=white
 * @param w    width in pixels
 * @param h    height in pixels
 * @retval 0  the file was written
 * @retval -1 the file could not be opened
 */
static int write_gray_bmp(const char *path, const uint8_t *gray, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;

	const int stride = (w + 3) & ~3;		   // rows padded to 4 bytes
	const uint32_t pixOff = 14 + 40 + 256 * 4; // headers + palette
	const uint32_t imgSize = (uint32_t)stride * h;

	// BITMAPFILEHEADER
	fputc('B', f);
	fputc('M', f);
	bmpw_put32(f, pixOff + imgSize); // file size
	bmpw_put32(f, 0);				 // reserved
	bmpw_put32(f, pixOff);			 // offset to pixel data

	// BITMAPINFOHEADER
	bmpw_put32(f, 40); // header size
	bmpw_put32(f, (uint32_t)w);
	bmpw_put32(f, (uint32_t)h); // positive -> bottom-up
	bmpw_put16(f, 1);			// planes
	bmpw_put16(f, 8);			// bits per pixel
	bmpw_put32(f, 0);			// compression: none
	bmpw_put32(f, imgSize);
	bmpw_put32(f, 2835); // ~72 dpi x
	bmpw_put32(f, 2835); // ~72 dpi y
	bmpw_put32(f, 256);	 // palette size
	bmpw_put32(f, 0);	 // important colours

	// Grayscale palette: entry i = (B,G,R,0) = (i,i,i,0).
	for (int i = 0; i < 256; i++)
	{
		uint8_t e[4] = {(uint8_t)i, (uint8_t)i, (uint8_t)i, 0};
		fwrite(e, 1, 4, f);
	}

	// Pixel rows, bottom-up, each padded to `stride`.
	static const uint8_t pad[4] = {0, 0, 0, 0};
	for (int y = h - 1; y >= 0; y--)
	{
		fwrite(gray + (size_t)y * w, 1, w, f);
		if (stride > w)
			fwrite(pad, 1, stride - w, f);
	}

	fclose(f);
	return 0;
}

#endif // BMP_WRITER_H
