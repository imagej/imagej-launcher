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
#include "file-funcs.h"
#include "platform.h"
#include "xalloc.h"

#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>

int find_file(struct string *search_root, int max_depth, const char *file, struct string *result)
{
	int len = search_root->length;
	DIR *directory;
	struct dirent *entry;

	string_add_char(search_root, '/');

	string_append(search_root, file);
	if (file_exists(search_root->buffer)) {
		string_set(result, search_root->buffer);
		string_set_length(search_root, len);
		return 1;
	}

	if (max_depth <= 0)
		return 0;

	string_set_length(search_root, len);
	directory = opendir(search_root->buffer);
	if (!directory)
		return 0;
	string_add_char(search_root, '/');
	while (NULL != (entry = readdir(directory))) {
		if (entry->d_name[0] == '.')
			continue;
		string_append(search_root, entry->d_name);
		if (dir_exists(search_root->buffer))
			if (find_file(search_root, max_depth - 1, file, result)) {
				string_set_length(search_root, len);
				closedir(directory);
				return 1;
			}
		string_set_length(search_root, len + 1);
	}
	closedir(directory);
	string_set_length(search_root, len);
	return 0;
}

void detect_library_path(struct string *library_path, struct string *directory)
{
	int original_length = directory->length;
	char found = 0;
	DIR *dir = opendir(directory->buffer);
	struct dirent *entry;

	if (!dir)
		return;

	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		string_addf(directory, "/%s", entry->d_name);
		if (dir_exists(directory->buffer))
			detect_library_path(library_path, directory);
		else if (!found && is_native_library(directory->buffer)) {
			string_set_length(directory, original_length);
			string_append_path_list(library_path, directory->buffer);
			found = 1;
			continue;
		}
		string_set_length(directory, original_length);
	}
	closedir(dir);
}

const char *last_slash(const char *path)
{
	const char *slash = strrchr(path, '/');
#ifdef WIN32
	const char *backslash = strrchr(path, '\\');

	if (backslash && slash < backslash)
		slash = backslash;
#endif
	return slash;
}

void follow_symlinks(struct string *path, int max_recursion)
{
#ifndef WIN32
	char buffer[PATH_MAX];
	int count = readlink(path->buffer, buffer, sizeof(buffer) - 1);
	if (count < 0)
		return;
	string_set_length(path, 0);
	string_addf(path, "%.*s", count, buffer);
	if (max_recursion > 0)
		follow_symlinks(path, max_recursion - 1);
#endif
}

const char *make_absolute_path(const char *path)
{
	static char bufs[2][PATH_MAX + 1], *buf = bufs[0];
	char cwd[PATH_MAX] = "";
#ifndef WIN32
	static char *next_buf = bufs[1];
	int buf_index = 1, len;
#endif

	int depth = 20;
	char *last_elem = NULL;
	struct stat st;

	if (mystrlcpy(buf, path, PATH_MAX) >= PATH_MAX)
		die("Too long path: %s", path);

	while (depth--) {
		if (stat(buf, &st) || !S_ISDIR(st.st_mode)) {
			const char *slash = last_slash(buf);
			if (slash) {
				buf[slash-buf] = '\0';
				last_elem = xstrdup(slash + 1);
			} else {
				last_elem = xstrdup(buf);
				*buf = '\0';
			}
		}

		if (*buf) {
			if (!*cwd && !getcwd(cwd, sizeof(cwd)))
				die("Could not get current working dir");

			if (chdir(buf))
				die("Could not switch to %s", buf);
		}
		if (!getcwd(buf, PATH_MAX))
			die("Could not get current working directory");

		if (last_elem) {
			int len = strlen(buf);
			if (len + strlen(last_elem) + 2 > PATH_MAX)
				die("Too long path name: %s/%s", buf, last_elem);
			buf[len] = '/';
			strcpy(buf + len + 1, last_elem);
			free(last_elem);
			last_elem = NULL;
		}

#ifndef WIN32
		if (!lstat(buf, &st) && S_ISLNK(st.st_mode)) {
			len = readlink(buf, next_buf, PATH_MAX);
			if (len < 0)
				die("Invalid symlink: %s", buf);
			next_buf[len] = '\0';
			buf = next_buf;
			buf_index = 1 - buf_index;
			next_buf = bufs[buf_index];
		} else
#endif
			break;
	}

	if (*cwd && chdir(cwd))
		die("Could not change back to %s", cwd);

	return buf;
}

