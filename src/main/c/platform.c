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
#include "platform.h"
#include "file-funcs.h"
#include "java.h"
#include "xalloc.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* A wrapper for setenv that exits on error */
void setenv_or_exit(const char *name, const char *value, int overwrite)
{
	int result;
	if (!value) {
#ifdef __APPLE__
		unsetenv(name);
#else
		result = unsetenv(name);
		if (result)
			die("Unsetting environment variable %s failed", name);
#endif
		return;
	}
	result = setenv(name, value, overwrite);
	if (result)
		die("Setting environment variable %s to %s failed", name, value);
}

/* returns bit-width (32, 64), or 0 if it is not a .dll */
static int MAYBE_UNUSED is_dll(const char *path)
{
	int in;
	unsigned char buffer[0x40];
	unsigned char *p;
	off_t offset;

	if (suffixcmp(path, -1, ".dll"))
		return 0;

	if ((in = open(path, O_RDONLY | O_BINARY)) < 0)
		return 0;

	if (!read_exactly(in, buffer, sizeof(buffer)) ||
			buffer[0] != 'M' || buffer[1] != 'Z') {
		close(in);
		return 0;
	}

	p = (unsigned char *)(buffer + 0x3c);
	offset = p[0] | (p[1] << 8) | (p[2] << 16) | (p[2] << 24);
	lseek(in, offset, SEEK_SET);
	if (!read_exactly(in, buffer, 0x20) ||
			buffer[0] != 'P' || buffer[1] != 'E' ||
			buffer[2] != '\0' || buffer[3] != '\0') {
		close(in);
		return 0;
	}

	close(in);
	if (buffer[0x17] & 0x20)
		return (buffer[0x17] & 0x1) ? 32 : 64;
	return 0;
}

static int MAYBE_UNUSED is_elf(const char *path)
{
	int in;
	unsigned char buffer[0x40];

	if (suffixcmp(path, -1, ".so"))
		return 0;

	if ((in = open(path, O_RDONLY | O_BINARY)) < 0)
		return 0;

	if (!read_exactly(in, buffer, sizeof(buffer))) {
		close(in);
		return 0;
	}

	close(in);
	if (buffer[0] == '\x7f' && buffer[1] == 'E' && buffer[2] == 'L' &&
			buffer[3] == 'F')
		return buffer[4] * 32;
	return 0;
}

static int MAYBE_UNUSED is_dylib(const char *path)
{
	int in;
	unsigned char buffer[0x40];

	if (suffixcmp(path, -1, ".dylib") &&
			suffixcmp(path, -1, ".jnilib"))
		return 0;

	if ((in = open(path, O_RDONLY | O_BINARY)) < 0)
		return 0;

	if (!read_exactly(in, buffer, sizeof(buffer))) {
		close(in);
		return 0;
	}

	close(in);

	/*
	 * mach-o header parsing
	 *
	 * check cafebabe and feedface
	 */
	if (buffer[0] == 0xca && buffer[1] == 0xfe && buffer[2] == 0xba &&
			buffer[3] == 0xbe && buffer[4] == 0x00 &&
			buffer[5] == 0x00 && buffer[6] == 0x00 &&
			(buffer[7] >= 1 && buffer[7] < 20))
		return 32 | 64; /* might be a fat one, containing both */

	/*
	 * check both endians for feedface
	 */
	if ((buffer[0] == 0xcf &&
			buffer[1] == 0xfa &&
			buffer[2] == 0xed &&
			buffer[3] == 0xfe) ||
			(buffer[0] == 0xfe &&
			buffer[1] == 0xed &&
			buffer[2] == 0xfa &&
			buffer[3] == 0xcf))
		return 64;

	if ((buffer[0] == 0xce &&
			buffer[1] == 0xfa &&
			buffer[2] == 0xed &&
			buffer[3] == 0xfe) ||
			(buffer[0] == 0xfe &&
			buffer[1] == 0xed &&
			buffer[2] == 0xfa &&
			buffer[3] == 0xce))
		return 32;

	return 0;
}

int is_native_library(const char *path)
{
#ifdef __APPLE__
	return is_dylib(path);
#else
	return
#ifdef WIN32
		is_dll(path)
#else
		is_elf(path)
#endif
		== sizeof(char *) * 8;
#endif
}

#ifdef __APPLE__

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/machine.h>
#include <pthread.h>
#include <string.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

/* Determining heap size */

