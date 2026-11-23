/*
 * File-input target for the collision-map BSP loader CM_LoadMap
 * (src/common/collision.c) — what the server runs on `map <name>` — followed
 * by the queries the server makes against the loaded map: point contents,
 * box traces, PVS/PHS decompression, area flooding, inline models.
 *
 * The engine services collision.c imports are provided here: FS_LoadFile hands
 * out the input file for the .bsp (no .ent override file exists), a small
 * cvar store, and Com_Error(ERR_DROP), which aborts the map load in the engine,
 * ends the process with status 1 — hence a file-input (AFL) target.
 * Usage: bsp_file <map.bsp>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../src/common/header/common.h"

static const char *input_path;

/* ---- engine services used by collision.c ---- */
void
Com_Printf(const char *fmt, ...)
{
	(void)fmt;
}

void
Com_DPrintf(const char *fmt, ...)
{
	(void)fmt;
}

void
Com_Error(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "Com_Error(%d): ", code);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* crc.c: only reached on an engine invariant violation */
void
Sys_Error(const char *fmt, ...)
{
	(void)fmt;
	abort();
}

static cvar_t *cvars;

cvar_t *
Cvar_Get(const char *name, const char *value, int flags)
{
	cvar_t *v;
	for (v = cvars; v; v = v->next)
	{
		if (!strcmp(v->name, name))
		{
			return v;
		}
	}
	v = calloc(1, sizeof(*v));
	v->name = strdup(name);
	v->string = strdup(value);
	v->value = (float)atof(value);
	v->flags = flags;
	v->next = cvars;
	cvars = v;
	return v;
}

float
Cvar_VariableValue(const char *name)
{
	for (cvar_t *v = cvars; v; v = v->next)
	{
		if (!strcmp(v->name, name))
		{
			return v->value;
		}
	}
	return 0;
}

/* the server's sv_entfile (default 1: look for a .ent override; none exists) */
cvar_t *sv_entfile;

int
FS_LoadFile(const char *name, void **buf)
{
	FILE *f;
	long len;
	size_t nlen = strlen(name);

	*buf = NULL;
	if (nlen < 4 || strcmp(name + nlen - 4, ".bsp") || !(f = fopen(input_path, "rb")))
	{
		return -1;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	*buf = malloc(len > 0 ? len : 1);
	if (len > 0 && fread(*buf, len, 1, f) != 1)
	{
		len = 0;
	}
	fclose(f);
	return (int)len;
}

void
FS_FreeFile(void *buf)
{
	free(buf);
}

int
FS_Read(void *buffer, int len, fileHandle_t f)
{
	(void)buffer; (void)f;
	return len;
}

int
main(int argc, char **argv)
{
	unsigned checksum = 0;
	cmodel_t *world;
	byte *vis;
	int i, n, leafs[64], topnode;
	trace_t tr;
	vec3_t mins = {-16, -16, -24}, maxs = {16, 16, 32}, zero = {0, 0, 0};

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <map.bsp>\n", argv[0]);
		return 2;
	}
	input_path = argv[1];
	Swap_Init();
	sv_entfile = Cvar_Get("sv_entfile", "1", 0);

	world = CM_LoadMap("maps/fuzz.bsp", false, &checksum);
	printf("map: checksum=%08x clusters=%d inline_models=%d entchars=%zu\n",
		checksum, CM_NumClusters(), CM_NumInlineModels(),
		strlen(CM_EntityString()));

	/* the per-frame queries SV_LinkEdict / SV_Trace / SV_BuildClientFrame make */
	vec3_t pts[3] = {{0, 0, 0}, {world->mins[0], world->mins[1], world->mins[2]},
		{world->maxs[0], world->maxs[1], world->maxs[2]}};
	for (i = 0; i < 3; i++)
	{
		int leaf = CM_PointLeafnum(pts[i]);
		int cluster = CM_LeafCluster(leaf);
		int area = CM_LeafArea(leaf);
		(void)CM_PointContents(pts[i], 0);
		if (cluster >= 0)
		{
			vis = CM_ClusterPVS(cluster);
			(void)vis;
			vis = CM_ClusterPHS(cluster);
			(void)vis;
		}
		(void)CM_AreasConnected(area, 1);
		tr = CM_BoxTrace(pts[i], pts[(i + 1) % 3], mins, maxs, 0, MASK_PLAYERSOLID);
		(void)tr;
	}
	{
		vec3_t a = {0, 0, 64}, b = {0, 0, -32}, below = {0, 0, -10};
		tr = CM_BoxTrace(a, b, zero, zero, 0, MASK_SOLID);
		printf("trace: fraction=%.3f endz=%.1f contents=%d below=%d\n",
			tr.fraction, tr.endpos[2], tr.contents, CM_PointContents(below, 0));
	}
	n = CM_BoxLeafnums(world->mins, world->maxs, leafs, 64, &topnode);
	(void)n;
	(void)CM_HeadnodeForBox(mins, maxs);
	for (i = 1; i < CM_NumInlineModels(); i++)
	{
		char name[16];
		snprintf(name, sizeof(name), "*%d", i);
		cmodel_t *m = CM_InlineModel(name);
		tr = CM_TransformedBoxTrace(zero, m->origin, mins, maxs, m->headnode,
			MASK_SOLID, zero, zero);
	}
	return 0;
}
