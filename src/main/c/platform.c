/*
 * #%L
 * ImageJ software for multidimensional image processing and analysis.
 * %%
 * Copyright (C) 2009 - 2015 Board of Regents of the University of
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
#include "platform.h"
#include "file-funcs.h"
#include "java.h"
#include "splash.h"
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

int set_path_to_apple_JVM(void)
{
	/*
	 * MacOSX specific stuff for system java
	 * -------------------------------------
	 * Non-macosx works but places java into separate pid,
	 * which causes all kinds of strange behaviours (app can
	 * launch multiple times, etc).
	 *
	 * Search for system wide java >= 1.5
	 * and if found, launch ImageJ with the system wide java.
	 * This is an adaptation from simple.c from Apple's
	 * simpleJavaLauncher code.
	 */

	if (get_java_home_env()) {
		/* JAVA_HOME is set, and must point to a *non-Apple* JVM. */
		if (debug) {
			error("[APPLE] JAVA_HOME variable is set: '%s'", get_java_home_env());
		}
		return 2;
	}

	/* Ask Apple's java_home executable for its preferred Java 8+ VM. */
	FILE *javaHomeHandle = popen("/usr/libexec/java_home -F -v 1.8+", "r");
	char prefJVM[1035];
	int foundPreferredJVM = 0;
	if (javaHomeHandle) {
		/* Read stdout of the java_home process. */
		while (fgets(prefJVM, sizeof(prefJVM) - 1, javaHomeHandle) != NULL);
		pclose(javaHomeHandle);

		if (strlen(prefJVM) <= 1) {
			if (debug) error("[APPLE] No preferred JVM found.");
		}
		else if (access(prefJVM, R_OK)) {
			/*
			 * Strip newlines.
			 *
			 * Credit to Tim Cas: http://stackoverflow.com/a/28462221
			 *
			 * NB: If we do this before calling 'access' then the check fails!
			 */
			prefJVM[strcspn(prefJVM, "\r\n")] = 0;

			if (debug) error("[APPLE] Found preferred JVM: '%s'", prefJVM);
			foundPreferredJVM = 1;
		}
		else if (debug) {
			error("[APPLE] Ignoring non-existent preferred JVM: '%s'", prefJVM);
		}
	}

	/* We are now ready to search for Java installations! */

	struct string *base = string_init(32);
	struct string *jvm = string_init(32);
	const char *library_path;

	/*
	 * Check the preferred JVM discovered above, if available.
	 */
	if (foundPreferredJVM) {
		string_set_length(base, 0);
		string_append(base, prefJVM);
		library_path = "jre/lib/server/libjvm.dylib";
		if (debug) error("[APPLE] Looking at the preferred JVM as a JDK");
		find_newest(base, 1, library_path, jvm);
		if (jvm->length) {
			set_library_path(library_path + strlen("jre/"));
			string_append(jvm, "/jre/");
			if (debug) error("[APPLE] Using preferred JDK: '%s'", jvm->buffer);
			set_java_home(jvm->buffer);
			string_release(base);
			return 1;
		}

		library_path = "lib/server/libjvm.dylib";
		if (debug) error("[APPLE] Looking at the preferred JVM as a JRE");
		find_newest(base, 1, library_path, jvm);
		if (jvm->length) {
			set_library_path(library_path);
			string_append(jvm, "/");
			if (debug) error("[APPLE] Using preferred JRE: '%s'", jvm->buffer);
			set_java_home(jvm->buffer);
			string_release(base);
			return 1;
		}

		if (debug) error("[APPLE] Ignoring invalid preferred JVM: '%s'", prefJVM);
	}

	/*
	 * Look for a local Java shipped with ImageJ in ${ij.dir}/java/macosx
	 */
	string_set_length(base, 0);
	string_append(base, ij_path("java/macosx"));
	library_path = "jre/Contents/Home/lib/server/libjvm.dylib";
	if (debug) error("[APPLE] Looking for a local Java");
	find_newest(base, 1, library_path, jvm);
	if (jvm->length) {
		set_library_path(library_path + strlen("jre/Contents/Home/"));
		string_append(jvm, "/jre/Contents/Home/");
		if (debug) error("[APPLE] Discovered bundled JRE: '%s'", jvm->buffer);
		set_java_home(jvm->buffer);
		string_release(base);
		return 1;
	}

	/*
	 * Look for a JDK in /Library/Java/JavaVirtualMachines
	 *
	 * This is the JDK from java.oracle.com.
	 */
	string_set_length(base, 0);
	string_append(base, "/Library/Java/JavaVirtualMachines");
	library_path = "Contents/Home/jre/lib/server/libjvm.dylib";
	if (debug) error("[APPLE] Looking for a modern JDK");
	find_newest(base, 1, library_path, jvm);
	if (jvm->length) {
		set_library_path(library_path + strlen("Contents/Home/jre/"));
		string_append(jvm, "/Contents/Home/");
		if (debug) error("[APPLE] Discovered modern JDK: '%s'", jvm->buffer);
		set_java_home(jvm->buffer);
		string_release(base);
		return 1;
	}

	/*
	 * Look for JRE in /Library/Internet Plug-Ins/JavaAppletPlugin.plugin
	 *
	 * This is the JRE from the java.com installer.
	 */
	string_set_length(base, 0);
	string_append(base, "/Library/Internet Plug-Ins/JavaAppletPlugin.plugin");
	library_path = "Contents/Home/lib/server/libjvm.dylib";
	if (debug) error("[APPLE] Looking for a modern JRE");
	find_newest(base, 1, library_path, jvm);
	if (jvm->length) {
		set_library_path(library_path + strlen("Contents/Home/"));
		string_append(jvm, "/Contents/Home/");
		if (debug) error("[APPLE] Discovered modern JRE: '%s'", jvm->buffer);
		set_java_home(jvm->buffer);
		string_release(base);
		return 1;
	}

	/**
	 * Look for old-style JDK in /System/Library/Java/JavaVirtualMachines
	 * This is also known as an Apple JavaVM Framework.
	 *
	 * This is the legacy Apple JDK shipped with older systems. Note that
	 * when reinstalling Java from https://support.apple.com/downloads/java,
	 * it is placed in /Library/Java/JavaVirtualMachines with the others.
	 */
	string_set_length(base, 0);
	string_append(base, "/System/Library/Java/JavaVirtualMachines");
	if (sizeof(void *) > 4)
		library_path = "Contents/Home/../Libraries/libserver.dylib";
	else
		library_path = "Contents/Home/../Libraries/libjvm.dylib";
	if (debug) error("[APPLE] Looking for a JavaVM framework");
	find_newest(base, 1, library_path, jvm);
	if (jvm->length) {
		set_library_path(library_path + strlen("Contents/Home/"));
		string_append(jvm, "/Contents/Home/");
		if (debug) {
			error("[APPLE] Discovered JavaVM framework: '%s'", jvm->buffer);
		}
		set_java_home(jvm->buffer);
		string_release(base);
		return 1;
	}

	/* Clean up. */
	string_release(base);
	string_release(jvm);

	/*
	 * Couldn't find a JDK or JRE or anywhere.
	 *
	 * So let's fall back to Apple's API, looking
	 * for the JavaVM bundle using its identifier.
	 */
	if (debug) error("[APPLE] Looking for a JavaVM bundle");
	CFBundleRef JavaVMBundle =
		CFBundleGetBundleWithIdentifier(CFSTR("com.apple.JavaVM"));
	if (JavaVMBundle) {
		if (debug) error("[APPLE] Found com.apple.JavaVM bundle");
	}
	else {
		/* All searches failed, and no JavaVMBundle. Give up. */
		if (debug) error("[APPLE] No com.apple.JavaVM bundle found");
		fprintf(stderr, "Warning: could not find Java bundle\n");
		return 3;
	}

	/* Get a path for the JavaVM bundle. */
	CFURLRef JavaVMBundleURL = CFBundleCopyBundleURL(JavaVMBundle);
	CFRelease(JavaVMBundle);
	if (!JavaVMBundleURL) {
		fprintf(stderr, "Warning: could not get path for Java\n");
		return 5;
	}

	/* Append to the path the Versions Component. */
	CFURLRef JavaVMBundlerVersionsDirURL =
		CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
			JavaVMBundleURL, CFSTR("Versions"), 1);
	CFRelease(JavaVMBundleURL);
	if (!JavaVMBundlerVersionsDirURL) {
		fprintf(stderr, "Warning: could not detect Java versions\n");
		return 7;
	}
	else if (debug) {
		print_cfurl("JavaVMBundlerVersionsDirURL", JavaVMBundlerVersionsDirURL);
	}

	/* Append to the path the target JVM's Version. */
	CFURLRef TargetJavaVM = NULL;
	CFStringRef targetJVM; /* Minimum Java5. */

	/* TODO: disable this test on 10.6+ */
	/* Try 1.6 only with 64-bit */
	if (is_intel() && sizeof(void *) > 4) {
		if (debug) error("[APPLE] Detected 64-bit Intel machine");
		targetJVM = CFSTR("1.6");
		TargetJavaVM =
			CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
				JavaVMBundlerVersionsDirURL, targetJVM, 1);
		if (debug && cfurl_dir_exists(TargetJavaVM)) {
			print_cfurl("[APPLE] Found Apple Java VM 1.6", TargetJavaVM);
		}
	}

	int needs_retrotranslator = 0;

	if (!cfurl_dir_exists(TargetJavaVM)) {
		needs_retrotranslator = 1;
		targetJVM = CFSTR("1.5");
		TargetJavaVM =
			CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
				JavaVMBundlerVersionsDirURL, targetJVM, 1);
		if (debug && cfurl_dir_exists(TargetJavaVM)) {
			print_cfurl("[APPLE] Found Apple Java VM 1.5", TargetJavaVM);
		}
	}

	if (!cfurl_dir_exists(TargetJavaVM)) {
		/* No 1.6 or 1.5, so look for the default Versions/A. */
		needs_retrotranslator = 0;
		targetJVM = CFSTR("A");
		TargetJavaVM =
			CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
				JavaVMBundlerVersionsDirURL, targetJVM, 1);
		if (cfurl_dir_exists(TargetJavaVM)) {
			if (debug) {
				print_cfurl("[APPLE] Found default Apple Java VM", TargetJavaVM);
			}
			/**
			 * Folder found; return success without setting JAVA_JVM_VERSION.
			 *
			 * This will hopefully result in JNI_CreateJavaVM being called
			 * and linking against the default Apple Framework JVM.
			 */
			return 0;
		}
	}

	CFRelease(JavaVMBundlerVersionsDirURL);
	if (!cfurl_dir_exists(TargetJavaVM)) {
		fprintf(stderr, "Warning: Could not locate compatible Apple Java VM\n");
		return 11;
	}

	UInt8 pathToTargetJVM[PATH_MAX] = "";
	Boolean result = CFURLGetFileSystemRepresentation(TargetJavaVM, 1,
				pathToTargetJVM, PATH_MAX);
	CFRelease(TargetJavaVM);
	if (!result) {
		fprintf(stderr, "Warning: could not get path for Java VM\n");
		return 13;
	}

	/*
	 * Check to see if the directory, or a symlink for the target
	 * JVM directory exists, and if so set the environment
	 * variable JAVA_JVM_VERSION to the target JVM.
	 */
	if (access((const char *)pathToTargetJVM, R_OK)) {
		fprintf(stderr, "Warning: Could not access Java VM: %s\n", (const char *)pathToTargetJVM);
		return 17;
	}

	/*
	 * Ok, the directory exists, so now we need to set the
	 * environment var JAVA_JVM_VERSION to the CFSTR targetJVM.
	 *
	 * We can reuse the pathToTargetJVM buffer to set the environment
	 * varable.
	 */
	if (!CFStringGetCString(targetJVM, (char *)pathToTargetJVM,
		PATH_MAX, kCFStringEncodingUTF8))
	{
		fprintf(stderr, "Warning: Could not set JAVA_JVM_VERSION\n");
		return 19;
	}

	/* Set JAVA_JVM_VERSION to the Apple Framework JVM that we found. */
	if (debug) {
		error("Setting JAVA_JVM_VERSION to %s\n", (const char *)pathToTargetJVM);
	}
	setenv("JAVA_JVM_VERSION",
		(const char *)pathToTargetJVM, 1);

	if (needs_retrotranslator)
		retrotranslator = 1;

	return 0;
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

	const char *c_string = CFStringGetCStringPtr(propertyString, kCFStringEncodingMacRoman);
	if (!c_string)
		return -6;

	string_set_length(value, 0);
	string_append(value, c_string);

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