size_t get_memory_size(int available_only)
{
	host_priv_t host = mach_host_self();
	vm_size_t page_size;
	vm_statistics_data_t host_info;
	mach_msg_type_number_t host_count =
		sizeof(host_info) / sizeof(integer_t);

	host_page_size(host, &page_size);
	return host_statistics(host, HOST_VM_INFO,
			(host_info_t)&host_info, &host_count) ?
		0 : ((size_t)(available_only ? host_info.free_count :
				host_info.active_count +
				host_info.inactive_count +
				host_info.wire_count) * (size_t)page_size);
}

void append_icon_path(struct string *str, const char *main_argv0)
{
	/*
	 * Check if we're launched from within an Application bundle or
	 * command line.  If from a bundle, ImageJ.app should be the IJ dir.
	 */
	int length = str->length, i;
	const char *paths[] = {
		"Contents/Resources/Fiji.icns",
		"images/Fiji.icns",
		"Contents/Resources/ImageJ.icns",
		"images/ImageJ.icns"
	};
	const char *slash;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		string_append(str, ij_path(paths[i]));
		if (file_exists(str->buffer + length))
			return;
		string_set_length(str, length);
	}

	slash = strrchr(main_argv0, '/');
	if (slash && !suffixcmp(main_argv0, slash - main_argv0, ".app/Contents/MacOS"))
		for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
			string_addf(str, "%.*s%s", (int)(slash - main_argv0) - 14, main_argv0, paths[i]);
			if (file_exists(str->buffer + length))
				return;
			string_set_length(str, length);
		}
}

static void print_cfurl(const char *message, CFURLRef cfurl)
{
	UInt8 bytes[PATH_MAX] = "";
	Boolean result = CFURLGetFileSystemRepresentation(cfurl, 1,
		bytes, PATH_MAX);
	if (result) {
		error("%s: %s", message, (const char *)bytes);
	}
	else {
		error("%s: <error>\n", message);
	}
}

static int cfurl_dir_exists(const CFURLRef cfurl)
{
	UInt8 bytes[PATH_MAX] = "";
	Boolean result = CFURLGetFileSystemRepresentation(cfurl, 1,
		bytes, PATH_MAX);
	if (!result) {
		/* Invalid CFURL. */
		return 0;
	}
	return dir_exists((const char *)bytes);
}

static struct string *convert_cfstring(CFStringRef ref, struct string *buffer)
{
	string_ensure_alloc(buffer, (int)CFStringGetLength(ref) * 6);
	if (!CFStringGetCString((CFStringRef)ref, buffer->buffer, buffer->alloc, kCFStringEncodingUTF8))
		string_set_length(buffer, 0);
	else
		buffer->length = strlen(buffer->buffer);
	return buffer;
}

static struct string *resolve_alias(CFDataRef ref, struct string *buffer)
{
	if (!ref)
		string_set_length(buffer, 0);
	else {
		CFRange range = { 0, CFDataGetLength(ref) };
		if (range.length <= 0) {
			string_set_length(buffer, 0);
			return buffer;
		}
		AliasHandle alias = (AliasHandle)NewHandle(range.length);
		CFDataGetBytes(ref, range, (void *)*alias);

		string_ensure_alloc(buffer, 1024);
		FSRef fs;
		Boolean changed;
		if (FSResolveAlias(NULL, alias, &fs, &changed) == noErr)
			FSRefMakePath(&fs, (unsigned char *)buffer->buffer, buffer->alloc);
		else {
			CFStringRef string;
			if (FSCopyAliasInfo(alias, NULL, NULL, &string, NULL, NULL) == noErr) {
				convert_cfstring(string, buffer);
				CFRelease(string);
			}
			else
				string_set_length(buffer, 0);
		}
		buffer->length = strlen(buffer->buffer);

		DisposeHandle((Handle)alias);
	}

	return buffer;
}

static int is_intel(void)
{
	int mib[2] = { CTL_HW, HW_MACHINE };
	char result[128];
	size_t len = sizeof(result);;

	if (sysctl(mib, 2, result, &len, NULL, 0) < 0)
		return 0;
	return !strcmp(result, "i386") || !strncmp(result, "x86", 3);
}

