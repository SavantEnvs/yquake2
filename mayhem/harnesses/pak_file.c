/*
 * File-input target for the engine's pack-archive filesystem
 * (src/common/filesystem.c + src/common/unzip): what q2ded / the client do at
 * startup with every pakN.pak / pakN.pk3 in a game directory.
 *
 * The input is loaded in place with FS_LoadPK3 (when it starts with the ZIP
 * magic "PK") or FS_LoadPAK, which parse the archive directory, and mounted
 * on the search path as FS_AddDirToSearchPath does; then every
 * file the archive lists is read back through FS_LoadFile() (FS_FOpenFile ->
 * pack search -> fseek/fread for .pak, unzLocateFile + inflate for .pk3).
 *
 * filesystem.c is #included so its search-path list can be walked; the engine
 * services it imports are stubbed below. Com_Error() ends the process with
 * status 1 (ERR_FATAL on a bad archive is the engine's own behavior), hence a
 * file-input (AFL) target.
 *
 * Usage: pak_file <archive>      prints "<name> <size> <fnv1a-of-contents>" per file
 */
#include "../../src/common/filesystem.c"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

/* ---- engine services used by filesystem.c ---- */
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

void
Sys_Error(const char *fmt, ...)
{
	(void)fmt;
	abort();
}

void *
Z_Malloc(size_t size)
{
	void *p = calloc(1, size);
	if (!p)
	{
		Com_Error(ERR_FATAL, "Z_Malloc: failed on allocation of %zu bytes", size);
	}
	return p;
}

void
Z_Free(void *ptr)
{
	free(ptr);
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
	v->string = strdup(value ? value : "");
	v->value = (float)atof(v->string);
	v->flags = flags;
	v->next = cvars;
	cvars = v;
	return v;
}

cvar_t *
Cvar_FullSet(const char *name, const char *value, int flags)
{
	cvar_t *v = Cvar_Get(name, value, flags);
	free(v->string);
	v->string = strdup(value);
	v->value = (float)atof(value);
	return v;
}

void Cmd_AddCommand(const char *name, xcommand_t f) { (void)name; (void)f; }
void Cmd_RemoveCommand(const char *name) { (void)name; }
int Cmd_Argc(void) { return 0; }
char *Cmd_Argv(int i) { (void)i; return ""; }

void
Sys_Mkdir(const char *path)
{
	mkdir(path, 0700);
}

/* No host-directory listing: only the numbered pak0.* is mounted. */
char *Sys_FindFirst(const char *path, unsigned musthave, unsigned canhave)
{
	(void)path; (void)musthave; (void)canhave;
	return NULL;
}
char *Sys_FindNext(unsigned musthave, unsigned canhave) { (void)musthave; (void)canhave; return NULL; }
void Sys_FindClose(void) {}

/* Startup-time services FS_BuildGameSpecificSearchPath / FS_BuildRawPath use. */
cvar_t *dedicated;
qboolean is_portable;
void Cbuf_AddText(const char *text) { (void)text; }
void Qcommon_ExecConfigs(qboolean gameStartUp) { (void)gameStartUp; }
void OGG_InitTrackList(void) {}
void CleanCachedMapsList(void) {}
void CL_WriteConfiguration(void) {}
qboolean menu_startdemoloop;
char *Sys_GetHomeDir(void) { return NULL; }
const char *Sys_GetBinaryDir(void) { return ""; }
qboolean Sys_Realpath(const char *in, char *out, size_t size) { Q_strlcpy(out, in, size); return true; }

char *
CopyString(const char *in)
{
	return strdup(in);
}

/* ---- driver ---- */
int
main(int argc, char **argv)
{
	unsigned char magic[2] = {0, 0};
	fsPack_t *pack;
	fsSearchPath_t *search;
	FILE *in;
	int pk3;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <archive>\n", argv[0]);
		return 2;
	}
	if (!(in = fopen(argv[1], "rb")))
	{
		perror(argv[1]);
		return 2;
	}
	pk3 = (fread(magic, 1, 2, in) == 2 && magic[0] == 'P' && magic[1] == 'K');
	fclose(in);

	Swap_Init();
	fs_debug = Cvar_Get("fs_debug", "0", 0);
	fs_gamedirvar = Cvar_Get("game", "", 0);
	fs_basedir = Cvar_Get("basedir", ".", 0);
	fs_cddir = Cvar_Get("cddir", "", 0);

	/* Mount the archive the way FS_AddDirToSearchPath mounts a numbered
	 * pakN.pak / pakN.pk3: load it in place (no copy into a game dir, so
	 * nothing depends on a writable cwd), sort it, push it on the search
	 * path. The engine picks the loader by file suffix; the input has none,
	 * so a ZIP local-header magic selects the PK3 loader. */
	pack = pk3 ? FS_LoadPK3(argv[1]) : FS_LoadPAK(argv[1]);
	if (!pack)
	{
		fprintf(stderr, "not a pack\n");
		return 1;
	}
	FS_SortPack(pack);
	search = Z_Malloc(sizeof(fsSearchPath_t));
	search->pack = pack;
	search->next = fs_searchPaths;
	fs_searchPaths = search;

	for (fsSearchPath_t *s = fs_searchPaths; s; s = s->next)
	{
		if (!s->pack)
		{
			continue;
		}
		/* Read back at most 256 entries: a directory can claim millions. */
		for (int i = 0; i < s->pack->numFiles && i < 256; i++)
		{
			char name[MAX_QPATH];
			void *data = NULL;
			int len;

			Q_strlcpy(name, s->pack->files[i].name, sizeof(name));
			len = FS_LoadFile(name, &data);
			if (len >= 0 && data)
			{
				unsigned h = 2166136261u;
				for (int k = 0; k < len; k++)
				{
					h = (h ^ ((unsigned char *)data)[k]) * 16777619u;
				}
				printf("%s %d %08x\n", name, len, h);
				FS_FreeFile(data);
			}
			else
			{
				printf("%s missing\n", name);
			}
		}
	}
	return 0;
}
