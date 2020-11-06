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
#include "common.h"
#include "java.h"
#include "file-funcs.h"
#include "platform.h"
#include "string-funcs.h"
#include "xalloc.h"

/*
 * If set, overrides the environment variable JAVA_HOME, which in turn
 * overrides relative_java_home.
 */
static const char *absolute_java_home;
static const char *relative_java_home;
static const char *library_path;
static const char *default_library_path;
#if defined(__APPLE__)
static const char *default_library_paths[6] =
{
	"Contents/Home/jre/lib/jli/libjli.dylib",
	"jre/lib/jli/libjli.dylib",
	"Contents/Home/lib/jli/libjli.dylib",
	"lib/jli/libjli.dylib",
	"Contents/MacOS/libjli.dylib",
	"Contents/Libraries/libjli.jnilib"
};
#elif defined(WIN32)
static const char *default_library_paths[4] =
{
	"jre/bin/client/jvm.dll",
	"bin/client/jvm.dll",
	"jre/bin/server/jvm.dll",
	"bin/server/jvm.dll"
};
#else
static const char *default_library_paths[8] =
{
	"lib/i386/server/libjvm.so",
	"jre/lib/i386/server/libjvm.so",
	"lib/i386/client/libjvm.so",
	"jre/lib/i386/client/libjvm.so",
	"lib/amd64/server/libjvm.so",
	"jre/lib/amd64/server/libjvm.so",
	"lib/server/libjvm.so",
	"jre/lib/server/libjvm.so"
};
#endif
static struct string *legacy_jre_path;


const char *get_java_command(void)
{
#ifdef WIN32
	if (!console_opened)
		return "javaw";
#endif
	return "java";
}

static const char *parse_number(const char *string, unsigned int *result, int shift)
{
	char *endp;
	long value = strtol(string, &endp, 10);

	if (string == endp)
		return NULL;

	*result |= (int)(value << shift);
	return endp;
}

void set_java_home(const char *absolute_path)
{
	enter("set_java_home");
	absolute_java_home = absolute_path;
	debug("absolute_java_home -> %s", absolute_java_home);
	leave();
}

void set_relative_java_home(const char *relative_path)
{
	enter("set_relative_java_home");
	relative_java_home = relative_path;
	debug("relative_java_home -> %s", relative_java_home);
	leave();
}

int is_jre_home(const char *directory)
{
	enter("is_jre_home");
	debug("directory is %s", directory);
	int i;
	int result = 0;
	if (dir_exists(directory)) {
		// Check if one of default_library_paths exists
		int arrayLength = sizeof(default_library_paths)/sizeof(default_library_paths[0]);
		for (i=0; i<arrayLength; i++) {
			struct string* libjvm = string_initf("%s/%s", directory, default_library_paths[i]);
			if (!file_exists(libjvm->buffer)) {
				debug("Ignoring JAVA_HOME (does not exist): %s", libjvm->buffer);
			}
			else if (!is_native_library(libjvm->buffer)) {
				debug("Ignoring JAVA_HOME (wrong arch): %s", libjvm->buffer);
			}
			else {
				debug("Identified JAVA_HOME: %s", libjvm->buffer);
				result = 1;
				break;
			}
			string_release(libjvm);
		}
	}
	leave();
	return result;
}

/**
 * Checks if a directory is a java home directory by calling is_jre_home
 * on <directory>/jre and <directory>.
 */
int is_java_home(const char *directory)
{
	enter("is_java_home");
	debug("directory = %s", directory);
	struct string *jre = string_initf("%s", directory);
	int result = is_jre_home(jre->buffer);
	if (!result) {
		// Java9 does not have a jre subfolder -> check directory
		string_set(jre, directory);
		result = is_jre_home(jre->buffer);
	}
	string_release(jre);
	leave();
	return result;
}

const char *get_java_home_env(void)
{
	enter("get_java_home_env");
	const char *env = getenv("JAVA_HOME");
	debug("JAVA_HOME is set to %s", env);
	if (env && is_java_home(env)) {
		leave();
		return env;
	}
	leave();
	return NULL;
}

const char *get_java_home(void)
{
	enter("get_java_home");
	const char *result;

	/* Check if an absolute path has been previously set */
	if (absolute_java_home) {
		debug("Using absolute_java_home: %s", absolute_java_home);
		leave();
		return absolute_java_home;
	}

	/* Check if a relative path has been previously set */
	result = !relative_java_home ? NULL : ij_path(relative_java_home);
	debug("Trying to use relative_java_home: %s", result);
	if (result && is_java_home(result)) {
		debug("Returning %s", result);
		leave();
		return result;
	}
	if (result && (!suffixcmp(result, -1, "/jre") ||
			 !suffixcmp(result, -1, "/jre/")) &&
			is_jre_home(result))
	{
		/* Strip jre/ from result */
		char *new_eol = (char *)(result + strlen(result) - 4);
		*new_eol = '\0';
		debug("Returning %s", result);
		leave();
		return result;
	}

	/* Check JAVA_HOME environment variable */
	result = get_java_home_env();
	if (result) {
		debug("Returning %s", result);
		leave();
		return result;
	}

	/* Otherwise use the system's Java */
	debug("Returning discover_system_java_home()");
	leave();
	return discover_system_java_home();
}

