/*
 * #%L
 * ImageJ software for multidimensional image processing and analysis.
 * %%
 * Copyright (C) 2007 - 2016 Board of Regents of the University of
 * Wisconsin-Madison, Broad Institute of MIT and Harvard, and Max Planck
 * Institute of Molecular Cell Biology and Genetics.
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
#include "common.h"
#include "splash.h"
#include "file-funcs.h"
#include "java.h"
#include "platform.h"
#include "string-funcs.h"

/* Splash screen */

static int no_splash;
static void (*SplashClose)(void);

void disable_splash(void)
{
	no_splash = 1;
}

struct string *get_splashscreen_lib_path(const char *jre_home)
{
#if defined(__APPLE__)
	struct string *search_root = string_initf("/System/Library/Java/JavaVirtualMachines");
	struct string *result = string_init(32);
	if (!find_file(search_root, 4, "libsplashscreen.jnilib", result)) {
		string_release(result);
		result = NULL;
	}
	string_release(search_root);
	return result;
#endif
	if (!jre_home)
		return NULL;
#if defined(WIN32)
	return string_initf("%s/bin/splashscreen.dll", jre_home);
#elif defined(__linux__)
	return string_initf("%s/lib/%s/libsplashscreen.so", jre_home, sizeof(void *) == 8 ? "amd64" : "i386");
#else
	return NULL;
#endif
}

void show_splash(const char *file_jar_name)
{
	const char *image_path = NULL;
	struct string *lib_path = get_splashscreen_lib_path(get_jre_home());
	void *splashscreen;
	int (*SplashInit)(void);
	int (*SplashLoadFile)(const char *path);
	int (*SplashSetFileJarName)(const char *file_path, const char *jar_path);

	if (no_splash || !lib_path || SplashClose)
		return;

	if (FLAT_SPLASH_PATH)
		image_path = ij_path(FLAT_SPLASH_PATH);
	if (!image_path || !file_exists(image_path))
		image_path = ij_path(SPLASH_PATH);
	if (!image_path || !file_exists(image_path))
		return;

	splashscreen = dlopen(lib_path->buffer, RTLD_LAZY);
	if (!splashscreen) {
		if (debug)
			error("Splashscreen library not found: '%s'", lib_path->buffer);
		string_release(lib_path);
		return;
	}
	SplashInit = dlsym(splashscreen, "SplashInit");
	SplashLoadFile = dlsym(splashscreen, "SplashLoadFile");
	SplashSetFileJarName = dlsym(splashscreen, "SplashSetFileJarName");
	SplashClose = dlsym(splashscreen, "SplashClose");
	if (!SplashInit || !SplashLoadFile || !SplashSetFileJarName || !SplashClose) {
		if (debug)
			error("Ignoring splashscreen:\ninit: %p\nload: %p\nsetFileJar: %p\nclose: %p", SplashInit, SplashLoadFile, SplashSetFileJarName, SplashClose);
		string_release(lib_path);
		SplashClose = NULL;
		return;
	}

	SplashInit();
	SplashLoadFile(image_path);
	SplashSetFileJarName(image_path, file_jar_name);

	string_release(lib_path);
}

void hide_splash(void)
{
	if (!SplashClose)
		return;
	SplashClose();
	SplashClose = NULL;
}


