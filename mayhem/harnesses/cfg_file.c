/*
 * File-input target for the console command interpreter
 * (src/common/cmdparser.c + src/common/cvar.c): what the engine does with a
 * .cfg script (config.cfg, autoexec.cfg, default.cfg from a pak) and with
 * "stuffcmd" text a server sends to a client, both of which end up in the
 * command buffer.
 *
 * The input is run through the engine's own "exec" command: Cmd_Exec_f loads
 * it (FS_LoadFile2), Cbuf_InsertText queues it, and Cbuf_Execute splits it
 * into lines and runs each through Cmd_ExecuteString -> Cmd_TokenizeString
 * (with $cvar macro expansion, COM_Parse) -> alias / command / Cvar_Command.
 * Only the commands Cmd_Init and Cvar_Init register exist (exec, vstr, echo,
 * alias, wait, cmdlist, set/seta/sets/setu, toggle, reset, cvarlist, inc,
 * dec, ...); they only touch the interpreter's and the cvar system's own
 * in-process state, which starts fresh in every process. A nested "exec"
 * finds no file. Cvar_WriteVariables-style file writes are not reachable.
 *
 * Com_Error() ends the process with status 1 (the engine's own behavior on a
 * fatal error), hence a file-input (AFL) target.
 *
 * Usage: cfg_file <script>      prints what the script echoes / prints
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../src/common/header/common.h"

static unsigned char *input;
static long input_len;
static int input_served;

extern sizebuf_t cmd_text;

/* ---- engine services used by cmdparser.c / cvar.c ---- */
void
Com_Printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
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
Com_Quit(void)
{
	exit(0);
}

/* Deterministic frame clock: the engine runs Cbuf_Execute once per frame
 * and "wait" defers the rest of the buffer by ~16 ms of this clock. */
static int frame_ms = 1;

int
Sys_Milliseconds(void)
{
	return frame_ms;
}

int
Com_ServerState(void)
{
	return 0;
}

/* Only "game" changes reach these (Cvar_Set on a latched game cvar); there
 * is no filesystem to rebuild. */
char userGivenGame[MAX_QPATH];

void
FS_BuildGameSpecificSearchPath(const char *dir)
{
	(void)dir;
}

/* Map-name completion only; no files. */
strlist_t
FS_ListFiles2(const char *findname, unsigned musthave, unsigned canthave)
{
	strlist_t l;
	(void)findname; (void)musthave; (void)canthave;
	memset(&l, 0, sizeof(l));
	return l;
}

/* FS_LoadFile2 as the filesystem implements it for Cmd_Exec_f: a buffer of
 * len + extra bytes holding the file. The fuzz input is the only file, and
 * only the first exec finds it. */
int
FS_LoadFile2(const char *path, void **buffer, int extra)
{
	(void)path;
	if (input_served)
	{
		if (buffer)
		{
			*buffer = NULL;
		}
		return -1;
	}
	if (!buffer)
	{
		return (int)input_len;
	}
	input_served = 1;
	*buffer = Z_Malloc(input_len + extra);
	memcpy(*buffer, input, input_len);
	return (int)input_len;
}

int
FS_LoadFile(const char *path, void **buffer)
{
	return FS_LoadFile2(path, buffer, 0);
}

void
FS_FreeFile(void *buffer)
{
	Z_Free(buffer);
}

/* ---- driver ---- */
int
main(int argc, char **argv)
{
	FILE *f;

	if (argc != 2 || !(f = fopen(argv[1], "rb")))
	{
		fprintf(stderr, "usage: %s <script.cfg>\n", argv[0]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	input_len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (input_len < 0)
	{
		return 2;
	}
	input = malloc(input_len ? input_len : 1);
	if (input_len > 0 && fread(input, input_len, 1, f) != 1)
	{
		return 2;
	}
	fclose(f);

	Z_Init();
	Swap_Init();
	Cbuf_Init();
	Cmd_Init();
	Cvar_Init();

	Cbuf_AddText("exec fuzz.cfg\n");
	/* One Cbuf_Execute per engine frame; a script that keeps re-queuing
	 * itself (aliases, vstr, wait) is cut off after a fixed frame count. */
	for (int frame = 0; frame < 64 && cmd_text.cursize; frame++)
	{
		Cbuf_Execute();
		frame_ms += 17;
	}
	return 0;
}
