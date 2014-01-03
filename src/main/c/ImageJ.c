/*
 * #%L
 * ImageJ software for multidimensional image processing and analysis.
 * %%
 * Copyright (C) 2009 - 2014 Board of Regents of the University of
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
 * 
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of any organization.
 * #L%
 */

/*
 * This is the ImageJ launcher, a small native program to handle the
 * startup of Java and ImageJ.
 *
 * This program was originally developed as the Fiji launcher
 * (http://fiji.sc/), but has been adapted and improved for use with ImageJ
 * core. It is also meant to be the default launcher of ImageJ 1.x.
 *
 * The ImageJ launcher is copyright 2007 - 2013 Johannes Schindelin, Mark
 * Longair, Albert Cardona, Benjamin Schmid, Erwin Frise, Gregory Jefferis
 * and Curtis Rueden.
 *
 * Clarification: the license of the ImageJ launcher has no effect on
 * the Java Runtime, ImageJ or any plugins, since they are not derivatives.
 *
 * @author Johannes Schindelin
 * @author Erwin Frise
 * @author Mark Longair
 * @author Albert Cardona
 * @author Benjamin Schmid
 * @author Gregory Jefferis
 * @author Curtis Rueden
 */

#define _BSD_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if !defined(WIN32) || !defined(__TINYC__)
#include <unistd.h>
#endif

#include "jni.h"

#include "common.h"
#include "exe-ico.h"
#include "file-funcs.h"
#include "java.h"
#include "platform.h"
#include "splash.h"
#include "string-funcs.h"
#include "xalloc.h"

static const char *default_fiji1_class = "fiji.Main";
static const char *default_main_class = "imagej.Main";
static int legacy_mode;
int retrotranslator;
static int debug;

static const char *legacy_ij1_class = "ij.ImageJ";
static struct string *legacy_ij1_options;

static int is_default_ij1_class(const char *name)
{
	return name && (!strcmp(name, default_fiji1_class) ||
			!strcmp(name, legacy_ij1_class));
}

/* This returns the amount of megabytes */
static long parse_memory(const char *amount)
{
	char *endp;
	long result = strtol(amount, &endp, 0);

	if (endp)
		switch (*endp) {
		case 't': case 'T':
			result <<= 10;
			/* fall through */
		case 'g': case 'G':
			result <<= 10;
			/* fall through */
		case 'm': case 'M':
		case '\0':
			/* fall back to megabyte */
			break;
		default:
			die("Unsupported memory unit '%c' in %s", *endp, amount);
		}

	return result;
}

static MAYBE_UNUSED int parse_bool(const char *value)
{
	return strcmp(value, "0") && strcmp(value, "false") &&
		strcmp(value, "False") && strcmp(value, "FALSE");
}

/* Java stuff */

#ifndef JNI_CREATEVM
#define JNI_CREATEVM "JNI_CreateJavaVM"
#endif

static char *ij_launcher_jar;
static char *main_argv0;
static char **main_argv, **main_argv_backup;
static int main_argc, main_argc_backup;
static const char *main_class, *startup_class;

static MAYBE_UNUSED struct string *get_parent_directory(const char *path)
{
	const char *slash = last_slash(path);

	if (!slash || slash == path)
		return string_initf("/");
	return string_initf("%.*s", (int)(slash - path), path);
}

/*
 * On Linux, Java5 does not find the library path with libmlib_image.so,
 * so we have to add that explicitely to the LD_LIBRARY_PATH.
 *
 * Unfortunately, ld.so only looks at LD_LIBRARY_PATH at startup, so we
 * have to reexec after setting that variable.
 *
 * See also line 140ff of
 * http://hg.openjdk.java.net/jdk6/jdk6/hotspot/file/14f7b2425c86/src/os/solaris/launcher/java_md.c
 *
 * As noticed by Ilan Tal, we also need to re-execute if the LD_LIBRARY_PATH
 * was extended for some other reason, e.g. when native libraries were discovered
 * in $IJDIR/lib/; This also affects Java6 because it is only clever enough to handle
 * the inter-dependent JRE libraries without requiring a correctly set LD_LIBRARY_PATH.
 *
 * We assume here that java_library_path was initialized with the value
 * of the environment variable LD_LIBRARY_PATH; if at the end, java_library_path
 * is longer than LD_LIBRARY_PATH, we have to reset it (and for Java5, re-execute).
 */
static void maybe_reexec_with_correct_lib_path(struct string *java_library_path)
{
#ifdef __linux__
	const char *jre_home = get_jre_home(), *original = getenv("LD_LIBRARY_PATH");

	if (jre_home) {
		struct string *path, *parent, *lib_path, *jli;

		path = string_initf("%s/%s", jre_home, get_library_path());
		parent = get_parent_directory(path->buffer);
		lib_path = get_parent_directory(parent->buffer);
		jli = string_initf("%s/jli", lib_path->buffer);

		/* Is this JDK5? */
		if (dir_exists(get_jre_home()) && !dir_exists(jli->buffer)) {
			/* need to make sure the JRE lib/ is in LD_LIBRARY_PATH */
			if (!path_list_contains(java_library_path->buffer, lib_path->buffer))
				string_append_path_list(java_library_path, lib_path->buffer);
			if (!path_list_contains(java_library_path->buffer, parent->buffer))
				string_append_path_list(java_library_path, parent->buffer);
		}
		string_release(jli);
		string_release(lib_path);
		string_release(parent);
		string_release(path);
	}

	/* Was LD_LIBRARY_PATH already correct? */
	if (java_library_path->length == (original ? strlen(original) : 0))
		return;

	setenv_or_exit("LD_LIBRARY_PATH", java_library_path->buffer, 1);
	if (debug)
		error("Re-executing with correct library lookup path (%s)", java_library_path->buffer);
	hide_splash();
	execvp(main_argv_backup[0], main_argv_backup);
	die("Could not re-exec with correct library lookup (%d: %s)!", errno, strerror(errno));
#elif defined(__APPLE__)
	const char *original;

	if (!is_lion())
		return;

	original = getenv("DYLD_LIBRARY_PATH");
	if (original != NULL && strlen(original) == java_library_path->length)
		return;

	setenv_or_exit("DYLD_LIBRARY_PATH", java_library_path->buffer, 1);
	if (debug)
		error("Re-executing with correct library lookup path (%s)", java_library_path->buffer);
	hide_splash();
	execvp(main_argv_backup[0], main_argv_backup);
	die("Could not re-exec with correct library lookup: %d (%s)", errno, strerror(errno));
#endif
}

static int create_java_vm(JavaVM **vm, void **env, JavaVMInitArgs *args)
{
#ifdef __APPLE__
	if (!set_path_to_apple_JVM()) {
		/* We found an Apple Framework JVM (pre-Java-1.7). */
		return JNI_CreateJavaVM(vm, env, args);
	}
#endif

	/*
	 * At this point, we are either not on OS X, or on a
	 * newer OS X that is missing the Apple Framework paths:
	 *
	 *     /System/Library/Frameworks/JavaVM.framework/Versions/1.6
	 *     /System/Library/Frameworks/JavaVM.framework/Versions/1.5
	 *
	 * So we'll do things the good ol' fashioned way: dlopen and dlsym.
	 */

	/*
	 * Save the original value of JAVA_HOME: if creating the JVM this
	 * way doesn't work, set it back so that calling the system JVM
	 * can use the JAVA_HOME variable if it's set...
	 */
	char *original_java_home_env = getenv("JAVA_HOME");
	struct string *buffer = string_init(32);
	void *handle;
	char *err;
	static jint (*JNI_CreateJavaVM)(JavaVM **pvm, void **penv, void *args);
	const char *java_home = get_jre_home();

	if (!java_home) {
		error("No known JRE; cannot link to Java library");
		return 1;
	}

#ifdef WIN32
	/* On Windows, a setenv() invalidates strings obtained by getenv(). */
	if (original_java_home_env)
		original_java_home_env = xstrdup(original_java_home_env);
#endif

	setenv_or_exit("JAVA_HOME", java_home, 1);

	string_addf(buffer, "%s/%s", java_home, get_library_path());

	handle = dlopen(buffer->buffer, RTLD_LAZY);
	if (!handle) {
		const char *err;
		if (debug)
			error("Could not open '%s'", buffer->buffer);
		setenv_or_exit("JAVA_HOME", original_java_home_env, 1);
		if (!file_exists(java_home)) {
			if (debug)
				error("'%s' does not exist", java_home);
			string_release(buffer);
			return 2;
		}

		err = dlerror();
		if (!err)
			err = "(unknown error)";
		error("Could not load Java library '%s': %s",
			buffer->buffer, err);
		string_release(buffer);
		return 1;
	}
	dlerror(); /* Clear any existing error */

	JNI_CreateJavaVM = dlsym(handle, JNI_CREATEVM);
	err = dlerror();
	if (err) {
		error("Error loading libjvm: %s: %s", buffer->buffer, err);
		setenv_or_exit("JAVA_HOME", original_java_home_env, 1);
		string_release(buffer);
		return 1;
	}

	return JNI_CreateJavaVM(vm, env, args);
}

static void initialize_ij_launcher_jar_path(void)
{
	ij_launcher_jar = find_jar(ij_path("jars/"), "ij-launcher");
}

static int add_retrotranslator_to_path(struct string *path)
{
	const char *retro = ij_path("retro/");
	DIR *dir = opendir(retro);
	struct dirent *entry;
	int counter = 0;

	if (!dir) {
		error("Could not find Retrotranslator, trying to continue without.");
		return 0;
	}

	while ((entry = readdir(dir))) {
		if (suffixcmp(entry->d_name, -1, ".jar"))
			continue;
		string_append_path_list(path, retro);
		string_append(path, entry->d_name);
		counter++;
	}

	if (!counter)
		error("Could not find Retrotranslator, trying to continue without.");
	return counter;
}

static int headless, headless_argc, batch;