/* Returns the JRE/JAVA HOME folder that will be used */
const char *get_jre_home(void)
{
	enter("get_jre_home");
	const char *result;
	int len;
	static struct string *jre;
	static int initialized;

	if (jre) {
		debug("get_jre_home: Returning %s", jre->buffer);
		leave();
		return jre->buffer;
	}	

	if (initialized) {
		debug("get_jre_home: Returning NULL");
		leave();
		return NULL;
	}	
	initialized = 1;

	/* ImageJ 1.x ships the JRE in <ij.dir>/jre/ */
	result = legacy_jre_path ? legacy_jre_path->buffer : get_java_home();
	if (!result) {
		const char *jre_home = getenv("JRE_HOME");
		if (jre_home && *jre_home && is_jre_home(jre_home)) {
			jre = string_copy(jre_home);
			debug("Setting jre to %s", jre->buffer);
			debug("Found a JRE in JRE_HOME: %s", jre->buffer);
			leave();
			return jre->buffer;
		}
		jre_home = getenv("JAVA_HOME");
		if (jre_home && *jre_home && is_jre_home(jre_home)) {
			jre = string_copy(jre_home);
			debug("get_jre_home: Setting jre to %s", jre->buffer);
			debug("Found a JRE in JAVA_HOME: %s", jre->buffer);
			leave();
			return jre->buffer;
		}
		debug("No JRE was found in default locations");
		leave();
		return NULL;
	}

	len = strlen(result);
	if (len > 4 && !suffixcmp(result, len, "/jre")) {
		jre = string_copy(result);
		debug("Setting jre to %s", jre->buffer);
		debug("JAVA_HOME points to a JRE: '%s'", result);
		leave();
		return jre->buffer;
	}

	jre = string_copy(result);
	debug("Setting jre to %s", jre->buffer);
	debug("JAVA_HOME appears to be a JRE: '%s'", jre->buffer);
	leave();
	return jre->buffer;
}

char *discover_system_java_home(void)
{
	enter("discover_system_java_home");
#ifdef WIN32
	HKEY key;
	HRESULT result;
	const char *key_root = "SOFTWARE\\JavaSoft\\Java Development Kit";
	struct string *string;
	char buffer[PATH_MAX];
	DWORD valuelen = sizeof(buffer);

	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, key_root, 0, KEY_READ, &key);
	if (ERROR_SUCCESS != result) {
		leave();
		return NULL;
	}
	result = RegQueryValueEx(key, "CurrentVersion", NULL, NULL, (LPBYTE)buffer, &valuelen);
	RegCloseKey(key);
	if (ERROR_SUCCESS != result) {
		error(get_win_error());
		leave();
		return NULL;
	}
	string = string_initf("%s\\%s", key_root, buffer);
	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, string->buffer, 0, KEY_READ, &key);
	if (ERROR_SUCCESS != result) {
		leave();
		return NULL;
	}
	valuelen = sizeof(buffer);
	result = RegQueryValueEx(key, "JavaHome", NULL, NULL, (LPBYTE)buffer, &valuelen);
	RegCloseKey(key);
	if (ERROR_SUCCESS != result) {
		leave();
		return NULL;
	}
	leave();
	return strdup(buffer);
#else
	const char *java_executable = find_in_path(get_java_command(), 0);

#ifdef __APPLE__
	if (!java_executable) {
		leave();
		return NULL;
	}

	int len = strlen(java_executable);
	if (len > 14 && !suffixcmp(java_executable, len, "/Commands/java")) {
		/*
		 * Containing folder is not a JRE or JDK, it's an Apple Framework. Bah!
		 * For example, the path:
		 *
		 *     /System/Library/Frameworks/JavaVM.framework/Versions/A/Commands
		 *
		 * does not actually contain a proper JRE. It is merely a Framework
		 * for the current version of Apple Java on the system.
		 *
		 * Unfortunately, on OS X, /usr/bin/java is typically symlinked to
		 * /System/Library/Frameworks/JavaVM.framework/Versions/Current/Commands/java,
		 * with Current symlinked to A, resulting in a failure of this PATH-based
		 * strategy to find the actual JRE. So we simply give up in that case.
		 */
		debug("Ignoring Apple Framework java executable: '%s'", java_executable);
		debug("discover_system_java_home: Returning NULL");
		leave();
		return NULL;
	}
