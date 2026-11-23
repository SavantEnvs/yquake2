/*
 * Functional check driver (built with normal flags for mayhem/test.sh):
 * decodes a PCX / WAL / MD2 / SP2 file with the real loaders and prints
 * what they produced, so test.sh can compare it to known values.
 * Usage: asset_dump pcx|wal|md2|sp2 <file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/client/refresh/ref_shared.h"
#include "ref_stubs.h"

static unsigned
fnv(const byte *p, size_t n)
{
	unsigned h = 2166136261u;
	for (size_t i = 0; i < n; i++)
	{
		h = (h ^ p[i]) * 16777619u;
	}
	return h;
}

static struct image_s *
dump_image(const char *name, byte *pic, int width, int realwidth,
	int height, int realheight, size_t data_size, imagetype_t type, int bits)
{
	(void)name; (void)realwidth; (void)realheight; (void)type; (void)data_size;
	printf("wal %dx%d bits=%d hash=%08x\n", width, height, bits,
		fnv(pic, (size_t)width * height * (bits / 8)));
	return NULL;
}

static struct image_s *
dump_skin(const char *name, imagetype_t type)
{
	(void)type;
	printf("skin %s\n", name);
	return NULL;
}

int
main(int argc, char **argv)
{
	FILE *f;
	long len;
	byte *buf;

	if (argc != 3 || !(f = fopen(argv[2], "rb")))
	{
		fprintf(stderr, "usage: %s pcx|wal|md2|sp2 <file>\n", argv[0]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(len);
	if (fread(buf, len, 1, f) != 1)
	{
		return 2;
	}
	fclose(f);

	stubs_init();
	stubs_set_input(buf, (size_t)len);

	if (!strcmp(argv[1], "pcx"))
	{
		byte *pic, *pal;
		int w = 0, h = 0, bpp = 0;
		LoadPCX("pics/test.pcx", &pic, &pal, &w, &h, &bpp);
		if (!pic)
		{
			printf("pcx rejected\n");
			return 1;
		}
		printf("pcx %dx%d bpp=%d hash=%08x pal=%08x\n", w, h, bpp,
			fnv(pic, (size_t)w * h * (bpp / 8)), pal ? fnv(pal, 768) : 0);
	}
	else if (!strcmp(argv[1], "wal"))
	{
		LoadWal("textures/test", it_wall, dump_image);
	}
	else
	{
		struct image_s *skins[MAX_MD2SKINS];
		vec3_t mins, maxs;
		modtype_t type = 0;
		void *extra;
		if (!strcmp(argv[1], "md2"))
		{
			extra = Mod_LoadMD2("models/test.md2", buf, (int)len, mins, maxs,
				skins, dump_skin, &type);
		}
		else
		{
			extra = Mod_LoadSP2("sprites/test.sp2", buf, (int)len, skins,
				dump_skin, &type);
		}
		if (!extra)
		{
			printf("model rejected\n");
			return 1;
		}
		printf("model type=%d\n", (int)type);
		if (type == mod_alias)
		{
			dmdl_t *h = extra;
			printf("md2 frames=%d tris=%d xyz=%d skins=%d glcmds=%d\n",
				h->num_frames, h->num_tris, h->num_xyz, h->num_skins,
				h->num_glcmds);
		}
		else
		{
			dsprite_t *s = extra;
			printf("sp2 frames=%d first=%dx%d %s\n", s->numframes,
				s->frames[0].width, s->frames[0].height, s->frames[0].name);
		}
	}
	return 0;
}