static struct string *set_property(JNIEnv *env,
		const char *key, const char *value)
{
	static jclass system_class = NULL;
	static jmethodID set_property_method = NULL;
	jstring result;

	if (!value)
		return NULL;

	if (!system_class) {
		system_class = (*env)->FindClass(env, "java/lang/System");
		if (!system_class)
			return NULL;
	}

	if (!set_property_method) {
		set_property_method = (*env)->GetStaticMethodID(env, system_class,
				"setProperty",
				"(Ljava/lang/String;Ljava/lang/String;)"
				"Ljava/lang/String;");
		if (!set_property_method)
			return NULL;
	}

	result = (jstring)(*env)->CallStaticObjectMethod(env, system_class,
				set_property_method,
				(*env)->NewStringUTF(env, key),
				(*env)->NewStringUTF(env, value));
	if (result) {
		const char *chars = (*env)->GetStringUTFChars(env, result, NULL);
		struct string *res = string_copy(chars);
		(*env)->ReleaseStringUTFChars(env, result, chars);
		return res;
	}

	return NULL;
}

static JavaVMOption *prepare_java_options(struct string_array *array)
{
	JavaVMOption *result = (JavaVMOption *)xcalloc(array->nr,
			sizeof(JavaVMOption));
	int i;

	for (i = 0; i < array->nr; i++)
		result[i].optionString = array->list[i];

	return result;
}

static jobjectArray prepare_ij_options(JNIEnv *env, struct string_array* array)
{
	jstring jstr;
	jobjectArray result;
	int i;

	if (!(jstr = (*env)->NewStringUTF(env, array->nr ? array->list[0] : ""))) {
fail:
		(*env)->ExceptionDescribe(env);
		die("Failed to create ImageJ option array");
	}

	result = (*env)->NewObjectArray(env, array->nr,
			(*env)->FindClass(env, "java/lang/String"), jstr);
	if (!result)
		goto fail;
	for (i = 1; i < array->nr; i++) {
		if (!(jstr = (*env)->NewStringUTF(env, array->list[i])))
			goto fail;
		(*env)->SetObjectArrayElement(env, result, i, jstr);
	}
	return result;
}

struct options {
	struct string_array java_options, launcher_options, ij_options;
	int dry_run, use_system_jvm;
};

static void add_launcher_option(struct options *options, const char *option, const char *class_path)
{
	append_string(&options->launcher_options, xstrdup(option));
	if (class_path)
		append_string(&options->launcher_options, xstrdup(class_path));
}

static void add_tools_jar(struct options *options)
{
	const char *jre_home = get_jre_home();
	struct string *string;

	if (!jre_home)
		die("Cannot determine path to tools.jar");

	string = string_initf("%s/../lib/tools.jar", jre_home);
	add_launcher_option(options, "-classpath", string->buffer);
	string_release(string);
}

static void add_option(struct options *options, char *option, int for_ij)
{
	append_string(for_ij ?
			&options->ij_options : &options->java_options, option);
}

static void add_option_copy(struct options *options, const char *option, int for_ij)
{
	add_option(options, xstrdup(option), for_ij);
}

static void add_option_string(struct options *options, struct string *option, int for_ij)
{
	add_option(options, xstrdup(option->buffer), for_ij);
}

static int is_quote(char c)
{
	return c == '\'' || c == '"';
}

static int find_closing_quote(const char *s, char quote, int index, int len)
{
	int i;

	for (i = index; i < len; i++) {
		char c = s[i];
		if (c == quote)
			return i;
		if (is_quote(c))
			i = find_closing_quote(s, c, i + 1, len);
	}
	fprintf(stderr, "Unclosed quote: %s\n               ", s);
	for (i = 0; i < index; i++)
		fputc(' ', stderr);
	die("^");
}

static void add_options(struct options *options, const char *cmd_line, int for_ij)
{
	int len = strlen(cmd_line), i, cp_option = 0;
	struct string *current = string_init(32);

	for (i = 0; i <= len; i++) {
		char c = cmd_line[i];
		if (is_quote(c)) {
			int i2 = find_closing_quote(cmd_line, c, i + 1, len);
			string_append_at_most(current, cmd_line + i + 1, i2 - i - 1);
			i = i2;
			continue;
		}
		if (!c || c == ' ' || c == '\t' || c == '\n') {
			if (!current->length)
				continue;
			if (!strcmp(current->buffer, "-cp"))
				cp_option = 1;
			else if (cp_option) {
				if (strcmp(current->buffer, "ij.jar"))
					add_launcher_option(options,
						"--ijcp", current->buffer);
				cp_option = 0;
			} else
				add_option_string(options, current, for_ij);
			string_set_length(current, 0);
		} else
			string_add_char(current, c);
	}

	string_release(current);
}

/*
 * If passing -Xmx=99999999g -Xmx=37m to Java, the former still triggers an
 * error. So let's keep only the last, so that the command line can override
 * invalid settings in jvm.cfg.
 */
static void keep_only_one_memory_option(struct string_array *options)
{
	int index_Xmx = -1, index_Xms = -1, index_Xmn = -1;
	int i, j;

	for (i = options->nr - 1; i >= 0; i--)
		if (index_Xmx < 0 && !prefixcmp(options->list[i], "-Xmx"))
			index_Xmx = i;
		else if (index_Xms < 0 && !prefixcmp(options->list[i], "-Xms"))
			index_Xms = i;
		else if (index_Xmn < 0 && !prefixcmp(options->list[i], "-Xmn"))
			index_Xmn = i;

	for (i = j = 0; i < options->nr; i++)
		if ((i < index_Xmx && !prefixcmp(options->list[i], "-Xmx")) ||
				(i < index_Xms && !prefixcmp(options->list[i], "-Xms")) ||
				(i < index_Xmn && !prefixcmp(options->list[i], "-Xmn")))
			continue;
		else {
			if (i > j)
				options->list[j] = options->list[i];
			j++;
		}
	options->nr = j;
}

static const char* has_option_with_prefix(struct string_array *options,
	const char *prefix)
{
	int i;
	for (i = options->nr - 1; i >= 0; i--)
		if (!prefixcmp(options->list[i], prefix))
			return options->list[i];
	return NULL;
}

static const char* has_memory_option(struct string_array *options)
{
	return has_option_with_prefix(options, "-Xm");
}

static const char* has_plugins_dir_option(struct string_array *options)
{
	return has_option_with_prefix(options, "-Dplugins.dir=");
}

static MAYBE_UNUSED void read_file_as_string(const char *file_name, struct string *contents)
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

static struct string *quote_if_necessary(const char *option)
{
	struct string *result = string_init(32);
	for (; *option; option++)
		switch (*option) {
		case '\n':
			string_append(result, "\\n");
			break;
		case '\t':
			string_append(result, "\\t");
			break;
		case ' ': case '"': case '\\':
			string_add_char(result, '\\');
			/* fallthru */
		default:
			string_add_char(result, *option);
			break;
		}
	return result;
}

#ifdef WIN32
/* fantastic win32 quoting */
static char *quote_win32(char *option)
{
	char *p, *result, *r1;
	int backslashes = 0;

	for (p = option; *p; p++)
		if (strchr(" \"\t", *p))
			backslashes++;

	if (!backslashes)
		return option;

	result = (char *)xmalloc(strlen(option) + backslashes + 2 + 1);
	r1 = result;
	*(r1++) = '"';
	for (p = option; *p; p++) {
		if (*p == '"')
			*(r1++) = '\\';
		*(r1++) = *p;
	}
	*(r1++) = '"';
	*(r1++) = '\0';

	return result;
}
#endif

static void show_commandline(struct options *options)
{
	int j;

	printf("%s", get_java_command());
	for (j = 0; j < options->java_options.nr; j++) {
		struct string *quoted = quote_if_necessary(options->java_options.list[j]);
		printf(" %s", quoted->buffer);
		string_release(quoted);
	}
	printf(" %s", main_class);
	for (j = 0; j < options->ij_options.nr; j++) {
		struct string *quoted = quote_if_necessary(options->ij_options.list[j]);
		printf(" %s", quoted->buffer);
		string_release(quoted);
	}
	fputc('\n', stdout);
}

int file_is_newer(const char *path, const char *than)
{
	struct stat st1, st2;

	if (stat(path, &st1))
		return 0;
	return stat(than, &st2) || st1.st_mtime > st2.st_mtime;
}

int handle_one_option(int *i, const char **argv, const char *option, struct string *arg)
{
	int len;
	string_set_length(arg, 0);
	if (!strcmp(argv[*i], option)) {
		if (++(*i) >= main_argc || !argv[*i])
			die("Option %s needs an argument!", option);
		string_append(arg, argv[*i]);
		return 1;
	}
	len = strlen(option);
	if (!strncmp(argv[*i], option, len) && argv[*i][len] == '=') {
		string_append(arg, argv[*i] + len + 1);
		return 1;
	}
	return 0;
}

static int is_file_empty(const char *path)
{
	struct stat st;

	return !stat(path, &st) && !st.st_size;
}

static int update_files(struct string *relative_path)
{
	int len = relative_path->length, source_len, target_len;
	struct string *source = string_initf("%s/update%s",
		get_ij_dir(), relative_path->buffer), *target;
	DIR *directory = opendir(source->buffer);
	struct dirent *entry;

	if (!directory) {
		string_release(source);
		return 0;
	}
	target = string_copy(ij_path(relative_path->buffer));
	if (mkdir_p(target->buffer)) {
		string_release(source);
		string_release(target);
		die("Could not create directory: %s", relative_path->buffer);
	}
	string_add_char(source, '/');
	source_len = source->length;
	string_add_char(target, '/');
	target_len = target->length;
	while (NULL != (entry = readdir(directory))) {
		const char *filename = entry->d_name;

		if (!strcmp(filename, ".") || !strcmp(filename, ".."))
			continue;

		string_set_length(relative_path, len);
		string_addf(relative_path, "/%s", filename);
		if (update_files(relative_path)) {
			continue;
		}

		string_set_length(source, source_len);
		string_append(source, filename);
		string_set_length(target, target_len);
		string_append(target, filename);

		if (is_file_empty(source->buffer)) {
			if (unlink(source->buffer))
				error("Could not remove %s", source->buffer);
			if (unlink(target->buffer))
				error("Could not remove %s", target->buffer);
			continue;
		}

#ifdef WIN32
		if (file_exists(target->buffer) && unlink(target->buffer)) {
			if (!strcmp(filename, "ImageJ.exe") || !strcmp(filename, "ImageJ-win32.exe") || !strcmp(filename, "ImageJ-win64.exe")) {
				struct string *old = string_initf("%.*s.old.exe", target->length - 4, target->buffer);
				if (file_exists(old->buffer) && unlink(old->buffer))
					die("Could not move %s out of the way!", old->buffer);
				if (rename(target->buffer, old->buffer))
					die("Could not remove old version of %s.  Please move %s to %s manually!", target->buffer, source->buffer, target->buffer);
				string_release(old);
			}
			else
				die("Could not remove old version of %s.  Please remove it manually!", target->buffer);
		}
#endif
		if (rename(source->buffer, target->buffer))
			die("Could not move %s to %s: %s", source->buffer,
				target->buffer, strerror(errno));
	}
	closedir(directory);
	string_set_length(source, source_len - 1);
	rmdir(source->buffer);

	string_release(source);
	string_release(target);
	string_set_length(relative_path, len);

	return 1;
}

