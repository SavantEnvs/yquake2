/*
 * File-input target for the WAV parser GetWavinfo (src/client/sound/wave.c),
 * fed exactly as S_LoadSound does: the whole file in a heap buffer.
 * GetWavinfo rejects some inputs with Com_Error(ERR_DROP), which in the engine
 * unwinds the frame; here it ends the process with status 1 (a rejection,
 * not a crash), which is why this is a file-input (AFL) target.
 * Usage: wav_file <file.wav>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "../../src/client/header/client.h"
#include "../../src/client/sound/header/local.h"

void
Com_Printf(const char *fmt, ...)
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

int
main(int argc, char **argv)
{
	FILE *f;
	long len;
	byte *buf;
	wavinfo_t info;

	if (argc != 2 || !(f = fopen(argv[1], "rb")))
	{
		fprintf(stderr, "usage: %s <file.wav>\n", argv[0]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(len > 0 ? len : 1);
	if (len > 0 && fread(buf, len, 1, f) != 1)
	{
		return 2;
	}
	fclose(f);

	info = GetWavinfo("sound/fuzz.wav", buf, (int)len);
	printf("rate=%d width=%d channels=%d loopstart=%d samples=%d dataofs=%d\n",
		info.rate, info.width, info.channels, info.loopstart, info.samples,
		info.dataofs);
	if (info.samples > 0 && info.dataofs > 0)
	{
		/* Checksum the sample data GetWavinfo located, clamped to the file:
		 * the harness only reports what the parser found, so an
		 * out-of-range samples/dataofs is left for the parser frames
		 * (GetWavinfo / GetLittleLong) rather than crashing here. */
		volatile unsigned sum = 0;
		long n = (long)info.samples * info.width * info.channels;
		if (info.dataofs >= len)
		{
			n = 0;
		}
		else if (n > len - info.dataofs)
		{
			n = len - info.dataofs;
		}
		for (long i = 0; i < n; i++)
		{
			sum += buf[info.dataofs + i];
		}
	}
	free(buf);
	return 0;
}
