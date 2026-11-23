/*
 * File-input target for the game module's savegame readers
 * (src/game/savegame/savegame.c), driven through the exported game API
 * exactly as the server's `load` command does (SV_ReadServerFile ->
 * ge->ReadGame, SV_ReadLevelFile -> ge->ReadLevel).
 *
 * The game module is linked statically and given a stub game_import_t:
 * a small cvar store, tagged allocations, and inert collision/network calls.
 * gi.error() is the engine's ERR_DROP (it aborts the load); here it ends the
 * process with status 1, which is why this is a file-input (AFL) target.
 *
 * Usage:
 *   savegame_file game  <game.ssv>   ReadGame
 *   savegame_file level <level.sav>  ReadLevel (after a fresh game init)
 *   savegame_file write <dir>        write seed saves (game.ssv, level.sav)
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../src/game/header/local.h"

extern game_export_t *GetGameAPI(const game_import_t *import);

static game_export_t *ge;

/* ---- console variables ---- */
static cvar_t *cvars;

static cvar_t *
find_cvar(const char *name)
{
	for (cvar_t *v = cvars; v; v = v->next)
	{
		if (!strcmp(v->name, name))
		{
			return v;
		}
	}
	return NULL;
}

static void
set_value(cvar_t *v, const char *value)
{
	free(v->string);
	v->string = strdup(value);
	v->value = (float)atof(value);
	v->modified = true;
}

static cvar_t *
stub_cvar(const char *name, const char *value, int flags)
{
	cvar_t *v = find_cvar(name);
	if (v)
	{
		v->flags |= flags;
		return v;
	}
	v = calloc(1, sizeof(*v));
	v->name = strdup(name);
	v->flags = flags;
	set_value(v, value ? value : "");
	v->next = cvars;
	cvars = v;
	return v;
}

static cvar_t *
stub_cvar_set(const char *name, const char *value)
{
	cvar_t *v = find_cvar(name);
	if (!v)
	{
		return stub_cvar(name, value, 0);
	}
	set_value(v, value);
	return v;
}

/* ---- tagged memory ---- */
typedef struct tagblock_s
{
	struct tagblock_s *prev, *next;
	int tag;
	int pad;
} tagblock_t;

static tagblock_t tag_head = { &tag_head, &tag_head, 0, 0 };

/* Com_Error(ERR_FATAL): the engine prints and terminates (Sys_Error). */
static void __attribute__((noreturn))
stub_fatal(const char *what, long long v)
{
	fprintf(stderr, "Com_Error(ERR_FATAL): %s: %lld\n", what, v);
	exit(1);
}

/* The engine's gi.TagMalloc: GI_TagMalloc (src/server/sv_game.c) ->
 * Z_TagMalloc (src/common/zone.c), with the same checks in the same order:
 * size < 0 and size == 0 are fatal errors, an out-of-range tag becomes 0,
 * and a failed allocation is a fatal error. */
static void *
stub_TagMalloc(int size, int tag)
{
	tagblock_t *b;

	if (size < 0)
	{
		stub_fatal("GI_TagMalloc: size < 0", size);
	}
	if ((tag > USHRT_MAX) || (tag < 0))
	{
		tag = 0;
	}
	if (!size)
	{
		stub_fatal("Z_TagMalloc: bad allocation size", size);
	}
	b = calloc(1, sizeof(*b) + (size_t)size);
	if (!b)
	{
		stub_fatal("Z_TagMalloc: failed to allocate", (long long)size);
	}
	b->tag = tag;
	b->next = tag_head.next;
	b->prev = &tag_head;
	tag_head.next->prev = b;
	tag_head.next = b;
	return b + 1;
}

static void
stub_TagFree(void *p)
{
	tagblock_t *b = (tagblock_t *)p - 1;
	b->prev->next = b->next;
	b->next->prev = b->prev;
	free(b);
}

