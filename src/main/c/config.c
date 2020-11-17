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
#include "config.h"
#include "file-funcs.h"
#include "java.h"
#include "string-funcs.h"

#include <string.h>

int legacy_mode;
struct string *legacy_ij1_options;

void parse_legacy_config(struct string *jvm_options)
{
	char *p = jvm_options->buffer;
	int line = 1;

	for (;;) {
		char *eol = strchr(p, '\n');

		/* strchrnul() is not portable */
		if (!eol)
			eol = p + strlen(p);

		debug("ImageJ.cfg:%d: %.*s", line, (int)(eol - p), p);

		if (line == 2) {
			int jre_len = -1;
#ifdef WIN32
			if (!suffixcmp(p, eol - p, "\\bin\\javaw.exe"))
				jre_len = eol - p - 14;
			else if (!suffixcmp(p, eol - p, "\\bin\\java.exe")) {
				jre_len = eol - p - 13;
				debug_mode++;
				new_win_console();
				error("Enabling debug mode due to ImageJ.cfg mentioning java.exe");
			}
#else
			if (!suffixcmp(p, eol - p, "/bin/java"))
				jre_len = eol - p - 9;
#endif
			if (jre_len > 0) {
				p[jre_len] = '\0';
				const struct string *jre_dir = string_copy(p);
				if (file_exists(jre_dir->buffer)) {
					set_legacy_jre_path(p);
				} else {
					debug("ImageJ.cfg points to invalid java: %s", p);
				}
				string_release(jre_dir);
			}
		}
		else if (line == 3) {
			char *main_class;

			*eol = '\0';
			main_class = strstr(p, " ij.ImageJ");
			if (main_class) {
				const char *rest = main_class + 10;

				while (*rest == ' ')
					rest++;
				if (rest < eol) {
					if (!legacy_ij1_options)
						legacy_ij1_options = string_init(32);
					string_setf(legacy_ij1_options, "%.*s", (int)(eol - rest), rest);
					debug("Found ImageJ options in ImageJ.cfg: '%s'", legacy_ij1_options->buffer);
				}
				eol = main_class;
			}

			string_replace_range(jvm_options, 0, p - jvm_options->buffer, "");
			string_set_length(jvm_options, eol - p);
			debug("Found Java options in ImageJ.cfg: '%s'", jvm_options->buffer);
			return;
		}

		if (*eol == '\0')
			break;

		p = eol + 1;
		line++;
		if (line > 3)
			break;
	}
	string_set_length(jvm_options, 0);
}

static const char *imagej_cfg_sentinel = "ImageJ startup properties";

int is_modern_config(const char *text)
{
	return *text == '#' &&
		(!prefixcmp(text + 1, imagej_cfg_sentinel) ||
		 (text[1] == ' ' && !prefixcmp(text + 2, imagej_cfg_sentinel)));
}

/* Returns the number of characters to skip to get to the value, or -1 if the key does not match */
static int property_line_key_matches(const char *line, const char *key)
{
	int offset = count_leading_whitespace(line);

	if (prefixcmp(line + offset, key))
		return -1;
	offset += strlen(key);

	offset += count_leading_whitespace(line + offset);

	if (line[offset++] != '=')
		return -1;

	return offset + count_leading_whitespace(line + offset);
}

void parse_modern_config(struct string *jvm_options)
{
	int offset = 0, skip, eol;

	while (jvm_options->buffer[offset]) {
		const char *p = jvm_options->buffer + offset;

		for (eol = offset; !is_end_of_line(jvm_options->buffer[eol]); eol++)
			; /* do nothing */

		/* memory option? */
		if ((skip = property_line_key_matches(p, "maxheap.mb")) > 0) {
			const char *replacement = offset ? " -Xmx" : "-Xmx";
			string_replace_range(jvm_options, offset, offset + skip, replacement);
			eol += strlen(replacement) - skip;
			string_replace_range(jvm_options, eol, eol, "m");
			eol++;
		}
		/* jvmargs? */
		else if ((skip = property_line_key_matches(p, "jvmargs")) > 0) {
			const char *replacement = offset ? " " : "";
			string_replace_range(jvm_options, offset, offset + skip, replacement);
			eol += strlen(replacement) - skip;
		}
		/* legacy.mode? */
		else if ((skip = property_line_key_matches(p, "legacy.mode")) > 0) {
			legacy_mode = !strncmp(p + skip, "true", 4);
			string_replace_range(jvm_options, offset, eol, "");
			eol = offset;
		}
		/* strip it */
		else {
			string_replace_range(jvm_options, offset, eol, "");
			eol = offset;
		}

		for (offset = eol; is_end_of_line(jvm_options->buffer[eol]); eol++)
			; /* do nothing */

		if (offset != eol)
			string_replace_range(jvm_options, offset, eol, "");
	}
}

void read_config(struct string *jvm_options)
{
	const char *path = ij_path("ImageJ.cfg");

	if (file_exists(path)) {
		debug("read_config: reading ImageJ.cfg");
		read_file_as_string(path, jvm_options);
		if (is_modern_config(jvm_options->buffer)) {
			debug("read_config: detected modern config");
			parse_modern_config(jvm_options);
		}
		else {
			debug("read_config: detected legacy config");
			parse_legacy_config(jvm_options);
		}
	}
	else {
		debug("read_config: checking jvm.cfg");
		path = ij_path("jvm.cfg");
		if (file_exists(path))
			read_file_as_string(path, jvm_options);
	}
}
