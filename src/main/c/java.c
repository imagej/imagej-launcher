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
#include "java.h"
#include "file-funcs.h"
#include "platform.h"
#include "string-funcs.h"

/*
 * If set, overrides the environment variable JAVA_HOME, which in turn
 * overrides relative_java_home.
 */
static const char *absolute_java_home;
static const char *relative_java_home;
static const char *library_path;
static const char *default_library_path;
#if defined(__APPLE__)
static const char *default_library_paths[1] = {"Contents/Home/lib/server/libjvm.dylib"};
#elif defined(WIN32)
static const char *default_library_paths[2] = {"bin/client/jvm.dll", "bin/server/jvm.dll"};
#else
static const char *default_library_paths[3] = {"lib/i386/client/libjvm.so", "lib/server/libjvm.so", "lib/amd64/server/libjvm.so"};
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


unsigned int guess_java_version(void)
{
	if (debug) error("guess_java_version: Entering");
	const char *java_home = get_jre_home();
	return guess_java_version_for_path(java_home);
}

unsigned int guess_java_version_for_path(const char *java_home)
{
	if (debug) error("guess_java_version_for_path: Entering with %s", java_home);

	while (java_home && *java_home) {
		// Known naming conventions include:
		// - X.Y.Z.jdk
		// - jdkX.Y.Z_b
		// - jdkX.Y.Z_b.jdk
		// - jdk-X.Y.Z
		// - jdk-X
		// - java-X
		// - openjdk-X
		// - adoptopenjdk-X.jdk
		if (!prefixcmp(java_home, "jdk") ||
		    !prefixcmp(java_home, "jre") ||
		    !prefixcmp(java_home, "java") ||
		    !prefixcmp(java_home, "openjdk") ||
		    !prefixcmp(java_home, "adoptopenjdk") ||
		    !prefixcmp(java_home, "1.")) {
			unsigned int result = 0;
			const char *p = java_home;
			// Skip to next number, to account for variety in naming conventions
			while ((*p < '0' || *p > '9') && *p != '\0') p++;

			p = parse_number(p, &result, 24);
			if (p) {
				// parsing first number was successful
				if (p && *p == '.')
					p = parse_number(p + 1, &result, 16);
				if (p && *p == '.')
					p = parse_number(p + 1, &result, 8);
				if (p && *p == '_')
					p = parse_number(p + 1, &result, 0);
				if (debug) error("guess_java_version: Returning %d", result);
				return result;
			}
		}
		// Folder is unlikely to be a Java home; move deeper
		java_home += strcspn(java_home, "\\/") + 1;
	}
	if (debug) error("guess_java_version_for_path: Returning 0");
	return 0;
}

void set_java_home(const char *absolute_path)
{
	if (debug) error("set_java_home: Entering with %s", absolute_path);
	absolute_java_home = absolute_path;
	if (debug) error("set_java_home: absolute_java_home is now: %s", absolute_java_home);
}

void set_relative_java_home(const char *relative_path)
{
	if (debug) error("set_relative_java_home: Entering with %s", relative_path);
	relative_java_home = relative_path;
	if (debug) error("set_relative_java_home: relative_java_home is now: %s", relative_java_home);
}

int is_jre_home(const char *directory)
{
	if (debug) error("is_jre_home: Entering with %s", directory);
	int i;
	int result = 0;
	if (dir_exists(directory)) {
		// Check if one of default_library_paths exists
		int arrayLength = sizeof(default_library_paths)/sizeof(default_library_paths[0]);
		for (i=0; i<arrayLength; i++) {
			struct string* libjvm = string_initf("%s/%s", directory, default_library_paths[i]);
			if (!file_exists(libjvm->buffer)) {
				if (debug)
					error("Ignoring JAVA_HOME (does not exist): %s", libjvm->buffer);
			}
			else if (!is_native_library(libjvm->buffer)) {
				if (debug)
					error("Ignoring JAVA_HOME (wrong arch): %s", libjvm->buffer);
			}
			else {
				result = 1;
				break;
			}
			string_release(libjvm);
		}
	}
	return result;
}

