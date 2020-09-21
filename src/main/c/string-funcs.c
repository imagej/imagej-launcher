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
#include <string.h>

#include "string-funcs.h"
#include "xalloc.h"
#include "common.h"

void string_ensure_alloc(struct string *string, int length)
{
	if (string->alloc <= length) {
		char *new_buffer = xrealloc(string->buffer, length + 1);

		string->buffer = new_buffer;
		string->alloc = length;
	}
}

void string_set_length(struct string *string, int length)
{
	if (length == string->length)
		return;
	if (length > string->length)
		die("Cannot enlarge strings");
	string->length = length;
	string->buffer[length] = '\0';
}

void string_set(struct string *string, const char *buffer)
{
	free(string->buffer);
	string->buffer = xstrdup(buffer);
	string->alloc = string->length = strlen(buffer);
}

struct string *string_init(int length)
{
	struct string *string = xcalloc(sizeof(struct string), 1);

	string_ensure_alloc(string, length);
	string->buffer[0] = '\0';
	return string;
}

struct string *string_copy(const char *string)
{
	int len = strlen(string);
	struct string *result = string_init(len);

	memcpy(result->buffer, string, len + 1);
	result->length = len;

	return result;
}

void string_release(struct string *string)
{
	if (string) {
		free(string->buffer);
		free(string);
	}
}

void string_add_char(struct string *string, char c)
{
	if (string->alloc == string->length)
		string_ensure_alloc(string, 3 * (string->alloc + 16) / 2);
	string->buffer[string->length++] = c;
	string->buffer[string->length] = '\0';
}

void string_append(struct string *string, const char *append)
{
	int len = strlen(append);

	string_ensure_alloc(string, string->length + len);
	memcpy(string->buffer + string->length, append, len + 1);
	string->length += len;
}

int path_list_contains(const char *list, const char *path)
{
	size_t len = strlen(path);
	const char *p = list;
	while (p && *p) {
		if (!strncmp(p, path, len) &&
				(p[len] == PATH_SEP[0] || !p[len]))
			return 1;
		p = strchr(p, PATH_SEP[0]);
		if (!p)
			break;
		p++;
	}
	return 0;
}

void string_append_path_list(struct string *string, const char *append)
{
	if (!append || path_list_contains(string->buffer, append))
		return;

	if (string->length)
		string_append(string, PATH_SEP);
	string_append(string, append);
}

void string_append_at_most(struct string *string, const char *append, int length)
{
	int len = strlen(append);

	if (len > length)
		len = length;

	string_ensure_alloc(string, string->length + len);
	memcpy(string->buffer + string->length, append, len + 1);
	string->length += len;
	string->buffer[string->length] = '\0';
}

void string_replace_range(struct string *string, int start, int end, const char *replacement)
{
	int length = strlen(replacement);
	int total_length = string->length + length - (end - start);

	if (end != start + length) {
		string_ensure_alloc(string, total_length);
		if (string->length > end)
			memmove(string->buffer + start + length, string->buffer + end, string->length - end);
	}

	if (length)
		memcpy(string->buffer + start, replacement, length);
	string->buffer[total_length] = '\0';
	string->length = total_length;
}

int number_length(unsigned long number, long base)
{
        int length = 1;
        while (number >= base) {
                number /= base;
                length++;
        }
        return length;
}

inline int is_digit(char c)
{
	return c >= '0' && c <= '9';
}

void string_vaddf(struct string *string, const char *fmt, va_list ap)
{
	while (*fmt) {
		char fill = '\0';
		int size = -1, max_size = -1;
		char *p = (char *)fmt;

		if (*p != '%' || *(++p) == '%') {
			string_add_char(string, *p++);
			fmt = p;
			continue;
		}
		if (*p == ' ' || *p == '0')
			fill = *p++;
		if (is_digit(*p))
			size = (int)strtol(p, &p, 10);
		else if (p[0] == '.' && p[1] == '*') {
			max_size = va_arg(ap, int);
			p += 2;
		}
		switch (*p) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			if (fill) {
				int len = size - strlen(s);
				while (len-- > 0)
					string_add_char(string, fill);
			}
			while (*s && max_size--)
				string_add_char(string, *s++);
			break;
		}
		case 'c':
			{
				char c = va_arg(ap, int);
				string_add_char(string, c);
			}
			break;
		case 'u':
		case 'i':
		case 'l':
		case 'd':
		case 'o':
		case 'x':
		case 'X': {
			int base = *p == 'x' || *p == 'X' ? 16 :
				*p == 'o' ? 8 : 10;
			int negative = 0, len;
			unsigned long number, power;

			if (*p == 'u') {
				number = va_arg(ap, unsigned int);
			}
			else {
				long signed_number;
				if (*p == 'l') {
					signed_number = va_arg(ap, long);
				}
				else {
					signed_number = va_arg(ap, int);
				}
				if (signed_number < 0) {
					negative = 1;
					number = -signed_number;
				} else
					number = signed_number;
			}

			/* pad */
			len = number_length(number, base);
			while (size-- > len + negative)
				string_add_char(string, fill ? fill : ' ');
			if (negative)
				string_add_char(string, '-');

			/* output number */
			power = 1;
			while (len-- > 1)
				power *= base;
			while (power) {
				int digit = number / power;
				string_add_char(string, digit < 10 ? '0' + digit
					: *p + 'A' - 'X' + digit - 10);
				number -= digit * power;
				power /= base;
			}

			break;
		}
		default:
			/* unknown / invalid format: copy verbatim */
			string_append_at_most(string, fmt, p - fmt + 1);
		}
		fmt = p + (*p != '\0');
	}
}