static void update_all_files(void)
{
	struct string *buffer = string_init(32);
	update_files(buffer);
	string_release(buffer);
}

/*
 * Flexible subcommand handling
 *
 * Every command line option of the form --<name> will be expanded to
 * <expanded>.
 */
struct subcommand
{
	char *name, *expanded;
	struct string description;
	struct {
		char **list;
		int alloc, size;
	} extensions;
};

struct {
	int alloc, size;
	struct subcommand *list;
} all_subcommands;

static int iswhitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

static void add_extension(struct subcommand *subcommand, const char *extension)
{
	int length = strlen(extension);

	while (length && iswhitespace(extension[length - 1]))
		length--;
	if (!length)
		return;

	if (subcommand->extensions.size + 1 >= subcommand->extensions.alloc) {
		int alloc = (16 + subcommand->extensions.alloc) * 3 / 2;
		subcommand->extensions.list = xrealloc(subcommand->extensions.list, alloc * sizeof(char *));
		subcommand->extensions.alloc = alloc;
	}
	subcommand->extensions.list[subcommand->extensions.size++] = xstrndup(extension, length);
}

/*
 * The files for subcommand configuration are of the form
 *
 * <option>: <command-line options to replacing the subcommand options>
 *  <description>
 *  [<possible continuation>]
 * [.extension]
 *
 * Example:
 *
 * --mini-maven --ij-jar=jars/ij-minimaven.jar --main-class=imagej.build.MiniMaven
 *  Start MiniMaven in the current directory
 */
static void add_subcommand(const char *line)
{
	int size = all_subcommands.size;
	/* TODO: safeguard against malformed configuration files. */
	struct subcommand *latest = &all_subcommands.list[size - 1];

	/* Is it the description? */
	if (line[0] == ' ') {
		struct string *description = &latest->description;

		string_append(description, "\t");
		string_append(description, line + 1);
		string_append(description, "\n");
	}
	else if (line[0] == '-') {
		struct subcommand *current;
		const char *space;
		int length = strlen(line);

		if (length && line[length - 1] == '\n')
			length --;
		if (length && line[length - 1] == '\r')
			length --;
		if (!length)
			return;

		if (size == all_subcommands.alloc) {
			int alloc = (size + 16) * 3 / 2;
			all_subcommands.list = xrealloc(all_subcommands.list,
				alloc * sizeof(struct subcommand));
			all_subcommands.alloc = alloc;
		}

		current = &all_subcommands.list[size];
		memset(current, 0, sizeof(struct subcommand));
		space = strchr(line, ' ');
		if (space) {
			current->name = xstrndup(line, space - line);
			current->expanded = xstrndup(space + 1,
				length - (space + 1 - line));
		}
		else
			current->name = xstrndup(line, length);
		all_subcommands.size++;
	}
	else if (line[0] == '.') {
		add_extension(latest, line);
	}
}

const char *default_subcommands[] = {
	"--update --dont-patch-ij1 --full-classpath --main-class=imagej.updater.ui.CommandLine",
	" start the command-line version of the ImageJ updater",
	"--jython --ij-jar=jars/jython.jar --full-classpath --main-class=org.python.util.jython",
	".py",
	" start Jython instead of ImageJ (this is the",
	" default when called with a file ending in .py)",
	"--jruby --ij-jar=jars/jruby.jar --full-classpath --main-class=org.jruby.Main",
	".rb",
	" start JRuby instead of ImageJ (this is the",
	" default when called with a file ending in .rb)",
	"--clojure --ij-jar=jars/clojure.jar --full-classpath --main-class=clojure.lang.Repl",
	".clj",
	" start Clojure instead of ImageJ (this is the """,
	" default when called with a file ending in .clj)",
	"--beanshell --ij-jar=jars/bsh.jar --full-classpath --main-class=bsh.Interpreter",
	".bs",
	"--bsh --ij-jar=jars/bsh.jar --full-classpath --main-class=bsh.Interpreter",
	".bsh",
	" start BeanShell instead of ImageJ (this is the",
	" default when called with a file ending in .bs or .bsh",
	"--javascript --ij-jar=jars/js.jar --full-classpath --main-class=org.mozilla.javascript.tools.shell.Main",
	"--js --ij-jar=jars/js.jar --full-classpath --main-class=org.mozilla.javascript.tools.shell.Main",
	".js",
	" start Javascript (the Rhino engine) instead of",
	" ImageJ (this is the default when called with a",
	" file ending in .js)",
	"--ant --tools-jar --ij-jar=jars/ant.jar --ij-jar=jars/ant-launcher.jar --ij-jar=jars/ant-nodeps.jar --ij-jar=jars/ant-junit.jar --dont-patch-ij1 --headless --main-class=org.apache.tools.ant.Main",
	" run Apache Ant",
	"--mini-maven --ij-jar=jars/ij-minimaven.jar --dont-patch-ij1 --main-class=imagej.build.MiniMaven",
	" run Fiji's very simple Maven mockup",
	"--javac --ij-jar=jars/javac.jar --freeze-classloader --headless --full-classpath --dont-patch-ij1 --pass-classpath --main-class=com.sun.tools.javac.Main",
	" start JavaC, the Java Compiler, instead of ImageJ",
	"--javah --only-tools-jar --headless --full-classpath --dont-patch-ij1 --pass-classpath --main-class=com.sun.tools.javah.Main",
	" start javah instead of ImageJ",
	"--javap --only-tools-jar --headless --full-classpath --dont-patch-ij1 --pass-classpath --main-class=sun.tools.javap.Main",
	" start javap instead of ImageJ",
	"--javadoc --only-tools-jar --headless --full-classpath --dont-patch-ij1 --pass-classpath --main-class=com.sun.tools.javadoc.Main",
	" start javadoc instead of ImageJ",
};

static void initialize_subcommands(void)
{
	int i;
	if (all_subcommands.size)
		return;
	for (i = 0; i < sizeof(default_subcommands) / sizeof(default_subcommands[0]); i++)
		add_subcommand(default_subcommands[i]);
}

static const char *expand_subcommand(const char *option)
{
	int i;

	initialize_subcommands();
	for (i = 0; i < all_subcommands.size; i++)
		if (!strcmp(option, all_subcommands.list[i].name))
			return all_subcommands.list[i].expanded;
	return NULL;
}

static const char *expand_subcommand_for_extension(const char *extension)
{
	int i, j;

	if (!extension)
		return NULL;

	initialize_subcommands();
	for (i = 0; i < all_subcommands.size; i++)
		for (j = 0; j < all_subcommands.list[i].extensions.size; j++)
			if (!strcmp(extension, all_subcommands.list[i].extensions.list[j]))
				return all_subcommands.list[i].expanded;
	return NULL;
}

static const char *get_file_extension(const char *path)
{
	int i = strlen(path);

	while (i)
		if (path[i - 1] == '.')
			return path + i - 1;
		else if (path[i - 1] == '/' || path[i - 1] == '\\')
			return NULL;
		else
			i--;
	return NULL;
}

__attribute__((format (printf, 1, 2)))
static int jar_exists(const char *fmt, ...)
{
	struct string string = { 0, 0, NULL };
	va_list ap;
	int result;

	va_start(ap, fmt);
	string_vaddf(&string, fmt, ap);
	result = file_exists(string.buffer);
	free(string.buffer);
	va_end(ap);

	return result;
}

/*
 * Check whether all .jar files specified in the classpath are available.
 */
static int check_subcommand_classpath(struct subcommand *subcommand)
{
	const char *expanded = subcommand->expanded;

	while (expanded && *expanded) {
		const char *space = strchr(expanded, ' ');
		if (!space)
			space = expanded + strlen(expanded);
		if (!prefixcmp(expanded, "--fiji-jar=")) {
			expanded += 11;
			if (!jar_exists("%s/%.*s", ij_path(""), (int)(space - expanded), expanded))
				return 0;
		}
		if (!prefixcmp(expanded, "--ij-jar=")) {
			expanded += 9;
			if (!jar_exists("%s/%.*s", ij_path(""), (int)(space - expanded), expanded))
				return 0;
		}
		else if (!prefixcmp(expanded, "--tools-jar") || !prefixcmp(expanded, "--only-tools-jar")) {
			const char *jre_home = get_jre_home();
			if (!jre_home || !jar_exists("%s/../lib/tools.jar", jre_home))
				return 0;
		}
		expanded = space + !!*space;
	}
	return 1;
}

