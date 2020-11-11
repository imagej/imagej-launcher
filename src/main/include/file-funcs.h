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
#ifndef FILE_FUNCS_H
#define FILE_FUNCS_H

#include "string-funcs.h"

extern int file_is_newer(const char *path, const char *than);
extern int find_file(struct string *search_root, int max_depth, const char *file, struct string *result);
extern void detect_library_path(struct string *library_path, struct string *directory);

extern const char *last_slash(const char *path);
extern void follow_symlinks(struct string *path, int max_recursion);
extern const char *make_absolute_path(const char *path);
extern int is_absolute_path(const char *path);
extern int file_exists(const char *path);
extern int dir_exists(const char *path);
extern const char *find_in_path(const char *path, int die_if_not_found);
extern int read_exactly(int fd, unsigned char *buffer, int size);
extern void read_file_as_string(const char *file_name, struct string *contents);

extern int mkdir_recursively(struct string *buffer);
extern int mkdir_p(const char *path);
extern char *find_jar(const char *jars_directory, const char *prefix);
extern int has_jar(const char *jars_directory, const char *prefix);

extern const char *get_ij_dir(void);
extern void set_ij_dir(const char *path);
extern void infer_ij_dir(const char *main_argv0);
extern const char *ij_path(const char *relative_path);

void find_newest(struct string *relative_path, int max_depth, const char *file, struct string *result);

#endif
