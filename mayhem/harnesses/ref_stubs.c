/*
 * Minimal refimport_t for fuzzing the renderer-independent asset loaders in
 * src/client/refresh/files/. FS_LoadFile hands out a heap copy of the current
 * fuzz input (exactly sized so ASan sees overreads); everything else is inert.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../src/client/refresh/ref_shared.h"
#include "ref_stubs.h"

refimport_t ri;

static const uint8_t *cur_data;
static size_t cur_size;

static int
stub_FS_LoadFile(const char *name, void **buf)
{
	(void)name;
	if (!buf)
	{
		return (int)cur_size;
	}
	void *p = malloc(cur_size ? cur_size : 1);
	memcpy(p, cur_data, cur_size);
	*buf = p;
	return (int)cur_size;
}

static void
stub_FS_FreeFile(void *buf)
{
	free(buf);
}

static void
stub_Com_VPrintf(int level, const char *fmt, va_list ap)
{
	(void)level; (void)fmt; (void)ap;
}

/* Sys_Error is only reached on a fatal engine condition (hunk overflow, mmap
 * failure); the engine would terminate, so abort to surface it. */
static void __attribute__((noreturn))
stub_Sys_Error(int code, const char *fmt, ...)
{
	va_list ap;
	(void)code;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	abort();
}

void
stubs_init(void)
{
	memset(&ri, 0, sizeof(ri));
	ri.FS_LoadFile = stub_FS_LoadFile;
	ri.FS_FreeFile = stub_FS_FreeFile;
	ri.Com_VPrintf = stub_Com_VPrintf;
	ri.Sys_Error = stub_Sys_Error;
	Swap_Init(); /* the renderers do this in R_Init */
}

void
stubs_set_input(const uint8_t *data, size_t size)
{
	cur_data = data;
	cur_size = size;
}