static int force_32_bit_mode(const char *argv0)
{
	int result = 0;
	struct string *buffer = string_initf("%s/%s", getenv("HOME"),
		"Library/Preferences/com.apple.LaunchServices.plist");
	if (!buffer)
		return 0;

	CFURLRef launchServicesURL =
		CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
		(unsigned char *)buffer->buffer, buffer->length, 0);
	if (!launchServicesURL)
		goto release_buffer;

	CFDataRef data;
	SInt32 errorCode;
	if (!CFURLCreateDataAndPropertiesFromResource(kCFAllocatorDefault,
			launchServicesURL, &data, NULL, NULL, &errorCode))
		goto release_url;

	CFDictionaryRef dict;
	CFStringRef errorString;
	dict = (CFDictionaryRef)CFPropertyListCreateFromXMLData(kCFAllocatorDefault,
		data, kCFPropertyListImmutable, &errorString);
	if (!dict || errorString)
		goto release_data;


	CFDictionaryRef arch64 = (CFDictionaryRef)CFDictionaryGetValue(dict,
		CFSTR("LSArchitecturesForX86_64"));
	if (!arch64)
		goto release_dict;

	CFArrayRef app = (CFArrayRef)CFDictionaryGetValue(arch64, CFSTR("org.fiji"));
	if (!app) {
		app = (CFArrayRef)CFDictionaryGetValue(arch64, CFSTR("net.imagej.ImageJ"));
		if (!app)
			goto release_dict;
	}

	int i, count = (int)CFArrayGetCount(app);
	for (i = 0; i < count; i += 2) {
		convert_cfstring((CFStringRef)CFArrayGetValueAtIndex(app, i + 1), buffer);
		if (strcmp(buffer->buffer, "i386"))
			continue;
		resolve_alias((CFDataRef)CFArrayGetValueAtIndex(app, i), buffer);
		if (!strcmp(buffer->buffer, get_ij_dir())) {
			fprintf(stderr, "Forcing 32-bit, as requested\n");
			result = 1;
			break;
		}
	}
release_dict:
	CFRelease(dict);
release_data:
	CFRelease(data);
release_url:
	CFRelease(launchServicesURL);
release_buffer:
	string_release(buffer);
	return result;
}

int get_fiji_bundle_variable(const char *key, struct string *value)
{
	/*
	 * Reading the command line options from the Info.plist file in the
	 * Application bundle.
	 *
	 * This routine expects a separate dictionary for fiji with the
	 * options from the command line as keys.
	 *
	 * If Info.plist is not present (i.e. if started from the cmd-line),
	 * the whole thing will be just skipped.
	 *
	 * Example: Setting the java heap to 1024m
	 * <key>fiji</key>
	 * <dict>
	 *	<key>heap</key>
	 *	<string>1024</string>
	 * </dict>
	 */

	static CFDictionaryRef fijiInfoDict;
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;

		/* Get the main bundle for the app. */
		CFBundleRef fijiBundle = CFBundleGetMainBundle();
		if (!fijiBundle)
			return -1;

		/* Get an instance of the non-localized keys. */
		CFDictionaryRef bundleInfoDict =
			CFBundleGetInfoDictionary(fijiBundle);
		if (!bundleInfoDict)
			return -2;

		fijiInfoDict = (CFDictionaryRef)
			CFDictionaryGetValue(bundleInfoDict, CFSTR("fiji"));
	}

	if (!fijiInfoDict)
		return -3;

	CFStringRef key_ref =
		CFStringCreateWithCString(NULL, key,
			kCFStringEncodingMacRoman);
	if (!key_ref)
		return -4;

	CFStringRef propertyString = (CFStringRef)
		CFDictionaryGetValue(fijiInfoDict, key_ref);
	CFRelease(key_ref);
	if (!propertyString)
		return -5;

	/*
	 * The CFStringGetCStringPtr documentation at
	 * https://developer.apple.com/documentation/corefoundation/1542133-cfstringgetcstringptr
	 * states:
	 *
	 * > This function either returns the requested pointer immediately, with no
	 * > memory allocations and no copying, in constant time, or returns NULL.
	 * > If the latter is the result, call an alternative function such as the
	 * > CFStringGetCString(_:_:_:_:) function to extract the characters.
	 * >
	 * > Whether or not this function returns a valid pointer or NULL depends on
	 * > many factors, all of which depend on how the string was created and its
	 * > properties. In addition, the function result might change between
	 * > different releases and on different platforms. So do not count on
	 * > receiving a non-NULL result from this function under any circumstances.
	 *
	 * Therefore, we avoid that function in favor of CFStringGetCString,
	 * which makes a copy.
	 */
	CFIndex length = CFStringGetLength(propertyString);
	struct string *temp = string_init(length + 1);
	int success = CFStringGetCString(propertyString,
		temp->buffer, length + 1, kCFStringEncodingMacRoman);
	if (!success) {
		string_release(temp);
		return -6;
	}
	string_set(value, temp->buffer);
	string_release(temp);

	return 0;
}