static void
stub_FreeTags(int tag)
{
	tagblock_t *b, *next;
	for (b = tag_head.next; b != &tag_head; b = next)
	{
		next = b->next;
		if (b->tag == tag)
		{
			stub_TagFree(b + 1);
		}
	}
}

/* ---- printing / errors ---- */
static void stub_bprintf(int l, const char *fmt, ...) { (void)l; (void)fmt; }
static void stub_dprintf(const char *fmt, ...) { (void)fmt; }
static void stub_cprintf(const edict_t *e, int l, const char *fmt, ...) { (void)e; (void)l; (void)fmt; }
static void stub_centerprintf(const edict_t *e, const char *fmt, ...) { (void)e; (void)fmt; }

static void __attribute__((noreturn))
stub_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "gi.error: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* ---- inert engine services ---- */
static void stub_sound(const edict_t *e, int c, int s, float v, float a, float t) { (void)e; (void)c; (void)s; (void)v; (void)a; (void)t; }
static void stub_psound(const vec3_t o, const edict_t *e, int c, int s, float v, float a, float t) { (void)o; (void)e; (void)c; (void)s; (void)v; (void)a; (void)t; }
static void stub_configstring(int n, const char *s) { (void)n; (void)s; }
static int stub_index(const char *name) { return name && name[0] ? 1 : 0; }
static void stub_setmodel(edict_t *e, const char *name) { (void)name; e->s.modelindex = 1; }

static trace_t
stub_trace(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, const edict_t *passent, int mask)
{
	trace_t t;
	(void)start; (void)mins; (void)maxs; (void)passent; (void)mask;
	memset(&t, 0, sizeof(t));
	t.fraction = 1.0f;
	VectorCopy(end, t.endpos);
	t.ent = ge->edicts; /* the world, as SV_Trace returns */
	return t;
}

static int stub_pointcontents(const vec3_t p) { (void)p; return 0; }
static qboolean stub_inPVS(vec3_t a, vec3_t b) { (void)a; (void)b; return true; }
static void stub_SetAreaPortalState(int p, qboolean o) { (void)p; (void)o; }
static qboolean stub_AreasConnected(int a, int b) { (void)a; (void)b; return true; }
static void stub_linkentity(edict_t *e) { (void)e; }
static int stub_BoxEdicts(const vec3_t a, const vec3_t b, edict_t **l, int m, int t) { (void)a; (void)b; (void)l; (void)m; (void)t; return 0; }
static void stub_Pmove(pmove_t *p) { (void)p; }
static void stub_multicast(const vec3_t o, multicast_t t) { (void)o; (void)t; }
static void stub_unicast(const edict_t *e, qboolean r) { (void)e; (void)r; }
static void stub_WriteInt(int c) { (void)c; }
static void stub_WriteFloat(float f) { (void)f; }
static void stub_WriteString(const char *s) { (void)s; }
static void stub_WriteVec(const vec3_t v) { (void)v; }
static int stub_argc(void) { return 0; }
static char *stub_argv(int n) { (void)n; return ""; }
static void stub_AddCommandString(const char *t) { (void)t; }
static void stub_DebugGraph(float v, int c) { (void)v; (void)c; }