int is_absolute_path(const char *path)
{
#ifdef WIN32
	if (((path[0] >= 'A' && path[0] <= 'Z') ||
			(path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
		return 1;
#endif
	return path[0] == '/';
}

int file_exists(const char *path)
{
	return !access(path, R_OK);
}

int dir_exists(const char *path)
{
	DIR *dir = opendir(path);
	if (dir) {
		closedir(dir);
		return 1;
	}
	return 0;
}

const char *find_in_path(const char *path, int die_if_not_found)
{
	const char *p = getenv("PATH");
	struct string *buffer;

#ifdef WIN32
	int len = strlen(path);
	struct string *path_with_suffix = NULL;
	const char *in_cwd;

	if (suffixcmp(path, len, ".exe") && suffixcmp(path, len, ".EXE")) {
		path_with_suffix = string_initf("%s.exe", path);
		path = path_with_suffix->buffer;
	}
	in_cwd = make_absolute_path(path);
	if (file_exists(in_cwd)) {
		string_release(path_with_suffix);
		return in_cwd;
	}
#endif

	if (!p) {
		if (die_if_not_found)
			die("Could not get PATH");
		debug("Could not get PATH");
		return NULL;
	}

	buffer = string_init(32);
	for (;;) {
		const char *colon = strchr(p, PATH_SEP[0]), *orig_p = p;
		int len = colon ? colon - p : strlen(p);
		struct stat st;

		if (!len) {
			if (colon) {
				// ignore double colon
				p++;
				continue;
			}
			if (die_if_not_found)
				die("Could not find %s in PATH", path);
			debug("Could not find '%s' in the PATH", path);
			return NULL;
		}

		p += len + !!colon;
		if (!is_absolute_path(orig_p))
			continue;
		string_setf(buffer, "%.*s/%s", len, orig_p, path);
#ifdef WIN32
#define S_IX S_IXUSR
#else
#define S_IX (S_IXUSR | S_IXGRP | S_IXOTH)
#endif
		if (!stat(buffer->buffer, &st) && S_ISREG(st.st_mode) &&
				(st.st_mode & S_IX)) {
			const char *result = make_absolute_path(buffer->buffer);
			string_release(buffer);
#ifdef WIN32
			string_release(path_with_suffix);
#endif
			return result;
		}
	}
}

int read_exactly(int fd, unsigned char *buffer, int size)
{
	while (size > 0) {
		int count = read(fd, buffer, size);
		if (count < 0)
			return 0;
		if (count == 0)
			/* short file */
			return 1;
		buffer += count;
		size -= count;
	}
	return 1;
}

int mkdir_recursively(struct string *buffer)
{
	int slash = buffer->length - 1, save_length;
	char save_char;
	while (slash > 0 && !is_slash(buffer->buffer[slash]))
		slash--;
	while (slash > 0 && is_slash(buffer->buffer[slash - 1]))
		slash--;
	if (slash <= 0)
		return -1;
	save_char = buffer->buffer[slash];
	save_length = buffer->length;
	buffer->buffer[slash] = '\0';
	buffer->length = slash;
	if (!dir_exists(buffer->buffer)) {
		int result = mkdir_recursively(buffer);
		if (result)
			return result;
	}
	buffer->buffer[slash] = save_char;
	buffer->length = save_length;
	return mkdir(buffer->buffer, 0777);
}

/*
   Ensures that a directory exists in the manner of "mkdir -p", creating
   components with file mode 777 (& umask) where they do not exist.
   Returns 0 on success, or the return code of mkdir in the case of
   failure.
*/
int mkdir_p(const char *path)
{
	int result;
	struct string *buffer;
	if (dir_exists(path))
		return 0;

	buffer = string_copy(path);
	result = mkdir_recursively(buffer);
	string_release(buffer);
	return result;
}

char *find_jar(const char *jars_directory, const char *prefix)
{
	int prefix_length = strlen(prefix);
	struct string *buffer;
	int length;
	time_t mtime = 0;
	DIR *directory = opendir(jars_directory);
	struct dirent *entry;
	struct stat st;
	char *result = NULL;

	if (directory == NULL)
		return NULL;

	buffer = string_initf("%s", jars_directory);
	length = buffer->length;
	if (length == 0 || buffer->buffer[length - 1] != '/') {
		string_add_char(buffer, '/');
		length++;
	}
	while ((entry = readdir(directory))) {
		const char *name = entry->d_name;
		if (prefixcmp(name, prefix) ||
			(strcmp(name + prefix_length, ".jar") &&
			 (name[prefix_length] != '-' ||
			  !isdigit(name[prefix_length + 1]) ||
			  suffixcmp(name + prefix_length + 2, -1, ".jar"))))
			continue;
		string_set_length(buffer, length);
		string_append(buffer, name);
		if (!stat(buffer->buffer, &st) && st.st_mtime > mtime) {
			free(result);
			result = strdup(buffer->buffer);
			mtime = st.st_mtime;
		}
	}
	closedir(directory);
	string_release(buffer);
	return result;
}

int has_jar(const char *jars_directory, const char *prefix)
{
	char *result = find_jar(jars_directory, prefix);

	if (!result)
		return 0;
	free(result);
	return 1;
}

/* The path to the top-level ImageJ.app/ directory */

static const char *ij_dir;

void set_ij_dir(const char *path)
{
	ij_dir = path;
}

const char *get_ij_dir(void)
{
	return ij_dir;
}

void infer_ij_dir(const char *argv0)
{
	static const char *buffer;
	const char *slash;
	int len;

	if (buffer)
		return;

	if (!last_slash(argv0))
		buffer = find_in_path(argv0, 1);
	else
		buffer = make_absolute_path(argv0);
	argv0 = buffer;

	slash = last_slash(argv0);
	if (!slash)
		die("Could not get absolute path for executable");

	len = slash - argv0;
#ifdef __APPLE__
	if (!suffixcmp(argv0, len, "/Contents/MacOS")) {
		struct string *scratch;
		len -= strlen("/Contents/MacOS");
		scratch = string_initf("%.*s/jars", len, argv0);
		if (len && !dir_exists(scratch->buffer))
			while (--len && argv0[len] != '/')
				; /* ignore */
		slash = argv0 + len;
		string_release(scratch);
	}
#endif

	buffer = xstrndup(buffer, slash - argv0);
#ifdef WIN32
	buffer = dos_path(buffer);
#endif
	ij_dir = buffer;
}

const char *ij_path(const char *relative_path)
{
	static struct string *string[3];
	static int counter;

	counter = ((counter + 1) % (sizeof(string) / sizeof(string[0])));
	if (!string[counter])
		string[counter] = string_initf("%s%s%s", ij_dir,
			is_slash(*relative_path) ? "" : "/", relative_path);
	else
		string_setf(string[counter], "%s%s%s", ij_dir,
			is_slash(*relative_path) ? "" : "/", relative_path);
	return string[counter]->buffer;
}

void read_file_as_string(const char *file_name, struct string *contents)
{
	char buffer[1024];
	FILE *in = fopen(file_name, "r");

	string_set_length(contents, 0);
	if (!in)
		return;

	while (!feof(in)) {
		int count = fread(buffer, 1, sizeof(buffer), in);
		string_append_at_most(contents, buffer, count);
	}
	fclose(in);
}

/*
 * Recursively traverses subfolders of a path up to a defined depth to find a
 * file. If multiple candidates are found, the newest will be returned.
 *
 * @param path - base directory to start search
 * @param max_depth - maximum subfolder distance to travel from base
 * @param file - the file we are looking for
 * @param result - reference to path where the file is found
 */
void find_newest(struct string *path, int max_depth, const char *file, struct string *result)
{
	enter("find_newest");

	// NB: we temporarily combine the file and path, this allows resetting the path.
	int len = path->length;
	DIR *directory;
	struct dirent *entry;

	debug("searching '%s' for '%s'", path->buffer, file);

	// Update the current path
	if (!len || path->buffer[len - 1] != '/')
		string_add_char(path, '/');
	string_append(path, file);

	// Check if the file exists and, if so, it is the newest candidate
	if (file_exists(path->buffer)) {
		if (is_native_library(path->buffer)) {
			string_set_length(path, len);
			if (!result->length) {
				debug("found a candidate: '%s'", path->buffer);
				string_set(result, path->buffer);
			}
			else if (file_is_newer(path->buffer, result->buffer)) {
				debug("found newer candidate: '%s'", path->buffer);
				string_set(result, path->buffer);
			}
			else debug("rejected older candidate: '%s'", path->buffer);
		}
		else debug("not a native library: '%s'", path->buffer);
	}
	else debug("file not found: '%s'", path->buffer);

	// Recursive end
	if (max_depth <= 0) {
		leave();
		return;
	}

	string_set_length(path, len);
	directory = opendir(path->buffer);
	if (!directory) {
		leave();
		return;
	}

	if (path->buffer[path->length - 1] != '/') {
		string_add_char(path, '/');
	}

	// Recursive step - descend to each subdirectory
	while (NULL != (entry = readdir(directory))) {
		if (entry->d_name[0] == '.')
			continue;
		string_append(path, entry->d_name);
		if (dir_exists(path->buffer))
			find_newest(path, max_depth - 1, file, result);
		string_set_length(path, len + 1);
	}
	closedir(directory);

	// Reset the search path
	string_set_length(path, len);

	leave();
}