#endif

	if (java_executable) {
		char *path = strdup(java_executable);
		const char *suffixes[] = {
			"java", "\\", "/", "bin", "\\", "/", NULL
		};
		int len = strlen(path), i;
		for (i = 0; suffixes[i]; i++)
			if (!suffixcmp(path, len, suffixes[i])) {
				len -= strlen(suffixes[i]);
				path[len] = '\0';
			}
		debug("Returning %s", path);
		leave();
		return path;
	}
	debug("Returning NULL");
	leave();
	return NULL;
#endif
}

void set_legacy_jre_path(const char *path)
{
	enter("set_legacy_jre_path");
	if (!legacy_jre_path)
		legacy_jre_path = string_init(32);
	string_set(legacy_jre_path, is_absolute_path(path) ? path : ij_path(path));
	debug("Using JRE from ImageJ.cfg: %s", legacy_jre_path->buffer);
	leave();
}

const char *get_default_library_path(void)
{
	if (!default_library_path)
	{
		initialize_java_home_and_library_path();
	}
	return default_library_path;
}

/*
 * Searches for a bundled platform-specific java, updating relative_java_home and the (default_)library_path if found.
 */
void *initialize_java_home_and_library_path(void)
{
	enter("initialize_java_home_and_library_path");

	struct string *bundled_dir;

	// Identify the platform-specific subdirectory in /java to search for a bundled JVM
	bundled_dir = string_copy(ij_path("java/"));
	string_append(bundled_dir, 
#if defined(__APPLE__)
		"macosx/"
#elif defined(WIN32)
		sizeof(void *) < 8 ? "win32/" : "win64/"
#else
		sizeof(void *) < 8 ? "linux/" : "linux-amd64/"
#endif
		);

	// Search the platform-specific subdirectory for a Java installation.
	find_java_library_path(bundled_dir);

	// Clean up
	string_release(bundled_dir);

	leave();
}

/**
 * Search for a java installation beneath the given directory.
 * This will update relative_java_home and (default_)library_path,
 * and will short-circuit once these are found.
 */
void find_java_library_path(struct string *dir)
{
	enter("find_java_library_path");
	debug("dir = %s", dir->buffer);

	int i;
	int arrayLength = sizeof(default_library_paths)/sizeof(default_library_paths[0]);
	for (i=0; i<arrayLength; i++)
		search_for_java(dir, default_library_paths[i]);

	leave();
}

/*
 * Recursively searches dir and the level below for java_library_path.
 * If found:
 *  the (default_)library_path(s) are set to java_library_path
 *  relative_java_home is set to the directory containing java_library_path, with the ij_path popped off.
 */
void search_for_java(struct string *dir, const char *java_library_path)
{
	if (default_library_path) return; // already found
	enter("search_for_java");
	debug("dir = %s", dir->buffer);
	debug("java_library_path = %s", java_library_path);

	int depth = 2;
	struct string *search_path, *result;
	result = string_init(32);
	search_path = string_initf("%s", java_library_path);
	find_newest(dir, depth, search_path->buffer, result);
	debug("find_newest complete with result: '%s'", result->buffer);
	if (result->length) {
		// Found a hit
		// Need to subtract off the ij_path to get the relative_path
		struct string *ij_base_dir = string_initf("%s", ij_path(""));
		int ij_dir_len = ij_base_dir->length;
		string_release(ij_base_dir);
		// Append a path separator if needed
		if (result->buffer[result->length - 1] != '/') {
			string_add_char(result, '/');
		}
		set_relative_java_home(xstrdup(result->buffer + ij_dir_len));
		set_library_path(java_library_path);
		default_library_path = java_library_path;
		debug("Default library path (relative): %s", java_library_path);
	}
	string_release(search_path);
	string_release(result);
	leave();
}

void set_library_path(const char *path)
{
	enter("set_library_path");
	library_path = path;
	debug("library_path is now %s", library_path);
	leave();
}

const char *get_library_path(void)
{
	return library_path;
}

void add_java_home_to_path(void)
{
	const char *java_home = get_java_home();
	struct string *new_path = string_init(32), *buffer;
	const char *env;

	if (!java_home)
		return;
	buffer = string_initf("%s/bin", java_home);
	if (dir_exists(buffer->buffer))
		string_append_path_list(new_path, buffer->buffer);
	string_setf(buffer, "%s/jre/bin", java_home);
	if (dir_exists(buffer->buffer))
		string_append_path_list(new_path, buffer->buffer);

	env = getenv("PATH");
	string_append_path_list(new_path, env ? env : get_ij_dir());
	setenv_or_exit("PATH", new_path->buffer, 1);
	string_release(buffer);
	string_release(new_path);
}