/* MacOSX needs to run Java in a new thread, AppKit in the main thread. */

static void dummy_call_back(void *info) {
}

static void *start_ij_aux(void *dummy)
{
	exit(start_ij());
}

int start_ij_macosx(const char *main_argv0)
{
	struct string *env_key, *icon_path;

	/* set the Application's name */
	env_key = string_initf("APP_NAME_%d", (int)getpid());
	setenv(env_key->buffer, "ImageJ", 1);

	/* set the Dock icon */
	string_setf(env_key, "APP_ICON_%d", (int)getpid());
	icon_path = string_init(32);
	append_icon_path(icon_path, main_argv0);
	if (icon_path->length)
		setenv(env_key->buffer, icon_path->buffer, 1);

	string_release(env_key);
	string_release(icon_path);

	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	/* Start the thread that we will start the JVM on. */
	pthread_create(&thread, &attr, start_ij_aux, NULL);
	pthread_attr_destroy(&attr);

	CFRunLoopSourceContext context;
	memset(&context, 0, sizeof(context));
	context.perform = &dummy_call_back;

	CFRunLoopSourceRef ref = CFRunLoopSourceCreate(NULL, 0, &context);
	CFRunLoopAddSource (CFRunLoopGetCurrent(), ref, kCFRunLoopCommonModes);
	CFRunLoopRun();

	return 0;
}

#elif defined(__linux__)

/* Determining heap size */

static size_t get_kB(struct string *string, const char *key)
{
	const char *p = strstr(string->buffer, key);
	if (!p)
		return 0;
	while (*p && *p != ' ')
		p++;
	return (size_t)strtoul(p, NULL, 10);
}

size_t get_memory_size(int available_only)
{
	ssize_t page_size, available_pages;
	/* Avoid overallocation */
	if (!available_only) {
		struct string *string = string_init(32);
		if (!string_read_file(string, "/proc/meminfo"))
			return 1024 * (get_kB(string, "MemFree:")
				+ get_kB(string, "Buffers:")
				+ get_kB(string, "Cached:"));
	}
	page_size = sysconf(_SC_PAGESIZE);
	available_pages = sysconf(available_only ?
		_SC_AVPHYS_PAGES : _SC_PHYS_PAGES);
	return page_size < 0 || available_pages < 0 ?
		0 : (size_t)page_size * (size_t)available_pages;
}

/* work around a SuSE IPv6 setup bug */

#ifdef IPV6_MAYBE_BROKEN
#include <netinet/ip6.h>
#include <fcntl.h>
#endif

int is_ipv6_broken(void)
{
#ifndef IPV6_MAYBE_BROKEN
	return 0;
#else
	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	static const struct in6_addr in6addr_loopback = {
		{ { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } }
	};
	struct sockaddr_in6 address = {
		AF_INET6, 57294 + 7, 0, in6addr_loopback, 0
	};
	int result = 0;
	long flags;

	if (sock < 0)
		return 1;

	flags = fcntl(sock, F_GETFL, NULL);
	if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
		close(sock);
		return 1;
	}


	if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
		if (errno == EINPROGRESS) {
			struct timeval tv;
			fd_set fdset;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;
			FD_ZERO(&fdset);
			FD_SET(sock, &fdset);
			if (select(sock + 1, NULL, &fdset, NULL, &tv) > 0) {
				int error;
				socklen_t length = sizeof(int);
				if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
						(void*)&error, &length) < 0)
					result = 1;
				else
					result = (error == EACCES) |
						(error == EPERM) |
						(error == EAFNOSUPPORT) |
						(error == EINPROGRESS);
			} else
				result = 1;
		} else
			result = (errno == EACCES) | (errno == EPERM) |
				(errno == EAFNOSUPPORT);
	}

	close(sock);
	return result;
#endif
}

#elif defined(WIN32)

#include <windows.h>

/* Determining heap size */

