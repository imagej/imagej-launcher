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
#ifndef COMMON_H
#define COMMON_H

#include <stdarg.h>
#include <string.h>

#if defined(__linux__) && !defined(__TINYC__)
#include "glibc-compat.h"
#endif

/*
 * Issues a message to the console. On Windows, opens a console as needed.
 */
__attribute__((format (printf, 1, 2)))
extern void error(const char *fmt, ...);

extern void enter(const char *func);
extern void leave(void);

__attribute__((format (printf, 1, 2)))
extern void debug(const char *fmt, ...);

__attribute__((__noreturn__))
__attribute__((format (printf, 1, 2)))
extern void die(const char *fmt, ...);

#ifdef WIN32
#include <io.h>
#include <process.h>
#define PATH_SEP ";"

extern void attach_win_console();
extern void new_win_console();
extern void win_error(const char *fmt, ...);
extern void win_verror(const char *fmt, va_list ap);

/* TODO: use dup2() and freopen() and a thread to handle the output */
#else
#define PATH_SEP ":"
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif

#if defined(_WIN64) && !defined(WIN32)
/* TinyCC's stdlib.h undefines WIN32 in 64-bit mode */
#define WIN32 1
#endif

#ifdef __GNUC__
#define MAYBE_UNUSED __attribute__ ((unused))
#else
#define MAYBE_UNUSED
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/* Global functions */
extern int start_ij(void);

/* Global variables */
extern int debug_mode;
extern int debug_indent;
extern int info_mode;

static inline int prefixcmp(const char *string, const char *prefix)
{
	return strncmp(string, prefix, strlen(prefix));
}

static inline int suffixcmp(const char *string, int len, const char *suffix)
{
	int suffix_len = strlen(suffix);
	if (len < 0)
		len = strlen(string);
	if (len < suffix_len)
		return -1;
	return strncmp(string + len - suffix_len, suffix, suffix_len);
}

#endif