/**
 * Checks if a directory is a java home directory by calling is_jre_home
 * on <directory>/jre and <directory>.
 */
int is_java_home(const char *directory)
{
	if (debug) error("is_java_home: Entering with %s", directory);
	struct string *jre = string_initf("%s/jre", directory);
	int result = is_jre_home(jre->buffer);
	if (!result) {
		// Java9 does not have a jre subfolder -> check directory
		string_set(jre, directory);
		result = is_jre_home(jre->buffer);
	}
	string_release(jre);
	return result;
}

const char *get_java_home_env(void)
{
	if (debug) error("get_java_home_env: Entering");
	const char *env = getenv("JAVA_HOME");
	if (debug) error("get_java_home_env: JAVA_HOME is set to %s", env);
	if (env && is_java_home(env))
		return env;
	return NULL;
}

const char *get_java_home(void)
{
	if (debug) error("get_java_home: Entering");
	const char *result;

	/* Check if an absolute path has been previously set */
	if (absolute_java_home) {
		if (debug) error("Using absolute_java_home: %s", absolute_java_home);
		return absolute_java_home;
	}

	/* Check if a relative path has been previously set */
	result = !relative_java_home ? NULL : ij_path(relative_java_home);
	if (debug) error("get_java_home: Trying to use relative_java_home: %s", result);
	if (result && is_java_home(result)) {
		if (debug) error("get_java_home: Returning %s", result);
		return result;
	}
	if (result && (!suffixcmp(result, -1, "/jre") ||
			 !suffixcmp(result, -1, "/jre/")) &&
			is_jre_home(result)) {
		/* Strip jre/ from result */
		char *new_eol = (char *)(result + strlen(result) - 4);
		*new_eol = '\0';
		if (debug) error("get_java_home: Returning %s", result);
		return result;
	}

	/* Check JAVA_HOME environment variable */
	result = get_java_home_env();
	if (result) {
		if (debug) error("get_java_home: Returning %s", result);
		return result;
	}

	/* Otherwise use the system's Java */
	if (debug) error("get_java_home: Returning discover_system_java_home()");
	return discover_system_java_home();
}

/* Returns the JRE/JAVA HOME folder that will be used */
const char *get_jre_home(void)
{
	if (debug) error("get_jre_home: Entering");
	const char *result;
	int len;
	static struct string *jre;
	static int initialized;

	if (jre) {
		if (debug) error("get_jre_home: Returning %s", jre->buffer);
		return jre->buffer;
	}	

	if (initialized) {
		if (debug) error("get_jre_home: Returning NULL");
		return NULL;
	}	
	initialized = 1;

	/* ImageJ 1.x ships the JRE in <ij.dir>/jre/ */
	result = legacy_jre_path ? legacy_jre_path->buffer : ij_path("jre");
	if (dir_exists(result)) {
		struct string *libjvm = string_initf("%s/%s", result, default_library_path);
		if (!file_exists(libjvm->buffer)) {
			if (debug)
				error("Invalid jre/: '%s' does not exist!",
						libjvm->buffer);
		}
		else if (!is_native_library(libjvm->buffer)) {
			if (debug)
				error("Invalid jre/: '%s' is not a %s library!",
						libjvm->buffer, get_platform());
		}
		else {
			string_release(libjvm);
			jre = string_initf("%s", result);
			if (debug)
				error("JRE found in '%s'", jre->buffer);
			return jre->buffer;
		}
		string_release(libjvm);
	}
	else {
		if (debug)
			error("JRE not found in '%s'", result);
	}

	result = get_java_home();
	if (!result) {
		const char *jre_home = getenv("JRE_HOME");
		if (jre_home && *jre_home && is_jre_home(jre_home)) {
			jre = string_copy(jre_home);
			if (debug) {
				error("get_jre_home: Setting jre to %s", jre->buffer);
				error("Found a JRE in JRE_HOME: %s", jre->buffer);
			}
			return jre->buffer;
		}
		jre_home = getenv("JAVA_HOME");
		if (jre_home && *jre_home && is_jre_home(jre_home)) {
			jre = string_copy(jre_home);
			if (debug) {
				error("get_jre_home: Setting jre to %s", jre->buffer);
				error("Found a JRE in JAVA_HOME: %s", jre->buffer);
			}
			return jre->buffer;
		}
		if (debug)
			error("No JRE was found in default locations");
		return NULL;
	}

	len = strlen(result);
	if (len > 4 && !suffixcmp(result, len, "/jre")) {
		jre = string_copy(result);
		if (debug) {
			error("get_jre_home: Setting jre to %s", jre->buffer);
			error("JAVA_HOME points to a JRE: '%s'", result);
		}
		return jre->buffer;
	}

	jre = string_initf("%s/jre", result);
	if (debug) error("get_jre_home: Setting jre to %s", jre->buffer);
	if (dir_exists(jre->buffer)) {
		if (debug)
			error("JAVA_HOME contains a JRE: '%s'", jre->buffer);
		return jre->buffer;
	}
	string_set(jre, result);
	if (debug) {
		error("get_jre_home: Setting jre to %s", jre->buffer);
		error("JAVA_HOME appears to be a JRE: '%s'", jre->buffer);
	}
	return jre->buffer;
}