size_t get_memory_size(int available_only)
{
	MEMORYSTATUS status;

	GlobalMemoryStatus(&status);
	return available_only ? status.dwAvailPhys : status.dwTotalPhys;
}

static char *dlerror_value;

void *dlopen(const char *name, int flags)
{
	void *result = LoadLibrary(name);

	dlerror_value = get_win_error();

	return result;
}

char *dlerror(void)
{
	/* We need to reset the error */
	char *result = dlerror_value;
	dlerror_value = NULL;
	return result;
}

void *dlsym(void *handle, const char *name)
{
	void *result = (void *)GetProcAddress((HMODULE)handle, name);
	dlerror_value = result ? NULL : (char *)"function not found";
	return result;
}

#ifndef __MINGW32__
/* __MINGW32__ is defined by MinGW64 for both x86 and amd64:
 * http://mingw.5.n7.nabble.com/Macros-MINGW32-AND-MINGW64-tp26319p26320.html
*/
void sleep(int seconds)
{
	Sleep(seconds * 1000);
}
#endif

/*
 * There is no setenv on Windows, so it should be safe for us to
 * define this compatible version.
 */
int setenv(const char *name, const char *value, int overwrite)
{
	struct string *string;

	if (!overwrite && getenv(name))
		return 0;
	if ((!name) || (!value))
		return 0;

	string = string_initf("%s=%s", name, value);
	return putenv(string->buffer);
}

/* Similarly we can do the same for unsetenv: */
int unsetenv(const char *name)
{
	struct string *string = string_initf("%s=", name);
	return putenv(string->buffer);
}

char *get_win_error(void)
{
	DWORD error_code = GetLastError();
	LPSTR buffer;

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error_code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&buffer,
			0, NULL);
	return buffer;
}

void win_verror(const char *fmt, va_list ap)
{
	struct string *string = string_init(32);

	string_vaddf(string, fmt, ap);
	MessageBox(NULL, string->buffer, "ImageJ Error", MB_OK);
	string_release(string);
}

MAYBE_UNUSED void win_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	win_verror(fmt, ap);
	va_end(ap);
}

#include <stdio.h>

int console_opened, console_attached;

static void sleep_a_while(void)
{
	sleep(60);
}

/*
 * Attempts to attach output to the spawning console
 */
void attach_win_console(void)
{
	if (!console_attached) {
		console_attached = 1;
		AttachConsole(-1);
		open_comm_channels();
		printf("\n--ImageJ output attached--");
	}
}

/*
 * Starts a new, dedicated console for output as if launched as a console application.
 */
void new_win_console(void)
{
	if (!console_attached) {
		console_attached = 1;
		FreeConsole();
		AllocConsole();
		open_comm_channels();
	}
}

void open_comm_channels(void)
{
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
}

#undef mkdir
int fake_posix_mkdir(const char *name, int mode)
{
	return mkdir(name);
}

DIR *open_dir(const char *path)
{
	struct dir *result = xcalloc(sizeof(struct dir), 1);
	if (!result)
		return result;
	result->pattern = string_initf("%s/*", path);
	result->handle = FindFirstFile(result->pattern->buffer,
			&(result->find_data));
	if (result->handle == INVALID_HANDLE_VALUE) {
		string_release(result->pattern);
		free(result);
		return NULL;
	}
	result->done = 0;
	return result;
}

struct entry *read_dir(struct dir *dir)
{
	if (dir->done)
		return NULL;
	strcpy(dir->entry.d_name, dir->find_data.cFileName);
	dir->entry.d_namlen = strlen(dir->entry.d_name);
	if (FindNextFile(dir->handle, &dir->find_data) == 0)
		dir->done = 1;
	return &dir->entry;
}

int close_dir(struct dir *dir)
{
	string_release(dir->pattern);
	FindClose(dir->handle);
	free(dir);
	return 0;
}

char *dos_path(const char *path)
{
	const char *orig = path;
	int size = GetShortPathName(path, NULL, 0);
	char *buffer;

	if (!size)
		path = find_in_path(path, 1);
	size = GetShortPathName(path, NULL, 0);
	if (!size)
		die ("Could not determine DOS name of %s", orig);
	buffer = (char *)xmalloc(size);
	GetShortPathName(path, buffer, size);
	return buffer;
}

#else /* Unknown platform */

/* Determining heap size */

size_t get_memory_size(int available_only)
{
	fprintf(stderr, "Cannot reserve optimal memory on this platform\n");
	return 0;
}

#endif