/*
 * Them stupid Apple software designers -- in their infinite wisdom -- added
 * 64-bit support to Tiger without really supporting it.
 *
 * As a consequence, a universal binary will be executed in 64-bit mode on
 * a x86_64 machine, even if neither CoreFoundation nor Java can be linked,
 * and sure enough, the executable will crash.
 *
 * It does not even reach main(), so we have to have a binary that does _not_
 * provide 64-bit support, detect if it is actually on Leopard, and execute
 * another binary in that case that _does_ provide 64-bit support, even if
 * we'd rather meet the Apple software designers some night, with a baseball
 * bat in our hands, than execute an innocent binary that is not to blame.
 */
static int is_osrelease(int min)
{
	int mib[2] = { CTL_KERN, KERN_OSRELEASE };
	static char os_release[128];
	size_t len = sizeof(os_release);;
	static int initialized;

	if (!initialized) {
		if (sysctl(mib, 2, os_release, &len, NULL, 0) == -1) {
			if (debug)
				error("sysctl to determine os_release failed: %d (%s)", errno, strerror(errno));
			return 0;
		}
		if (debug)
			error("sysctl says os_release is %s", os_release);
		initialized = 1;
	}

	return atoi(os_release) >= min;
}

static MAYBE_UNUSED int is_mountain_lion(void)
{
	return is_osrelease(12);
}

