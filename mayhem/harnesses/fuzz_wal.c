/* Wall texture loaders: LoadWal / LoadM8 / LoadM32 and their Get*Info
 * (src/client/refresh/files/wal.c). The load_image callback reads exactly the
 * bytes of pic that the renderers' R_LoadPic implementations read, and no
 * fewer, so an out-of-range pic/data_size from a loader is reported:
 *   - GL1/GL3 (gl1_image.c / gl3_image.c R_LoadPic): ignore data_size and
 *     upload width*height pixels of bits/8 bytes each (R_Upload8/R_Upload32).
 *   - soft (sw_image.c R_LoadPic): bits==32 converts data_size pixels of
 *     4 bytes (R_Convert32To8bit(pic, pic8, data_size)); bits==8 memcpy()s
 *     min(data_size, R_GetImageMipsSize(width*height)) bytes (R_LoadPic8).
 * Each loader's size check guarantees both amounts fit the file; a read past
 * pic here is the loader handing over a bad pic/data_size (the frame below
 * this one names the loader line). */
#include "../../src/client/refresh/ref_shared.h"
#include "ref_stubs.h"

#define SW_NUM_MIPS 4 /* soft/header/local.h NUM_MIPS */

static size_t
sw_image_mips_size(size_t mip1_size) /* soft R_GetImageMipsSize */
{
	size_t full_size = 0;
	for (int i = 0; i < SW_NUM_MIPS; i++)
	{
		full_size += mip1_size >> (i * 2);
	}
	return full_size;
}

static struct image_s *
fuzz_load_image(const char *name, byte *pic, int width, int realwidth,
	int height, int realheight, size_t data_size, imagetype_t type, int bits)
{
	volatile unsigned sum = 0;
	size_t pixels, gl_bytes, sw_bytes, n;
	(void)name; (void)realwidth; (void)realheight; (void)type;

	/* both renderers return early on these (R_LoadPic / R_LoadPic8) */
	if (!pic || data_size == 0 || width <= 0 || height <= 0)
	{
		return NULL;
	}
	pixels = (size_t)width * (size_t)height;
	gl_bytes = pixels * (size_t)(bits / 8);
	if (bits == 32)
	{
		sw_bytes = data_size * 4;
	}
	else
	{
		size_t full = sw_image_mips_size(pixels);
		sw_bytes = data_size < full ? data_size : full;
	}
	n = gl_bytes > sw_bytes ? gl_bytes : sw_bytes;
	for (size_t i = 0; i < n; i++)
	{
		sum += pic[i];
	}
	return NULL;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	int w, h;

	stubs_init();
	stubs_set_input(data, size);
	LoadWal("textures/fuzz", it_wall, fuzz_load_image);
	LoadM8("textures/fuzz", it_wall, fuzz_load_image);
	LoadM32("textures/fuzz", it_wall, fuzz_load_image);
	GetWalInfo("textures/fuzz", &w, &h);
	GetM8Info("textures/fuzz", &w, &h);
	GetM32Info("textures/fuzz", &w, &h);
	return 0;
}
