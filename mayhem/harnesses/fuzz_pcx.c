/* PCX image loader: LoadPCX -> PCX_Decode / PCX_RLE_Decode (src/client/refresh/files/pcx.c). */
#include <stdlib.h>
#include "../../src/client/refresh/ref_shared.h"
#include "ref_stubs.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	byte *pic = NULL, *pal = NULL;
	int w = 0, h = 0, bpp = 0;

	stubs_init();
	stubs_set_input(data, size);
	LoadPCX("pics/fuzz.pcx", &pic, &pal, &w, &h, &bpp);
	if (pic)
	{
		/* touch the whole decoded image, as the renderer's upload would */
		volatile unsigned sum = 0;
		size_t n = (size_t)w * (size_t)h * (size_t)(bpp / 8);
		for (size_t i = 0; i < n; i++)
		{
			sum += pic[i];
		}
	}
	free(pic);
	free(pal);
	return 0;
}
