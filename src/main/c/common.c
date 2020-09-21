/*
 * #%L
 * ImageJ software for multidimensional image processing and analysis.
 * %%
 * Copyright (C) 2007 - 2020 ImageJ developers.
 * %%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * #L%
 */
#include <stdio.h>
#include <stdlib.h>

#include "common.h"

__attribute__((format (printf, 1, 2)))
void error(const char *fmt, ...)
{
	va_list ap;
#ifdef WIN32
	const char *windebug = getenv("WINDEBUG");
	if (windebug && *windebug) {
		va_start(ap, fmt);
		win_verror(fmt, ap);
		va_end(ap);
		return;
	}
	new_win_console();
#endif

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void enter(const char *func) {
	debug("%s:", func);
	debug_indent++;
}

void leave(void) {
	debug_indent--;
}

__attribute__((format (printf, 1, 2)))
void debug(const char *fmt, ...)
{
	if (!debug_mode) return;
	va_list ap;
#ifdef WIN32
	const char *windebug = getenv("WINDEBUG");
	if (windebug && *windebug) {
		va_start(ap, fmt);
		win_verror(fmt, ap);
		va_end(ap);
		return;
	}
	new_win_console();
#endif

	int i;
	va_list nothing;
	for (i=0; i<debug_indent; i++)
		vfprintf(stderr, "    ", nothing);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

__attribute__((__noreturn__))
__attribute__((format (printf, 1, 2)))
void die(const char *fmt, ...)
{
	va_list ap;
#ifdef WIN32
	const char *windebug = getenv("WINDEBUG");
	if (windebug && *windebug) {
		va_start(ap, fmt);
		win_verror(fmt, ap);
		va_end(ap);
		exit(1);
	}
	new_win_console();
#endif

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);

	exit(1);
}