static void
init_game(void)
{
	static game_import_t gi_stub;

	gi_stub.bprintf = stub_bprintf;
	gi_stub.dprintf = stub_dprintf;
	gi_stub.cprintf = stub_cprintf;
	gi_stub.centerprintf = stub_centerprintf;
	gi_stub.sound = stub_sound;
	gi_stub.positioned_sound = stub_psound;
	gi_stub.configstring = stub_configstring;
	gi_stub.error = stub_error;
	gi_stub.modelindex = stub_index;
	gi_stub.soundindex = stub_index;
	gi_stub.imageindex = stub_index;
	gi_stub.setmodel = stub_setmodel;
	gi_stub.trace = stub_trace;
	gi_stub.pointcontents = stub_pointcontents;
	gi_stub.inPVS = stub_inPVS;
	gi_stub.inPHS = stub_inPVS;
	gi_stub.SetAreaPortalState = stub_SetAreaPortalState;
	gi_stub.AreasConnected = stub_AreasConnected;
	gi_stub.linkentity = stub_linkentity;
	gi_stub.unlinkentity = stub_linkentity;
	gi_stub.BoxEdicts = stub_BoxEdicts;
	gi_stub.Pmove = stub_Pmove;
	gi_stub.multicast = stub_multicast;
	gi_stub.unicast = stub_unicast;
	gi_stub.WriteChar = stub_WriteInt;
	gi_stub.WriteByte = stub_WriteInt;
	gi_stub.WriteShort = stub_WriteInt;
	gi_stub.WriteLong = stub_WriteInt;
	gi_stub.WriteFloat = stub_WriteFloat;
	gi_stub.WriteString = stub_WriteString;
	gi_stub.WritePosition = stub_WriteVec;
	gi_stub.WriteDir = stub_WriteVec;
	gi_stub.WriteAngle = stub_WriteFloat;
	gi_stub.TagMalloc = stub_TagMalloc;
	gi_stub.TagFree = stub_TagFree;
	gi_stub.FreeTags = stub_FreeTags;
	gi_stub.cvar = stub_cvar;
	gi_stub.cvar_set = stub_cvar_set;
	gi_stub.cvar_forceset = stub_cvar_set;
	gi_stub.argc = stub_argc;
	gi_stub.argv = stub_argv;
	gi_stub.args = (char *(*)(void))stub_argv;
	gi_stub.AddCommandString = stub_AddCommandString;
	gi_stub.DebugGraph = stub_DebugGraph;

	ge = GetGameAPI(&gi_stub);
	ge->Init();
}

/* A small single-player map: world, a spawn point, items, a monster, a door. */
static char seed_entities[] =
	"{\n\"classname\" \"worldspawn\"\n\"message\" \"fuzz\"\n\"sky\" \"unit1_\"\n}\n"
	"{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 24\"\n\"angle\" \"90\"\n}\n"
	"{\n\"classname\" \"item_health\"\n\"origin\" \"64 0 16\"\n}\n"
	"{\n\"classname\" \"weapon_shotgun\"\n\"origin\" \"-64 0 16\"\n}\n"
	"{\n\"classname\" \"monster_soldier\"\n\"origin\" \"128 128 24\"\n\"targetname\" \"t1\"\n}\n"
	"{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n\"angle\" \"-1\"\n\"target\" \"t1\"\n}\n"
	"{\n\"classname\" \"target_crosslevel_target\"\n\"delay\" \"2\"\n}\n";

int
main(int argc, char **argv)
{
	char path[1024];

	if (argc != 3)
	{
		fprintf(stderr, "usage: %s game|level|write <file|dir>\n", argv[0]);
		return 2;
	}

	init_game();

	if (!strcmp(argv[1], "game"))
	{
		ge->ReadGame(argv[2]);
		printf("game: maxclients=%d maxentities=%d helpmessage1=\"%s\"\n",
			game.maxclients, game.maxentities, game.helpmessage1);
	}
	else if (!strcmp(argv[1], "level"))
	{
		ge->ReadLevel(argv[2]);
		printf("level: mapname=\"%s\" num_edicts=%d total_monsters=%d\n",
			level.mapname, globals.num_edicts, level.total_monsters);
	}
	else if (!strcmp(argv[1], "write"))
	{
		ge->SpawnEntities("fuzz", seed_entities, "");
		snprintf(path, sizeof(path), "%s/game.ssv", argv[2]);
		ge->WriteGame(path, false);
		snprintf(path, sizeof(path), "%s/level.sav", argv[2]);
		ge->WriteLevel(path);
		printf("write: num_edicts=%d total_monsters=%d\n",
			globals.num_edicts, level.total_monsters);
	}
	else
	{
		return 2;
	}
	return 0;
}
