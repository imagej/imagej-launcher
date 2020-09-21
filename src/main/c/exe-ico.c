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
/* Code to change an .exe file's icon (because good Windows tools are so hard to get by). */

#ifdef WIN32

#include "common.h"
#include "exe-ico.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#pragma pack(2)
struct resource_directory
{
	int8_t width;
	int8_t height;
	int8_t color_count;
	int8_t reserved;
	int16_t planes;
	int16_t bit_count;
	int32_t bytes_in_resource;
	int16_t id;
};

struct header
{
	int16_t reserved;
	int16_t type;
	int16_t count;
};

struct icon_header
{
	int8_t width;
	int8_t height;
	int8_t color_count;
	int8_t reserved;
	int16_t planes;
	int16_t bit_count;
	int32_t bytes_in_resource;
	int32_t image_offset;
};

struct icon_image
{
	BITMAPINFOHEADER header;
	RGBQUAD colors;
	int8_t xors[1];
	int8_t ands[1];
};

struct icon
{
	int count;
	struct header *header;
	struct resource_directory *items;
	struct icon_image **images;
};

static int parse_ico_file(const char *ico_path, struct icon *result)
{
	struct header file_header;
	FILE *file = fopen(ico_path, "rb");
	int i;

	if (!file) {
		error("could not open icon file '%s'", ico_path);
		return 1;
	}

	fread(&file_header, sizeof(struct header), 1, file);
	result->count = file_header.count;

	result->header = malloc(sizeof(struct header) + result->count * sizeof(struct resource_directory));
	result->header->reserved = 0;
	result->header->type = 1;
	result->header->count = result->count;
	result->items = (struct resource_directory *)(result->header + 1);
	struct icon_header *icon_headers = malloc(result->count * sizeof(struct icon_header));
	fread(icon_headers, result->count * sizeof(struct icon_header), 1, file);
	result->images = malloc(result->count * sizeof(struct icon_image *));

	for (i = 0; i < result->count; i++) {
		struct icon_image** image = result->images + i;
		struct icon_header* icon_header = icon_headers + i;
		struct resource_directory *item = result->items + i;

		*image = malloc(icon_header->bytes_in_resource);
		fseek(file, icon_header->image_offset, SEEK_SET);
		fread(*image, icon_header->bytes_in_resource, 1, file);

		memcpy(item, icon_header, sizeof(struct resource_directory));
		item->id = (int16_t)(i + 1);
	}

	fclose(file);

	return 0;
}

int set_exe_icon(const char *exe_path, const char *ico_path)
{
	int id = 1, i;
	struct icon icon;
	HANDLE handle;

	if (suffixcmp(exe_path, -1, ".exe")) {
		error("Not an .exe file: '%s'", exe_path);
		return 1;
	}
	if (!file_exists(exe_path)) {
		error("File not found: '%s'", exe_path);
		return 1;
	}
	if (suffixcmp(ico_path, -1, ".ico")) {
		error("Not an .ico file: '%s'", ico_path);
		return 1;
	}
	if (!file_exists(ico_path)) {
		error("File not found: '%s'", ico_path);
		return 1;
	}

	if (parse_ico_file(ico_path, &icon))
		return 1;

	handle = BeginUpdateResource(exe_path, FALSE);
	if (!handle) {
		error("Could not update resources of '%s'", exe_path);
		return 1;
	}
	UpdateResource(handle, RT_GROUP_ICON,
			"MAINICON", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			icon.header, sizeof(struct header) + icon.count * sizeof(struct resource_directory));
	for (i = 0; i < icon.count; i++) {
		UpdateResource(handle, RT_ICON,
				MAKEINTRESOURCE(id++), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
				icon.images[i], icon.items[i].bytes_in_resource);
	}
	return !EndUpdateResource(handle, FALSE);
}
#endif


