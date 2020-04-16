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
#ifndef STRING_FUNCS_H
#define STRING_FUNCS_H

#include <stdarg.h>
#include <stdlib.h>

/**
 * String functions.
 *
 * Just like every other C project, we got to have our own string functions
 * including our own growable string buffer (the "string" struct).
 *
 * While these functions were heavily inspired by Git's strbuf, the source
 * code was written from scratch.
 *
 * @author Johannes Schindelin
 */

struct string {
	int alloc, length;
	char *buffer;
};

extern void string_ensure_alloc(struct string *string, int length);
extern void string_set_length(struct string *string, int length);
extern void string_set(struct string *string, const char *buffer);
extern struct string *string_init(int length);
extern struct string *string_copy(const char *string);
extern void string_release(struct string *string);
extern void string_add_char(struct string *string, char c);
extern void string_append(struct string *string, const char *append);
extern int path_list_contains(const char *list, const char *path);
extern void string_append_path_list(struct string *string, const char *append);
extern void string_append_at_most(struct string *string, const char *append, int length);
extern void string_replace_range(struct string *string, int start, int end, const char *replacement);
extern int number_length(unsigned long number, long base);
extern inline int is_digit(char c);
extern void string_vaddf(struct string *string, const char *fmt, va_list ap);
__attribute__((format (printf, 2, 3)))
extern void string_addf(struct string *string, const char *fmt, ...);
__attribute__((format (printf, 2, 3)))
extern void string_setf(struct string *string, const char *fmt, ...);
__attribute__((format (printf, 1, 2)))
extern struct string *string_initf(const char *fmt, ...);
extern void string_replace(struct string *string, char from, char to);
extern int string_read_file(struct string *string, const char *path);
extern void string_escape(struct string *string, const char *characters);

struct string_array {
	char **list;
	int nr, alloc;
};

extern void append_string(struct string_array *array, char *str);
extern void prepend_string(struct string_array *array, char *str);
extern void prepend_string_copy(struct string_array *array, const char *str);
extern void append_string_array(struct string_array *target, struct string_array *source);
extern void prepend_string_array(struct string_array *target, struct string_array *source);

extern size_t mystrlcpy(char *dest, const char *src, size_t size);

extern int count_leading_whitespace(const char *line);
extern int is_end_of_line(char ch);

#endif