char *discover_system_java_home(void)
{
	if (debug) error("discover_system_java_home: Entering");
#ifdef WIN32
	HKEY key;
	HRESULT result;
	const char *key_root = "SOFTWARE\\JavaSoft\\Java Development Kit";
	struct string *string;
	char buffer[PATH_MAX];
	DWORD valuelen = sizeof(buffer);

	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, key_root, 0, KEY_READ, &key);
	if (ERROR_SUCCESS != result)
		return NULL;
	result = RegQueryValueEx(key, "CurrentVersion", NULL, NULL, (LPBYTE)buffer, &valuelen);
	RegCloseKey(key);
	if (ERROR_SUCCESS != result)
{ error(get_win_error());
		return NULL;
}
	string = string_initf("%s\\%s", key_root, buffer);
	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, string->buffer, 0, KEY_READ, &key);
	if (ERROR_SUCCESS != result)
		return NULL;
	valuelen = sizeof(buffer);
	result = RegQueryValueEx(key, "JavaHome", NULL, NULL, (LPBYTE)buffer, &valuelen);
	RegCloseKey(key);
	if (ERROR_SUCCESS != result)
		return NULL;
	return strdup(buffer);
#else
	const char *java_executable = find_in_path(get_java_command(), 0);

#ifdef __APPLE__
	if (!java_executable) return NULL;

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
		if (debug) 
			error("Ignoring Apple Framework java executable: '%s'", java_executable);
		if (debug) error("discover_system_java_home: Returning NULL");
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
		if (debug) error("discover_system_java_home: Returning %s", path);
		return path;
	}
	if (debug) error("discover_system_java_home: Returning NULL");
	return NULL;
#endif
}

void set_legacy_jre_path(const char *path)
{
	if (!legacy_jre_path)
		legacy_jre_path = string_init(32);
	string_set(legacy_jre_path, is_absolute_path(path) ? path : ij_path(path));
	if (debug)
		error("Using JRE from ImageJ.cfg: %s", legacy_jre_path->buffer);
}

void set_default_library_path(void)
{
	default_library_path =
#if defined(__APPLE__)
		"Contents/Home/lib/server/libjvm.dylib";
#elif defined(WIN32)
		sizeof(void *) < 8 ? "bin/client/jvm.dll" : "bin/server/jvm.dll";
#else
		sizeof(void *) < 8 ? "lib/i386/client/libjvm.so" : (guess_java_version() >= 0x09000000 ? "lib/server/libjvm.so" : "lib/amd64/server/libjvm.so");
#endif
}

const char *get_default_library_path(void)
{
	return default_library_path;
}

void set_library_path(const char *path)
{
	if (debug) error("set_library_path: Entering with %s", path);
	library_path = path;
	if (debug) error("library_path is now %s", library_path);
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