int is_lion(void)
{
	return is_osrelease(11);
}

static MAYBE_UNUSED int is_snow_leopard(void)
{
	return is_osrelease(10);
}

static int is_leopard(void)
{
	return is_osrelease(9);
}

static MAYBE_UNUSED int is_tiger(void)
{
	return is_osrelease(8);
}

int launch_32bit_on_tiger(int argc, char **argv)
{
	const char *match, *replace;

	if (force_32_bit_mode(argv[0]))
		return 0;

	if (is_intel() && is_leopard()) {
		match = "-tiger";
		replace = "-macosx";
	}
	else { /* Tiger */
		match = "-macosx";
		replace = "-tiger";
		if (sizeof(void *) < 8)
			return 0; /* already 32-bit, everything's fine */
	}

	int offset = strlen(argv[0]) - strlen(match);
	if (offset < 0 || strcmp(argv[0] + offset, match))
		return 0; /* suffix not found, no replacement */

	if (strlen(replace) > strlen(match)) {
		char *buffer = (char *)xmalloc(offset + strlen(replace) + 1);
		memcpy(buffer, argv[0], offset);
		argv[0] = buffer;
	}
	strcpy(argv[0] + offset, replace);
	if (!file_exists(argv[0])) {
		strcpy(argv[0] + offset, match);
		return 0;
	}
	hide_splash();
	execv(argv[0], argv);
	fprintf(stderr, "Could not execute %s: %d(%s)\n",
		argv[0], errno, strerror(errno));
	exit(1);
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

void sleep(int seconds)
{
	Sleep(seconds * 1000);
}

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

void open_win_console(void)
{
	static int initialized = 0;
	struct string *kernel32_dll_path;
	void *kernel32_dll;
	BOOL WINAPI (*attach_console)(DWORD process_id) = NULL;
	SECURITY_ATTRIBUTES attributes;
	HANDLE handle;

	if (initialized)
		return;
	initialized = 1;
	console_attached = 1;
	if (!isatty(1) && !isatty(2))
		return;

	kernel32_dll_path = string_initf("%s\\system32\\kernel32.dll",
		getenv("WINDIR"));
	kernel32_dll = dlopen(kernel32_dll_path->buffer, RTLD_LAZY);
	string_release(kernel32_dll_path);
	if (kernel32_dll)
		attach_console = (typeof(attach_console))
			dlsym(kernel32_dll, "AttachConsole");
	if (!attach_console || !attach_console((DWORD)-1)) {
		if (attach_console) {
			if (GetLastError() == ERROR_ACCESS_DENIED)
				/*
				 * Already attached, according to
				 * http://msdn.microsoft.com/en-us/library/windows/desktop/ms681952(v=vs.85).aspx
				 */
				return;
			error("error attaching console: %s", get_win_error());
		}
		AllocConsole();
		console_opened = 1;
		atexit(sleep_a_while);
	} else {
		char title[1024];
		if (GetConsoleTitle(title, sizeof(title)) &&
				!strncmp(title, "rxvt", 4))
			return; /* Console already opened. */
	}

	memset(&attributes, 0, sizeof(attributes));
	attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	attributes.bInheritHandle = TRUE;

	handle = CreateFile("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE,
		&attributes, OPEN_EXISTING, 0, NULL);
	if (isatty(1)) {
		freopen("CONOUT$", "wt", stdout);
		SetStdHandle(STD_OUTPUT_HANDLE, handle);
	}
	if (isatty(2)) {
		freopen("CONOUT$", "wb", stderr);
		SetStdHandle(STD_ERROR_HANDLE, handle);
	}
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