__attribute__((format (printf, 2, 3)))
void string_addf(struct string *string, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	string_vaddf(string, fmt, ap);
	va_end(ap);
}

__attribute__((format (printf, 2, 3)))
void string_setf(struct string *string, const char *fmt, ...)
{
	va_list ap;

	string_ensure_alloc(string, strlen(fmt) + 64);
	string->length = 0;
	string->buffer[0] = '\0';
	va_start(ap, fmt);
	string_vaddf(string, fmt, ap);
	va_end(ap);
}

__attribute__((format (printf, 1, 2)))
struct string *string_initf(const char *fmt, ...)
{
	struct string *string = string_init(strlen(fmt) + 64);
	va_list ap;

	va_start(ap, fmt);
	string_vaddf(string, fmt, ap);
	va_end(ap);

	return string;
}

void string_replace(struct string *string, char from, char to)
{
	int j;
	for (j = 0; j < string->length; j++)
		if (string->buffer[j] == from)
			string->buffer[j] = to;
}

int string_read_file(struct string *string, const char *path)
{
	FILE *file = fopen(path, "rb");
	char buffer[1024];
	int result = 0;

	if (!file) {
		error("Could not open %s", path);
		return -1;
	}

	for (;;) {
		size_t count = fread(buffer, 1, sizeof(buffer), file);
		string_ensure_alloc(string, string->length + count);
		memcpy(string->buffer + string->length, buffer, count);
		string->length += count;
		if (count < sizeof(buffer))
			break;
	}
	if (ferror(file) < 0)
		result = -1;
	fclose(file);
	return result;
}

void string_escape(struct string *string, const char *characters)
{
	int i, j = string->length;

	for (i = 0; i < string->length; i++)
		if (strchr(characters, string->buffer[i]))
			j++;
	if (i == j)
		return;
	string_ensure_alloc(string, j);
	string->buffer[j] = '\0';
	while (--i < --j) {
		string->buffer[j] = string->buffer[i];
		if (strchr(characters, string->buffer[j]))
			string->buffer[--j] = '\\';
	}
}

void append_string(struct string_array *array, char *str)
{
	if (array->nr >= array->alloc) {
		array->alloc = 2 * array->nr + 16;
		array->list = (char **)xrealloc(array->list,
				array->alloc * sizeof(str));
	}
	array->list[array->nr++] = str;
}

void prepend_string(struct string_array *array, char *str)
{
	if (array->nr >= array->alloc) {
		array->alloc = 2 * array->nr + 16;
		array->list = (char **)xrealloc(array->list,
				array->alloc * sizeof(str));
	}
	memmove(array->list + 1, array->list, array->nr * sizeof(str));
	array->list[0] = str;
	array->nr++;
}

void prepend_string_copy(struct string_array *array, const char *str)
{
	prepend_string(array, xstrdup(str));
}

void append_string_array(struct string_array *target,
		struct string_array *source)
{
	if (target->alloc - target->nr < source->nr) {
		target->alloc += source->nr;
		target->list = (char **)xrealloc(target->list,
				target->alloc * sizeof(target->list[0]));
	}
	memcpy(target->list + target->nr, source->list, source->nr * sizeof(target->list[0]));
	target->nr += source->nr;
}

void prepend_string_array(struct string_array *target,
		struct string_array *source)
{
	if (source->nr <= 0)
		return;
	if (target->alloc - target->nr < source->nr) {
		target->alloc += source->nr;
		target->list = (char **)xrealloc(target->list,
				target->alloc * sizeof(target->list[0]));
	}
	memmove(target->list + source->nr, target->list, target->nr * sizeof(target->list[0]));
	memcpy(target->list, source->list, source->nr * sizeof(target->list[0]));
	target->nr += source->nr;
}

size_t mystrlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

/* Returns the number of leading whitespace characters */
int count_leading_whitespace(const char *line)
{
	int offset = 0;

	while (line[offset] && (line[offset] == ' ' || line[offset] == '\t'))
		offset++;

	return offset;
}

int is_end_of_line(char ch)
{
	return ch == '\r' || ch == '\n';
}
