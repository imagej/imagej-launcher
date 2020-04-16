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
#include <string.h>

#include "common.h"
#include "xalloc.h"

void *xmalloc(size_t size)
{
	void *result = malloc(size);
	if (!result)
		die("Out of memory");
	return result;
}

void *xcalloc(size_t size, size_t nmemb)
{
	void *result = calloc(size, nmemb);
	if (!result)
		die("Out of memory");
	return result;
}

void *xrealloc(void *p, size_t size)
{
	void *result = realloc(p, size);
	if (!result)
		die("Out of memory");
	return result;
}

char *xstrdup(const char *buffer)
{
	char *result = strdup(buffer);
	if (!result)
		die("Out of memory");
	return result;
}

char *xstrndup(const char *buffer, size_t max_length)
{
	char *eos = memchr(buffer, '\0', max_length - 1);
	int len = eos ? eos - buffer : max_length;
	char *result = xmalloc(len + 1);

	if (!result)
		die("Out of memory");

	memcpy(result, buffer, len);
	result[len] = '\0';

	return result;
}