static void parse_legacy_config(struct string *jvm_options)
{
	char *p = jvm_options->buffer;
	int line = 1;

	for (;;) {
		char *eol = strchr(p, '\n');

		/* strchrnul() is not portable */
		if (!eol)
			eol = p + strlen(p);

		if (debug > 1)
			error("ImageJ.cfg:%d: %.*s", line, (int)(eol - p), p);

		if (line == 2) {
			int jre_len = -1;
#ifdef WIN32
			if (!suffixcmp(p, eol - p, "\\bin\\javaw.exe"))
				jre_len = eol - p - 14;
			else if (!suffixcmp(p, eol - p, "\\bin\\java.exe")) {
				jre_len = eol - p - 13;
				debug++;
				open_win_console();
				error("Enabling debug mode due to ImageJ.cfg mentioning java.exe");
			}
#else
			if (!suffixcmp(p, eol - p, "/bin/java"))
				jre_len = eol - p - 9;
#endif
			if (jre_len > 0) {
				p[jre_len] = '\0';
				set_legacy_jre_path(p);
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
					if (debug)
						error("Found ImageJ options in ImageJ.cfg: '%s'", legacy_ij1_options->buffer);
				}
				eol = main_class;
			}

			string_replace_range(jvm_options, 0, p - jvm_options->buffer, "");
			string_set_length(jvm_options, eol - p);
			if (debug)
				error("Found Java options in ImageJ.cfg: '%s'", jvm_options->buffer);
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

const char *imagej_cfg_sentinel = "ImageJ startup properties";

static int is_modern_config(const char *text)
{
	return *text == '#' &&
		(!prefixcmp(text + 1, imagej_cfg_sentinel) ||
		 (text[1] == ' ' && !prefixcmp(text + 2, imagej_cfg_sentinel)));
}

/* Returns the number of leading whitespace characters */
static int count_leading_whitespace(const char *line)
{
	int offset = 0;

	while (line[offset] && (line[offset] == ' ' || line[offset] == '\t'))
		offset++;

	return offset;
}

static int is_end_of_line(char ch)
{
	return ch == '\r' || ch == '\n';
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

static void parse_modern_config(struct string *jvm_options)
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

static void read_config(struct string *jvm_options)
{
	const char *path = ij_path("ImageJ.cfg");

	if (file_exists(path)) {
		read_file_as_string(path, jvm_options);
		if (is_modern_config(jvm_options->buffer))
			parse_modern_config(jvm_options);
		else
			parse_legacy_config(jvm_options);
	}
	else {
		path = ij_path("jvm.cfg");
		if (file_exists(path))
			read_file_as_string(path, jvm_options);
	}
}

static void __attribute__((__noreturn__)) usage(void)
{
	struct string subcommands = { 0, 0, NULL };
	int i;

	initialize_subcommands();
	for (i = 0; i < all_subcommands.size; i++) {
		struct subcommand *subcommand = &all_subcommands.list[i];
		if (!check_subcommand_classpath(subcommand))
			continue;
		string_addf(&subcommands, "%s\n%s", subcommand->name,
			subcommand->description.length ? subcommand->description.buffer : "");
	}

	die("Usage: %s [<Java options>.. --] [<ImageJ options>..] [<files>..]\n"
		"\n%s%s%s%s%s%s%s%s",
		main_argv[0],
		"Java options are passed to the Java Runtime, ImageJ\n"
		"options to ImageJ (or Jython, JRuby, ...).\n"
		"\n"
		"In addition, the following options are supported by ImageJ:\n"
		"General options:\n"
		"--help, -h\n"
		"\tshow this help\n",
		"--dry-run\n"
		"\tshow the command line, but do not run anything\n"
		"--debug\n"
		"\tverbose output\n"
		"--system\n"
		"\tdo not try to run bundled Java\n"
		"--java-home <path>\n"
		"\tspecify JAVA_HOME explicitly\n"
		"--print-java-home\n"
		"\tprint ImageJ's idea of JAVA_HOME\n"
		"--print-ij-dir\n"
		"\tprint where ImageJ thinks it is located\n",
#ifdef WIN32
		"--console\n"
		"\talways open an error console\n"
		"--set-icon <exe-file> <ico-file>\n"
		"\tadd/replace the icon of the given program\n"
#endif
		"--headless\n"
		"\trun in text mode\n"
		"--ij-dir <path>\n"
		"\tset the ImageJ directory to <path> (used to find\n"
		"\t jars/, plugins/ and macros/)\n"
		"--heap, --mem, --memory <amount>\n"
		"\tset Java's heap size to <amount> (e.g. 512M)\n"
		"--class-path, --classpath, -classpath, --cp, -cp <path>\n"
		"\tappend <path> to the class path\n"
		"--jar-path, --jarpath, -jarpath <path>\n"
		"\tappend .jar files in <path> to the class path\n",
		"--pass-classpath\n"
		"\tpass -classpath <classpath> to the main() method\n"
		"--full-classpath\n"
		"\tcall the main class with the full ImageJ class path\n"
		"--dont-patch-ij1\n"
		"\tdo not try to runtime-patch ImageJ1\n"
		"--ext <path>\n"
		"\tset Java's extension directory to <path>\n"
		"--default-gc\n"
		"\tdo not use advanced garbage collector settings by default\n"
		"\t(-Xincgc -XX:PermSize=128m)\n"
		"--gc-g1\n"
		"\tuse the G1 garbage collector\n"
		"--debug-gc\n"
		"\tshow debug info about the garbage collector on stderr\n"
		"--debugger=<port>[,suspend]\n"
		"\tstart the JVM in a mode so Eclipse's debugger can attach to it\n"
		"--no-splash\n"
		"\tsuppress showing a splash screen upon startup\n"
		"\n",
		"Options for ImageJ:\n"
		"--ij2\n"
		"\tstart ImageJ2 instead of ImageJ1\n"
		"--ij1\n"
		"\tstart ImageJ1\n"
		"--allow-multiple\n"
		"\tdo not reuse existing ImageJ instance\n"
		"--plugins <dir>\n"
		"\tuse <dir> to discover plugins\n"
		"--run <plugin> [<arg>]\n"
		"\trun <plugin> in ImageJ, optionally with arguments\n"
		"--compile-and-run <path-to-.java-file>\n"
		"\tcompile and run <plugin> in ImageJ\n"
		"--edit [<file>...]\n"
		"\tedit the given file in the script editor\n"
		"\n",
		"Options to run programs other than ImageJ:\n",
		subcommands.buffer,
		"--main-class <class name> (this is the\n"
		"\tdefault when called with a file ending in .class)\n"
		"\tstart the given class instead of ImageJ\n"
		"--retrotranslator\n"
		"\tuse Retrotranslator to support Java < 1.6\n\n");
	string_release(&subcommands);
}

static const char *skip_whitespace(const char *string)
{
	while (iswhitespace(*string))
		string++;
	return string;
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

static unsigned int guess_java_version(void)
{
	const char *java_home = get_jre_home();

	while (java_home && *java_home) {
		if (!prefixcmp(java_home, "jdk") || !prefixcmp(java_home, "jre")) {
			unsigned int result = 0;
			const char *p = java_home + 3;

			p = parse_number(p, &result, 24);
			if (p && *p == '.')
				p = parse_number(p + 1, &result, 16);
			if (p && *p == '.')
				p = parse_number(p + 1, &result, 8);
			if (p) {
				if (*p == '_')
					p = parse_number(p + 1, &result, 0);
				return result;
			}
		}
		java_home += strcspn(java_home, "\\/") + 1;
	}
	return 0;
}

static void jvm_workarounds(struct options *options)
{
	unsigned int java_version = guess_java_version();

	if (java_version == 0x01070000 || java_version == 0x01070001) {
#ifndef WIN32
		add_option(options, "-XX:-UseLoopPredicate", 0);
#endif
		if (main_class && !strcmp(main_class, "sun.tools.javap.Main"))
			main_class = "com.sun.tools.javap.Main";
	}
	else if (java_version && java_version < 0x01060000)
		retrotranslator = 1;
}

/* the maximal size of the heap on 32-bit systems, in megabyte */
#ifdef WIN32
#define MAX_32BIT_HEAP 1638
#else
#define MAX_32BIT_HEAP 1920
#endif

struct string *make_memory_option(long megabytes)
{
	return string_initf("-Xmx%dm", (int)megabytes);
}

static void try_with_less_memory(long megabytes)
{
	char **new_argv;
	int i, j, found_dashdash;
	struct string *buffer;
	size_t subtract;

	/* Try again, with 25% less memory */
	if (megabytes < 0)
		return;
	subtract = megabytes >> 2;
	if (!subtract)
		return;
	megabytes -= subtract;

	buffer = string_initf("--mem=%dm", (int)megabytes);

	main_argc = main_argc_backup;
	main_argv = main_argv_backup;
	new_argv = (char **)xmalloc((3 + main_argc) * sizeof(char *));
	new_argv[0] = main_argv[0];

	j = 1;
	new_argv[j++] = xstrdup(buffer->buffer);

	/* Strip out --mem options. */
	found_dashdash = 0;
	for (i = 1; i < main_argc; i++) {
		struct string *dummy = string_init(32);
		if (!found_dashdash && !strcmp(main_argv_backup[i], "--"))
			found_dashdash = 1;
		if ((!found_dashdash || is_default_ij1_class(main_class)) &&
				(handle_one_option(&i, (const char **)main_argv, "--mem", dummy) ||
				 handle_one_option(&i, (const char **)main_argv, "--memory", dummy)))
			continue;
		new_argv[j++] = main_argv[i];
	}
	new_argv[j] = NULL;

	if (debug)
		error("Trying with a smaller heap: %s", buffer->buffer);

	hide_splash();

#ifdef WIN32
	new_argv[0] = dos_path(new_argv[0]);
	for (i = 0; i < j; i++)
		new_argv[i] = quote_win32(new_argv[i]);
#ifdef WIN64
	execve(new_argv[0], (char * const *)new_argv, NULL);
#else
	execve(new_argv[0], (const char * const *)new_argv, NULL);
#endif
#else
	execv(new_argv[0], new_argv);
#endif

	string_setf(buffer, "ERROR: failed to launch (errno=%d;%s):\n",
		errno, strerror(errno));
	for (i = 0; i < j; i++)
		string_addf(buffer, "%s ", new_argv[i]);
	string_add_char(buffer, '\n');
#ifdef WIN32
	MessageBox(NULL, buffer->buffer, "Error executing ImageJ", MB_OK);
#endif
	die("%s", buffer->buffer);
}

static const char *maybe_substitute_ij_jar(const char *relative_path)
{
	const char *replacement = NULL;

	if (!strcmp(relative_path, "jars/jython.jar"))
		replacement = "/usr/share/java/jython.jar";
	else if (!strcmp(relative_path, "jars/clojure.jar"))
		replacement = "/usr/share/java/clojure.jar";
	else if (!strcmp(relative_path, "jars/bsh-2.0b4.jar") || !strcmp(relative_path, "jars/bsh.jar"))
		replacement = "/usr/share/java/bsh.jar";
	else if (!strcmp(relative_path, "jars/ant.jar"))
		replacement = "/usr/share/java/ant.jar";
	else if (!strcmp(relative_path, "jars/ant-launcher.jar"))
		replacement = "/usr/share/java/ant-launcher.jar";
	else if (!strcmp(relative_path, "jars/ant-nodeps.jar"))
		replacement = "/usr/share/java/ant-nodeps.jar";
	else if (!strcmp(relative_path, "jars/ant-junit.jar"))
		replacement = "/usr/share/java/ant-junit.jar";
	else if (!strcmp(relative_path, "jars/jsch-0.1.44.jar") || !strcmp(relative_path, "jars/jsch.jar"))
		replacement = "/usr/share/java/jsch.jar";
	else if (!strcmp(relative_path, "jars/javassist.jar"))
		replacement = "/usr/share/java/javassist.jar";

	if (!replacement || file_exists(ij_path(relative_path)))
		return NULL;

	return replacement;
}

/*
 * Returns the number of elements which this option spans if it is an ImageJ1 option, 0 otherwise.
 */
static int imagej1_option_count(const char *option)
{
	if (!option)
		return 0;
	if (option[0] != '-') /* file names */
		return 1;
	if (!prefixcmp(option, "-port") || !strcmp(option, "-debug"))
		return 1;
	if (!strcmp(option, "-ijpath") || !strcmp(option, "-macro") || !strcmp(option, "-eval") || !strcmp(option, "-run"))
		return 2;
	if (!strcmp(option, "-batch"))
		return 3;
	return 0;
}

const char *properties[32];

static struct options options;
static long megabytes = 0;
static struct string buffer, buffer2, arg, plugin_path, ext_option;
static int jdb, advanced_gc = 1, debug_gc;
static int allow_multiple, skip_class_launcher, full_class_path;

static void parse_memory_from_java_options(int require)
{
	if (!megabytes) {
		const char *option = has_memory_option(&options.java_options);
		if (!option || prefixcmp(option, "-Xm") || !option[3]) {
			if (require)
				die ("Out of memory, could not determine heap size!");
			return;
		}
		megabytes = parse_memory(option + 4);
	}
}

static int handle_one_option2(int *i, int argc, const char **argv)
{
	if (!strcmp(argv[*i], "--dry-run"))
		options.dry_run++;
	else if (!strcmp(argv[*i], "--debug"))
		debug++;
	else if (handle_one_option(i, argv, "--java-home", &arg)) {
		set_java_home(xstrdup(arg.buffer));
		setenv_or_exit("JAVA_HOME", xstrdup(arg.buffer), 1);
	}
	else if (!strcmp(argv[*i], "--system"))
		options.use_system_jvm++;
	else if (!strcmp(argv[*i], "--set-icon")) {
		if (*i + 3 != argc)
			die("--set-icon requires two arguments: <exe-file> and <ico-file>");
#ifdef WIN32
		if (options.dry_run) {
			printf("Would set the icon of %s to %s.\n", argv[*i + 1], argv[*i + 2]);
			exit(0);
		}
		exit(set_exe_icon(argv[*i + 1], argv[*i + 2]));
#else
		die("Setting an .exe file's icon requires Windows!");
#endif
	}
	else if (!strcmp(argv[*i], "--console"))
#ifdef WIN32
		open_win_console();
#else
		; /* ignore */
#endif
	else if (!strcmp(argv[*i], "--jdb")) {
		add_tools_jar(&options);
		add_launcher_option(&options, "-jdb", NULL);
	}
	else if (!strcmp(argv[*i], "--allow-multiple"))
		allow_multiple = 1;
	else if (handle_one_option(i, argv, "--plugins", &arg))
		string_addf(&plugin_path, "-Dplugins.dir=%s", arg.buffer);
	else if (handle_one_option(i, argv, "--run", &arg)) {
		/* pass unparsed to ImageJ2 */
		if (!legacy_mode) {
			add_option(&options, strdup("--run"), 1);
			add_option_string(&options, &arg, 1);
			if (*i + 1 < argc)
				add_option(&options, strdup(argv[++(*i)]), 1);
			return 1;
		}
		string_replace(&arg, '_', ' ');
		if (*i + 1 < argc && argv[*i + 1][0] != '-')
			string_addf(&arg, "\", \"%s", argv[++(*i)]);
		add_option(&options, "-eval", 1);
		string_setf(&buffer, "run(\"%s\");", arg.buffer);
		add_option_string(&options, &buffer, 1);
		headless_argc++;
	}
	else if (handle_one_option(i, argv, "--compile-and-run", &arg)) {
		add_option(&options, "-eval", 1);
		string_setf(&buffer, "run(\"Refresh Javas\", \"%s \");",
			make_absolute_path(arg.buffer));
		add_option_string(&options, &buffer, 1);
		headless_argc++;
	}
	else if (*i == argc - 1 && !strcmp(argv[*i], "--edit")) {
		add_option(&options, "-eval", 1);
		add_option(&options, "run(\"Script Editor\");", 1);
	}
	else if (handle_one_option(i, argv, "--edit", &arg))
		for (;;) {
			add_option(&options, "-eval", 1);
			if (*arg.buffer && strncmp(arg.buffer, "class:", 6)) {
				string_set(&arg, make_absolute_path(arg.buffer));
				string_escape(&arg, "\\");
			}
			string_setf(&buffer, "run(\"Script Editor\", \"%s\");", arg.buffer);
			add_option_string(&options, &buffer, 1);
			if (*i + 1 >= argc)
				break;
			string_setf(&arg, "%s", argv[++(*i)]);
		}
	else if (handle_one_option(i, argv, "--heap", &arg) ||
			handle_one_option(i, argv, "--mem", &arg) ||
			handle_one_option(i, argv, "--memory", &arg))
		megabytes = parse_memory(arg.buffer);
	else if (!strcmp(argv[*i], "--headless"))
		headless = 1;
	else if (!strcmp(argv[*i], "-batch")) {
		batch = 1;
		return 0; /* Do not mark the argument as handled. */
	}
	else if (handle_one_option(i, argv, "--main-class", &arg)) {
		add_launcher_option(&options, "-classpath", ".");
		main_class = xstrdup(arg.buffer);
	}
	else if (handle_one_option(i, argv, "--jar", &arg)) {
		add_launcher_option(&options, "-classpath", arg.buffer);
		main_class = "imagej.JarLauncher";
		add_option_string(&options, &arg, 1);
	}
	else if (handle_one_option(i, argv, "--class-path", &arg) ||
			handle_one_option(i, argv, "--classpath", &arg) ||
			handle_one_option(i, argv, "-classpath", &arg) ||
			handle_one_option(i, argv, "--cp", &arg) ||
			handle_one_option(i, argv, "-cp", &arg))
		add_launcher_option(&options, "-classpath", arg.buffer);
	else if (handle_one_option(i, argv, "--fiji-jar", &arg) || handle_one_option(i, argv, "--ij-jar", &arg)) {
		const char *path = maybe_substitute_ij_jar(arg.buffer);
		if (path)
			add_launcher_option(&options, "-classpath", path);
		else
			add_launcher_option(&options, "-ijclasspath", arg.buffer);
	}
	else if (handle_one_option(i, argv, "--jar-path", &arg) ||
			handle_one_option(i, argv, "--jarpath", &arg) ||
			handle_one_option(i, argv, "-jarpath", &arg))
		add_launcher_option(&options, "-jarpath", arg.buffer);
	else if (!strcmp(argv[*i], "--full-classpath"))
		full_class_path = 1;
	else if (!strcmp(argv[*i], "--freeze-classloader"))
		add_launcher_option(&options, "-freeze-classloader", NULL);
	else if (handle_one_option(i, argv, "--ext", &arg)) {
		string_append_path_list(&ext_option, arg.buffer);
	}
	else if (!strcmp(argv[*i], "--ij2") || !strcmp(argv[*i], "--imagej"))
		main_class = default_main_class;
	else if (!strcmp(argv[*i], "--ij1") || !strcmp(argv[*i], "--legacy"))
		main_class = default_fiji1_class;
	else if (!strcmp(argv[*i], "--build") ||
			!strcmp(argv[*i], "--fake")) {
		const char *fake_jar;
#ifdef WIN32
		open_win_console();
#endif
		error("Fiji Build is deprecated! Please port your project to (Mini)Maven:\n"
			"\n\thttp://fiji.sc/Maven");
		skip_class_launcher = 1;
		headless = 1;
		fake_jar = ij_path("jars/fake.jar");
		string_set_length(&arg, 0);
		string_addf(&arg, "-Djava.class.path=%s", fake_jar);
		add_option_string(&options, &arg, 0);
		main_class = "fiji.build.Fake";
	}
	else if (!strcmp(argv[*i], "--tools-jar"))
		add_tools_jar(&options);
	else if (!strcmp(argv[*i], "--only-tools-jar")) {
		add_tools_jar(&options);
		add_launcher_option(&options, "-freeze-classloader", NULL);
	}
	else if (!strcmp(argv[*i], "--dont-patch-ij1"))
		add_option(&options, "-Dpatch.ij1=false", 0);
	else if (!strcmp(argv[*i], "--pass-classpath"))
		add_launcher_option(&options, "-pass-classpath", NULL);
	else if (!strcmp(argv[*i], "--retrotranslator") ||
			!strcmp(argv[*i], "--retro"))
		retrotranslator = 1;
	else if (handle_one_option(i, argv, "--fiji-dir", &arg))
		set_ij_dir(xstrdup(arg.buffer));
	else if (handle_one_option(i, argv, "--ij-dir", &arg))
		set_ij_dir(xstrdup(arg.buffer));
	else if (!strcmp("--print-ij-dir", argv[*i])) {
		printf("%s\n", get_ij_dir());
		exit(0);
	}
	else if (!strcmp("--print-java-home", argv[*i])) {
		const char *java_home = get_java_home();
		printf("%s\n", !java_home ? "" : java_home);
		exit(0);
	}
	else if (!strcmp("--default-gc", argv[*i]))
		advanced_gc = 0;
	else if (!strcmp("--gc-g1", argv[*i]) ||
			!strcmp("--g1", argv[*i]))
		advanced_gc = 2;
	else if (!strcmp("--debug-gc", argv[*i]))
		debug_gc = 1;
	else if (handle_one_option(i, argv, "--debugger", &arg)) {
		struct string *replace = string_copy("-agentlib:"
			"jdwp=transport=dt_socket,server=y,suspend=");
		if (suffixcmp(arg.buffer, arg.length, ",suspend")) {
			string_add_char(replace, 'n');
		} else {
			string_add_char(replace, 'y');
			string_set_length(&arg,
				arg.length - strlen(",suspend"));
		}
		string_append(replace, ",address=localhost:");
		string_replace_range(&arg, 0, 0, replace->buffer);
		string_release(replace);
		add_option_string(&options, &arg, 0);
	}
	else if (!strcmp("--no-splash", argv[*i]))
		disable_splash();
	else if (!strcmp("--help", argv[*i]) ||
			!strcmp("-h", argv[*i]))
		usage();
	else
		return 0;
	return 1;
}

static void handle_commandline(const char *line)
{
	int i, alloc = 32, argc = 0;
	char **argv = xmalloc(alloc * sizeof(char *));
	const char *current;

	current = line = skip_whitespace(line);
	if (!*current)
		return;

	while (*(++line))
		if (iswhitespace(*line)) {
			if (argc + 2 >= alloc) {
				alloc = (16 + alloc) * 3 / 2;
				argv = xrealloc(argv, alloc * sizeof(char *));
			}
			argv[argc++] = xstrndup(current, line - current);
			current = line = skip_whitespace(line + 1);
		}

	if (current != line)
		argv[argc++] = xstrndup(current, line - current);

	for (i = 0; i < argc; i++)
		if (!handle_one_option2(&i, argc, (const char **)argv))
			die("Unhandled option: %s", argv[i]);

	while (argc > 0)
		free(argv[--argc]);
	free(argv);
}

static void parse_command_line(void)
{
	struct string *jvm_options = string_init(32);
	struct string *default_arguments = string_init(32);
	struct string *java_library_path = string_init(32);
	struct string *library_base_path;
	int dashdash = 0;
	int count = 1, i;

#ifdef WIN32
#define EXE_EXTENSION ".exe"
#else
#define EXE_EXTENSION
#endif

#ifdef __linux__
	string_append_path_list(java_library_path, getenv("LD_LIBRARY_PATH"));
#endif
#ifdef __APPLE__
	string_append_path_list(java_library_path, getenv("DYLD_LIBRARY_PATH"));
#endif

	if (get_platform() != NULL) {
		struct string *buffer = string_initf("%s/%s", ij_path("lib"), get_platform());
		string_append_path_list(java_library_path, buffer->buffer);
		string_setf(buffer, "%s/%s", ij_path("mm"), get_platform());
		string_append_path_list(java_library_path, buffer->buffer);
		string_release(buffer);
	}

	library_base_path = string_copy(ij_path("lib"));
	detect_library_path(java_library_path, library_base_path);
	string_release(library_base_path);

#ifdef WIN32
	if (java_library_path->length) {
		struct string *new_path = string_initf("%s%s%s",
			getenv("PATH"), PATH_SEP, java_library_path->buffer);
		setenv("PATH", new_path->buffer, 1);
		string_release(new_path);
	}
#endif

	memset(&options, 0, sizeof(options));

#ifdef __APPLE__
	/* When double-clicked Finder adds a psn argument. */
	if (main_argc > 1 && !prefixcmp(main_argv[main_argc - 1], "-psn_")) {
		/*
		 * Reset main_argc so that ImageJ won't try to open
		 * that empty argument as a file (the root directory).
		 */
		main_argc--;
		/*
		 * Additionally, change directory to the ij dir to emulate
		 * the behavior of the regular ImageJ application which does
		 * not start up in the filesystem root.
		 */
		chdir(get_ij_dir());
	}

	if (!get_fiji_bundle_variable("heap", &arg) ||
			!get_fiji_bundle_variable("mem", &arg) ||
			!get_fiji_bundle_variable("memory", &arg)) {
		if (!strcmp("auto", arg.buffer))
			megabytes = 0;
		else
			megabytes = parse_memory(arg.buffer);
	}
	if (!get_fiji_bundle_variable("system", &arg) &&
			atol((&arg)->buffer) > 0)
		options.use_system_jvm++;
	if (get_fiji_bundle_variable("ext", &ext_option)) {
		string_set(&ext_option, ij_path("java/macosx-java3d/Home/lib/ext"));
		if (dir_exists(ext_option.buffer))
			string_add_char(&ext_option, ':');
		else
			string_set_length(&ext_option, 0);
		string_addf(&ext_option, "/Library/Java/Extensions:"
			"/System/Library/Java/Extensions:"
			"/System/Library/Frameworks/JavaVM.framework/Home/lib/ext");
	}
	if (!get_fiji_bundle_variable("allowMultiple", &arg))
		allow_multiple = parse_bool((&arg)->buffer);
	get_fiji_bundle_variable("JVMOptions", jvm_options);
	get_fiji_bundle_variable("DefaultArguments", default_arguments);
#else
	read_config(jvm_options);
#endif

	if (jvm_options->length)
		add_options(&options, jvm_options->buffer, 0);

	for (i = 1; i < main_argc; i++)
		if (!strcmp(main_argv[i], "--") && !dashdash)
			dashdash = count;
		else if (dashdash && main_class &&
				!is_default_ij1_class(main_class))
			main_argv[count++] = main_argv[i];
		else if (handle_one_option2(&i, main_argc, (const char **)main_argv))
			; /* ignore */
		else {
			const char *expanded = expand_subcommand(main_argv[i]);
			if (expanded)
				handle_commandline(expanded);
			else
				main_argv[count++] = main_argv[i];
		}

	main_argc = count;

#ifdef WIN32
	/* Windows automatically adds the path of the executable to PATH */
	const char *jre_home = get_jre_home();
	if (jre_home) {
		struct string *path = string_initf("%s;%s/bin",
			getenv("PATH"), jre_home);
		setenv_or_exit("PATH", path->buffer, 1);
		string_release(path);
	}
#endif
	if (!headless &&
#ifdef __APPLE__
			!CGSessionCopyCurrentDictionary()
#elif defined(__linux__)
			!getenv("DISPLAY")
#else
			0
#endif
			) {
		error("No GUI detected.  Falling back to headless mode.");
		headless = 1;
	}

	if (ext_option.length) {
		string_setf(&buffer, "-Djava.ext.dirs=%s", ext_option.buffer);
		add_option_string(&options, &buffer, 0);
	}

	/* Avoid Jython's huge startup cost: */
	add_option(&options, "-Dpython.cachedir.skip=true", 0);
	if (!plugin_path.length &&
			!has_plugins_dir_option(&options.java_options))
		string_setf(&plugin_path, "-Dplugins.dir=%s", get_ij_dir());
	if (plugin_path.length)
		add_option(&options, plugin_path.buffer, 0);

	if (legacy_ij1_options && is_default_ij1_class(main_class)) {
		struct options dummy;

		memset(&dummy, 0, sizeof(dummy));
		add_options(&dummy, legacy_ij1_options->buffer, 1);
		prepend_string_array(&options.ij_options, &dummy.ij_options);
		free(dummy.ij_options.list);
	}

	/* If arguments don't set the memory size, set it after available memory. */
	if (megabytes == 0 && !has_memory_option(&options.java_options)) {
		struct string *message = !debug ? NULL : string_init(32);
		megabytes = (long)(get_memory_size(0) >> 20);
		if (message)
			string_addf(message,"Available RAM: %dMB", (int)megabytes);
		/* 0.75x, but avoid multiplication to avoid overflow */
		megabytes -= megabytes >> 2;
		if (sizeof(void *) == 4 && megabytes > MAX_32BIT_HEAP) {
			if (message)
				string_addf(message,", using %dMB (maximum for 32-bit)", (int)MAX_32BIT_HEAP);
			megabytes = MAX_32BIT_HEAP;
		}
		else if (message)
			string_addf(message, ", using 3/4 of that: %dMB", (int)megabytes);
		if (message) {
			error("%s", message->buffer);
			string_release(message);
		}
	}
	if (sizeof(void *) < 8) {
		if (!megabytes)
			parse_memory_from_java_options(0);
		if (megabytes && megabytes > MAX_32BIT_HEAP)
			megabytes = MAX_32BIT_HEAP;
	}

	if (megabytes > 0)
		add_option(&options, make_memory_option(megabytes)->buffer, 0);

	if (headless) {
		add_option(&options, "-Djava.awt.headless=true", 0);
		add_option(&options, "-Dapple.awt.UIElement=true", 0);
	}

	if (is_ipv6_broken())
		add_option(&options, "-Djava.net.preferIPv4Stack=true", 0);

	jvm_workarounds(&options);

	if (advanced_gc == 1) {
		add_option(&options, "-Xincgc", 0);
		add_option(&options, "-XX:PermSize=128m", 0);
	}
	else if (advanced_gc == 2) {
		add_option(&options, "-XX:PermSize=128m", 0);
		add_option(&options, "-XX:+UseCompressedOops", 0);
		add_option(&options, "-XX:+UnlockExperimentalVMOptions", 0);
		add_option(&options, "-XX:+UseG1GC", 0);
		add_option(&options, "-XX:+G1ParallelRSetUpdatingEnabled", 0);
		add_option(&options, "-XX:+G1ParallelRSetScanningEnabled", 0);
		add_option(&options, "-XX:NewRatio=5", 0);
	}

	if (debug_gc)
		add_option(&options, "-verbose:gc", 0);

	if (!main_class) {
		int index = dashdash ? dashdash : 1;
		const char *first = main_argv[index];
		int len = main_argc > index ? strlen(first) : 0;
		const char *expanded;

		if (len > 1 && !strncmp(first, "--", 2))
			len = 0;
		if (len > 3 && (expanded = expand_subcommand_for_extension(get_file_extension(first))))
			handle_commandline(expanded);
		else if (len > 6 && !strcmp(first + len - 6, ".class")) {
			struct string *dotted = string_copy(first);
			add_launcher_option(&options, "-classpath", ".");
			string_replace(dotted, '/', '.');
			string_set_length(dotted, len - 6);
			main_class = xstrdup(dotted->buffer);
			main_argv++;
			main_argc--;
			string_release(dotted);
		}
		else
			main_class = legacy_mode ? default_fiji1_class : default_main_class;
	}

	maybe_reexec_with_correct_lib_path(java_library_path);

	if (!options.dry_run && !options.use_system_jvm && !headless && (is_default_ij1_class(main_class) || !strcmp(default_main_class, main_class)))
		show_splash(ij_launcher_jar);

	/* set up class path */
	if (full_class_path || !strcmp(default_main_class, main_class)) {
		add_launcher_option(&options, "-ijjarpath", "jars");
		add_launcher_option(&options, "-ijjarpath", "plugins");
	}
	else if (is_default_ij1_class(main_class)) {
		const char *jar_path = ij_path("jars/");
		char *ij1_jar = find_jar(jar_path, "ij");
		if (!ij1_jar)
			ij1_jar = find_jar(get_ij_dir(), "ij");
		if (!ij1_jar)
			die("Could not find ij.jar in %s", jar_path);
		add_launcher_option(&options, "-classpath", ij1_jar);
	}

	if (default_arguments->length)
		add_options(&options, default_arguments->buffer, 1);

	if (!strcmp(main_class, "org.apache.tools.ant.Main"))
		add_java_home_to_path();

	if (is_default_ij1_class(main_class)) {
		if (allow_multiple)
			add_option(&options, "-port0", 1);
		else if (!strcmp(default_fiji1_class, main_class))
			add_option(&options, "-port7", 1);
		add_option(&options, "-Dsun.java.command=ImageJ", 0);
	}

	/* If there is no -- but some options unknown to IJ1, DWIM it. */
	if (!dashdash && is_default_ij1_class(main_class)) {
		for (i = 1; i < main_argc; i++) {
			int count = imagej1_option_count(main_argv[i]);
			if (!count) {
				dashdash = main_argc;
				break;
			}
			i += count - 1;
		}
	}

	if (dashdash) {
		int is_imagej1 = is_default_ij1_class(main_class);

		for (i = 1; i < dashdash; ) {
			int count = is_imagej1 ? imagej1_option_count(main_argv[i]) : 0;
			if (!count)
				add_option(&options, main_argv[i++], 0);
			else
				while (count-- && i < dashdash)
					add_option(&options, main_argv[i++], 1);
		}
		main_argv += dashdash - 1;
		main_argc -= dashdash - 1;
	}

	/* handle "--headless script.ijm" gracefully */
	if (headless && is_default_ij1_class(main_class)) {
		if (main_argc + headless_argc < 2) {
			error("--headless without a parameter?");
			if (!options.dry_run)
				exit(1);
		}
		/* The -batch flag is required when --headless is given! */
		if (!batch) {
			batch = -1;
		}
	}

	if (jdb)
		add_launcher_option(&options, "-jdb", NULL);

	for (i = 1; i < main_argc; i++)
		add_option(&options, main_argv[i], 1);

	if (batch < 0) {
		if (debug)
			error("Appending missing -batch flag for headless operation.");
		add_option(&options, "-batch", 1);
	}

	i = 0;
	properties[i++] = "imagej.dir";
	properties[i++] = get_ij_dir(),
	properties[i++] = "ij.dir";
	properties[i++] = get_ij_dir(),
	properties[i++] = "fiji.dir";
	properties[i++] = get_ij_dir(),
	properties[i++] = "fiji.defaultLibPath";
	properties[i++] = get_default_library_path();
	properties[i++] = "fiji.executable";
	properties[i++] = main_argv0;
	properties[i++] = "ij.executable";
	properties[i++] = main_argv0;
	properties[i++] = "java.library.path";
	properties[i++] = java_library_path->buffer;
#ifdef WIN32
	properties[i++] = "sun.java2d.noddraw";
	properties[i++] = "true";
#endif
	if (debug) {
		properties[i++] = "ij.debug";
		properties[i++] = "true";
		properties[i++] = "scijava.log.level";
		properties[i++] = "debug";
	}
	properties[i++] = NULL;

	if (i > sizeof(properties) / sizeof(properties[0]))
		die ("Too many properties: %d", i);

	keep_only_one_memory_option(&options.java_options);

	if (ij_launcher_jar == NULL)
		skip_class_launcher = 1;

	if (!skip_class_launcher && strcmp(main_class, "org.apache.tools.ant.Main")) {
		struct string *string = string_initf("-Djava.class.path=%s", ij_launcher_jar);
		if (retrotranslator && !add_retrotranslator_to_path(string))
			retrotranslator = 0;
		add_option_string(&options, string, 0);
		add_launcher_option(&options, main_class, NULL);
		prepend_string_array(&options.ij_options, &options.launcher_options);
		startup_class = main_class;
		main_class = "imagej.ClassLauncher";
	}
	else {
		struct string *class_path = string_init(32);
		const char *sep = "-Djava.class.path=";
		int i;

		for (i = 0; i < options.launcher_options.nr; i++) {
			const char *option = options.launcher_options.list[i];
			if (sep)
				string_append(class_path, sep);
			if (!strcmp(option, "-ijclasspath"))
				string_append(class_path, ij_path(options.launcher_options.list[++i]));
			else if (!strcmp(option, "-classpath"))
				string_append(class_path, options.launcher_options.list[++i]);
			else
				die ("Without ij-launcher, '%s' cannot be handled", option);
			sep = PATH_SEP;
		}

		if (class_path->length)
			add_option_string(&options, class_path, 0);
		string_release(class_path);
	}

	if (retrotranslator) {
		prepend_string(&options.ij_options, strdup(main_class));
		prepend_string(&options.ij_options, "-advanced");
		main_class = "net.sf.retrotranslator.transformer.JITRetrotranslator";
	}

	if (options.dry_run || debug) {
		for (i = 0; properties[i]; i += 2) {
			if (!properties[i] || !properties[i + 1])
				continue;
			string_setf(&buffer, "-D%s=%s", properties[i], properties[i + 1]);
			add_option_string(&options, &buffer, 0);
		}

		show_commandline(&options);
		if (options.dry_run)
			exit(0);
	}

}

static void write_legacy_config(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f)
		error("Could not open '%s' for writing", path);
	else {
		const char *memory_option = has_memory_option(&options.java_options);
		fprintf(f, ".\n");
#ifdef WIN32
		fprintf(f, "jre\\bin\\javaw.exe\n");
#else
		fprintf(f, "jre/bin/java\n");
#endif
		fprintf(f, "%s -cp ij.jar ij.ImageJ\n", memory_option ? memory_option : "-Xmx640m");
		fclose(f);
	}
}

static void maybe_write_legacy_config(void)
{
#ifndef __APPLE__
	const char *path;

	if (!main_class || strcmp(main_class, legacy_ij1_class))
		return;

	path = ij_path("ImageJ.cfg");
	if (!file_exists(path))
		write_legacy_config(path);
#endif
}

static int write_desktop_file(const char *path, const char *title, const char *executable_path, const char *icon_path, const char *wm_class) {
	FILE *f = fopen(path, "w");

	if (!f) {
		if (debug)
			error("Could not write to '%s': %d (%s)", path, errno, strerror(errno));
		return 1;
	}

	fprintf(f, "[Desktop Entry]\n");
	fprintf(f, "Version=1.0\n");
	fprintf(f, "Name=%s\n", title);
	fprintf(f, "GenericName=%s\n", title);
	fprintf(f, "X-GNOME-FullName=%s\n", title);
	fprintf(f, "Comment=Scientific Image Analysis\n");
	fprintf(f, "Type=Application\n");
	fprintf(f, "Categories=Education;Science;ImageProcessing;\n");
	fprintf(f, "Exec=%s %%F\n", executable_path);
	fprintf(f, "TryExec=%s\n", executable_path);
	fprintf(f, "Terminal=false\n");
	fprintf(f, "StartupNotify=true\n");
	fprintf(f, "MimeType=image/*;\n");
	if (icon_path)
		fprintf(f, "Icon=%s\n", icon_path);
	if (wm_class)
		fprintf(f, "StartupWMClass=%s\n", wm_class);
	fclose(f);
#ifndef WIN32
	chmod(path, 0755);
#endif
}

static void maybe_write_desktop_file(void)
{
#if !defined(WIN32) && !defined(__APPLE__)
	const char *title, *name, *wm_class = NULL;
	struct string *path, *executable_path, *icon_path = NULL;

	if (!startup_class)
		startup_class = main_class;
	if (!startup_class)
		return;
	if (!strcmp("imagej.ClassLauncher", startup_class)) {
		if (debug)
			error("Could not determine startup class!");
		return;
	}
	if (!strcmp(startup_class, legacy_ij1_class)) {
		name = "ImageJ";
		title = "ImageJ";
		wm_class = "ij-ImageJ";
	}
	else if (!strcmp(startup_class, default_fiji1_class)) {
		name = "Fiji";
		title = "Fiji Is Just ImageJ";
		wm_class = "fiji-Main";
	}
	else if (!strcmp(startup_class, default_main_class)) {
		name = "ImageJ2";
		title = "ImageJ";
		wm_class = "imagej-ClassLauncher";
	}
	else
		return;

	path = string_initf("%s%s.desktop", ij_path(""), name);
	if (file_exists(path->buffer)) {
		if (debug)
			error("Keep existing '%s'", path->buffer);
		string_release(path);
		return;
	}

	if (last_slash(main_argv0))
		executable_path = string_copy(make_absolute_path(main_argv0));
	else {
		const char *in_path = find_in_path(main_argv0, 0);
		if (!in_path) {
			if (debug)
				error("Did not find '%s' in PATH, skipping %s\n", main_argv0, path->buffer);
			string_release(path);
			return;
		}
		executable_path = string_copy(in_path);
	}

	if (!icon_path) {
		const char *icon = ij_path("images/icon.png");
		if (icon && file_exists(icon))
			icon_path = string_copy(icon);
	}

	if (debug)
		error("Writing '%s'", path->buffer);
	write_desktop_file(path->buffer, title, executable_path->buffer, icon_path ? icon_path->buffer : NULL, wm_class);
	string_setf(path, "%s/.local/share/applications", getenv("HOME"));
	if (dir_exists(path->buffer)) {
		string_addf(path, "/%s.desktop", name);
		if (!file_exists(path->buffer)) {
			if (debug)
				error("Writing '%s'", path->buffer);
			write_desktop_file(path->buffer, title, executable_path->buffer, icon_path ? icon_path->buffer : NULL, wm_class);
		}
		else if (debug)
			error("Keep existing '%s'", path->buffer);
	}
	else if (debug)
		error("Skipping user-wide .desktop file: '%s' does not exist", path->buffer);

	string_release(path);
	string_release(executable_path);
	string_release(icon_path);
#endif
}

int start_ij(void)
{
	JavaVM *vm;
	JavaVMInitArgs args;
	JNIEnv *env;
	struct string *buffer = string_init(32);
	int i;

	memset(&args, 0, sizeof(args));
	/* JNI_VERSION_1_4 is used on Mac OS X to indicate 1.4.x and later */
	args.version = JNI_VERSION_1_4;
	args.options = prepare_java_options(&options.java_options);
	args.nOptions = options.java_options.nr;
	args.ignoreUnrecognized = JNI_FALSE;

	if (
#ifndef __APPLE__
			!get_jre_home() ||
#endif
			options.use_system_jvm) {
		fprintf(stderr, "Warning: falling back to system Java\n");
		env = NULL;
	}
	else {
		int result = create_java_vm(&vm, (void **)&env, &args);
		if (result == JNI_ENOMEM) {
			parse_memory_from_java_options(1);
			try_with_less_memory(megabytes);
			die("Out of memory!");
		}
		if (result) {
			if (result != 2) {
				fprintf(stderr, "Warning: falling back to System JVM\n");
				unsetenv("JAVA_HOME");
			}
			env = NULL;
		} else {
			const char *jre_home = get_jre_home();
			if (jre_home) {
				string_set(buffer, jre_home);
				if (!suffixcmp(buffer->buffer, buffer->length, "/"))
					string_set_length(buffer, buffer->length - 1);
				if (!suffixcmp(buffer->buffer, buffer->length, "/jre")) {
					// NB: It is technically incorrect to strip the jre suffix, since
					// according to Oracle's Java Tutorials, java.home should point to
					// the "Installation directory for Java Runtime Environment (JRE)".
					//
					// http://docs.oracle.com/javase/tutorial/essential/environment/sysprop.html
					//
					// However, we do this so that we can access JDK-only libraries
					// (particularly tools.jar) below java.home without resorting to
					// "${java.home}/.." or similar hacks.
					string_set_length(buffer, buffer->length - 4);
					string_replace_range(buffer, 0, 0, "-Djava.home=");
					if (debug)
						error("Adding option: %s", buffer->buffer);
					prepend_string_copy(&options.java_options, buffer->buffer);
				}
			}
		}
	}

	if (env) {
		jclass instance;
		jmethodID method;
		jobjectArray args;
		struct string *slashed = string_copy(main_class);

		for (i = 0; properties[i]; i += 2)
			set_property(env, properties[i], properties[i + 1]);

		string_replace(slashed, '.', '/');
		if (!(instance = (*env)->FindClass(env, slashed->buffer))) {
			(*env)->ExceptionDescribe(env);
			die("Could not find %s", slashed->buffer);
		}
		else if (!(method = (*env)->GetStaticMethodID(env, instance,
				"main", "([Ljava/lang/String;)V"))) {
			(*env)->ExceptionDescribe(env);
			die("Could not find main method of %s", slashed->buffer);
		}
		string_release(slashed);

		args = prepare_ij_options(env, &options.ij_options);
		(*env)->CallStaticVoidMethodA(env, instance,
				method, (jvalue *)&args);
		hide_splash();
		if ((*vm)->DetachCurrentThread(vm))
			error("Could not detach current thread");
		/* This does not return until ImageJ exits */
		(*vm)->DestroyJavaVM(vm);
	} else {
		/* fall back to system-wide Java */
		const char *java_home_env;
#ifdef __APPLE__
		struct string *icon_option;
		/*
		 * On MacOSX, one must (stupidly) fork() before exec() to
		 * clean up some pthread state somehow, otherwise the exec()
		 * will fail with "Operation not supported".
		 */
		if (fork())
			exit(0);

		add_option(&options, "-Xdock:name=ImageJ", 0);
		icon_option = string_copy("-Xdock:icon=");
		append_icon_path(icon_option, main_argv0);
		if (icon_option->length > 12)
			add_option_string(&options, icon_option, 0);
		string_release(icon_option);
#endif

		for (i = 0; properties[i]; i += 2) {
			if (!properties[i] || !properties[i + 1])
				continue;
			string_setf(buffer, "-D%s=%s", properties[i], properties[i + 1]);
			add_option_string(&options, buffer, 0);
		}

		/* fall back to system-wide Java */
		add_option_copy(&options, main_class, 0);
		append_string_array(&options.java_options, &options.ij_options);
		append_string(&options.java_options, NULL);
		prepend_string(&options.java_options, strdup(get_java_command()));

		string_set(buffer, get_java_command());
#ifdef WIN32
		string_append(buffer, ".exe");
#endif
		java_home_env = getenv("JAVA_HOME");
		if (java_home_env && strlen(java_home_env) > 0) {
			string_replace_range(buffer, 0, 0, "/bin/");
			string_replace_range(buffer, 0, 0, java_home_env);
#ifdef WIN32
			string_set(buffer, dos_path(buffer->buffer));
#endif
		}
		options.java_options.list[0] = buffer->buffer;
		hide_splash();
#ifndef WIN32
		if (execvp(buffer->buffer, options.java_options.list))
			error("Could not launch system-wide Java (%s)", strerror(errno));
#else
		if (console_opened && !console_attached)
			sleep(5); /* Sleep 5 seconds */

		STARTUPINFO startup_info;
		PROCESS_INFORMATION process_info;
		const char *java = file_exists(buffer->buffer) ? buffer->buffer :
			find_in_path(get_java_command(), 1);
		struct string *cmdline = string_initf("java");

		for (i = 0; i < options.java_options.nr - 1; i++)
			options.java_options.list[i] =
				quote_win32(options.java_options.list[i]);

		memset(&startup_info, 0, sizeof(startup_info));
		startup_info.cb = sizeof(startup_info);

		memset(&process_info, 0, sizeof(process_info));

		for (i = 1; i < options.java_options.nr - 1; i++)
			string_addf(cmdline, " %s", options.java_options.list[i]);
		if (CreateProcess(java, cmdline->buffer, NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS, NULL, NULL, &startup_info, &process_info)) {
			DWORD exit_code;
			WaitForSingleObject(process_info.hProcess, INFINITE);
			if (GetExitCodeProcess(process_info.hProcess, &exit_code) && exit_code)
				exit(exit_code);
			return 0;
		}

		char message[16384];
		int off = sprintf(message, "Error: '%s' while executing\n\n",
				strerror(errno));
		for (i = 0; options.java_options.list[i]; i++)
			off += sprintf(message + off, "'%s'\n",
					options.java_options.list[i]);
		MessageBox(NULL, message, "Error", MB_OK);
#endif
		exit(1);
	}
	return 0;
}

static void find_newest(struct string *relative_path, int max_depth, const char *file, struct string *result)
{
	int len = relative_path->length;
	DIR *directory;
	struct dirent *entry;

	string_add_char(relative_path, '/');

	string_append(relative_path, file);
	if (file_exists(ij_path(relative_path->buffer)) && is_native_library(ij_path(relative_path->buffer))) {
		string_set_length(relative_path, len);
		if (!result->length || file_is_newer(ij_path(relative_path->buffer), ij_path(result->buffer)))
			string_set(result, relative_path->buffer);
	}

	if (max_depth <= 0)
		return;

	string_set_length(relative_path, len);
	directory = opendir(ij_path(relative_path->buffer));
	if (!directory)
		return;
	string_add_char(relative_path, '/');
	while (NULL != (entry = readdir(directory))) {
		if (entry->d_name[0] == '.')
			continue;
		string_append(relative_path, entry->d_name);
		if (dir_exists(ij_path(relative_path->buffer)))
			find_newest(relative_path, max_depth - 1, file, result);
		string_set_length(relative_path, len + 1);
	}
	closedir(directory);
	string_set_length(relative_path, len);
}

/* TODO: try to find Java even if there is JRE local to ImageJ */
static void adjust_java_home_if_necessary(void)
{
	struct string *result, *buffer, *path;
	const char *prefix = "jre/";
	int depth = 2;

	set_default_library_path();
	set_library_path(get_default_library_path());

	buffer = string_copy("java");
	result = string_init(32);
	path = string_initf("%s%s", prefix, get_library_path());

	find_newest(buffer, depth, path->buffer, result);
	if (result->length) {
		if (result->buffer[result->length - 1] != '/')
			string_add_char(result, '/');
		string_append(result, prefix);
		set_relative_java_home(xstrdup(result->buffer));
	}
	else if (*prefix) {
		find_newest(buffer, depth + 1, get_library_path(), buffer);
		if (result->length)
			set_relative_java_home(xstrdup(result->buffer));
	}
	string_release(buffer);
	string_release(result);
	string_release(path);
}

int main(int argc, char **argv, char **e)
{
	int size;

	if (!suffixcmp(argv[0], -1, "debug.exe") ||
			!suffixcmp(argv[0], -1, "debug")) {
		debug++;
#ifdef WIN32
		open_win_console();
#endif
	}

	infer_ij_dir(argv[0]);

	/* Handle update/ */
	update_all_files();

#if defined(__APPLE__)
	launch_32bit_on_tiger(argc, argv);
#elif defined(WIN64)
	/* work around MinGW64 breakage */
	argc = __argc;
	argv = __argv;
	argv[0] = _pgmptr;
#endif
	adjust_java_home_if_necessary();
	main_argv0 = argv[0];
	main_argv = argv;
	main_argc = argc;

	/* save arguments in case we have to try with a smaller heap */
	size = (argc + 1) * sizeof(char *);
	main_argv_backup = (char **)xmalloc(size);
	memcpy(main_argv_backup, main_argv, size);
	main_argc_backup = argc;

	/* For now, launch Fiji1 when fiji-compat.jar was found */
	if (has_jar(ij_path("jars/"), "fiji-compat")) {
		if (debug)
			error("Detected Fiji");
		legacy_mode = 1;
	}
	/* If no ImageJ2 was found, try to fall back to ImageJ 1.x */
	else if (!has_jar(ij_path("jars/"), "ij-app")) {
		if (debug)
			error("Detected ImageJ 1.x");
		legacy_mode = 1;
		main_class = legacy_ij1_class;
	}
	else if (debug)
		error("Detected ImageJ2");

	initialize_ij_launcher_jar_path();
	parse_command_line();

	maybe_write_legacy_config();
	if (!debug)
		maybe_write_desktop_file();

#ifdef __APPLE__
	return start_ij_macosx(main_argv0);
#else
	return start_ij();
#endif
}
