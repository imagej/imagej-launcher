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
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdlib.h>

#include "string-funcs.h"

/* Dynamic library loading and other platform-dependent stuff */

void setenv_or_exit(const char *name, const char *value, int overwrite);

size_t get_memory_size(int available_only);

#define is_ipv6_broken() 0

#define is_slash(c) ((c) == '/')

/* Directory stuff */

#ifndef WIN32
#include <dirent.h>
#endif

/* Checks whether there a file is a native library, returns 32 or 64 upon success. */

int is_native_library(const char *path);

#ifdef __APPLE__

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <pthread.h>

extern void append_icon_path(struct string *str, const char *main_argv0);
extern int get_fiji_bundle_variable(const char *key, struct string *value);
extern int start_ij_macosx(const char *main_argv0);

#elif defined (__linux__)

#include <dlfcn.h>

#undef is_ipv6_broken
int is_ipv6_broken(void);

#elif defined(WIN32)

#include <windows.h>

#define RTLD_LAZY 0
extern void *dlopen(const char *name, int flags);
extern char *dlerror(void);
extern void *dlsym(void *handle, const char *name);

#ifndef __MINGW32__
/* __MINGW32__ is defined by MinGW64 for both x86 and amd64:
 * http://mingw.5.n7.nabble.com/Macros-MINGW32-AND-MINGW64-tp26319p26320.html
*/
extern void sleep(int seconds);
#endif

/*
 * There is no setenv on Windows, so it should be safe for us to
 * define this compatible version.
 */
extern int setenv(const char *name, const char *value, int overwrite);

/* Similarly we can do the same for unsetenv: */
extern int unsetenv(const char *name);

extern char *get_win_error(void);
extern void win_verror(const char *fmt, va_list ap);
extern void win_error(const char *fmt, ...);

#undef is_slash
static int is_slash(char c)
{
	return c == '\\' || c == '/';
}

#define mkdir fake_posix_mkdir

struct entry {
	char d_name[PATH_MAX];
	int d_namlen;
} entry;

struct dir {
	struct string *pattern;
	HANDLE handle;
	WIN32_FIND_DATA find_data;
	int done;
	struct entry entry;
};
#define DIR struct dir
#define dirent entry
#define opendir open_dir
#define readdir read_dir
#define closedir close_dir

extern DIR *open_dir(const char *path);
extern struct entry *read_dir(DIR *dir);
extern int close_dir(DIR *dir);

extern char *dos_path(const char *path);

extern int console_opened, console_attached;

#endif

static const char *get_platform(void)
{
#ifdef __APPLE__
	return "macosx";
#endif
#ifdef WIN32
	return sizeof(void *) < 8 ? "win32" : "win64";
#endif
#ifdef __linux__
	return sizeof(void *) < 8 ? "linux" : "linux-amd64";
#endif
	return NULL;
}

#endif
