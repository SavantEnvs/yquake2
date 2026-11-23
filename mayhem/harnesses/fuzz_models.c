/* Alias / sprite model loaders: Mod_LoadMD2 and Mod_LoadSP2
 * (src/client/refresh/files/models.c), dispatched on the file magic the way
 * the renderers' Mod_ForName does. */
#include <string.h>
#include "../../src/client/refresh/ref_shared.h"
#include "ref_stubs.h"

/* Hunk_Begin is wrapped (-Wl,--wrap=Hunk_Begin) so the harness knows whether
 * the loader opened a hunk, including on the loader's "return NULL" paths. */
void *__real_Hunk_Begin(int maxsize);
static void *hunk_base;

void *
__wrap_Hunk_Begin(int maxsize)
{
	hunk_base = __real_Hunk_Begin(maxsize);
	return hunk_base;
}

static struct image_s *
fuzz_find_image(const char *name, imagetype_t type)
{
	volatile size_t n = strlen(name);
	(void)type; (void)n;
	return NULL;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct image_s *skins[MAX_MD2SKINS];
	vec3_t mins, maxs;
	modtype_t type;
	void *extra = NULL;
	unsigned ident;

	/* Mod_ForName reads the 4-byte ident before choosing a loader. */
	if (size < 4)
	{
		return -1;
	}
	stubs_init();
	hunk_base = NULL;
	/* the loaders work on a heap copy, like Mod_ForName's FS_LoadFile buffer */
	void *buf = malloc(size);
	memcpy(buf, data, size);
	memcpy(&ident, data, 4);
	ident = LittleLong(ident);
	if (ident == IDALIASHEADER)
	{
		extra = Mod_LoadMD2("models/fuzz.md2", buf, (int)size, mins, maxs,
			skins, fuzz_find_image, &type);
	}
	else if (ident == IDSPRITEHEADER)
	{
		extra = Mod_LoadSP2("sprites/fuzz.sp2", buf, (int)size,
			skins, fuzz_find_image, &type);
	}
	(void)extra;
	if (hunk_base)
	{
		/* Mod_ForName always closes the hunk with Hunk_End (which records the
		 * mapping size Hunk_Free needs); Mod_Free later releases it. */
		Hunk_End();
		Hunk_Free(hunk_base);
		hunk_base = NULL;
	}
	free(buf);
	return 0;
}
