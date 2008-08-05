#define _BSD_SOURCE
#include <stdlib.h>
#include "jni.h"
#include <stdlib.h>
#include <limits.h>
#include <iostream>
#include <string.h>
using std::cerr;
using std::endl;

#include <string>
using std::string;

#include <sstream>
using std::stringstream;

#include <fstream>
using std::ifstream;

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef MACOSX
#include <stdlib.h>
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>

static void append_icon_path(string &str);
static void set_path_to_JVM(void);
static int get_fiji_bundle_variable(const char *key, string &value);
#endif

#include <algorithm>
using std::replace;

#ifdef WIN32
#include <io.h>
#include <process.h>
#define PATH_SEP ";"
#else
#define PATH_SEP ":"
#endif

static const char *relative_java_home = JAVA_HOME;
static const char *library_path = JAVA_LIB_PATH;

/* Dynamic library loading stuff */

#ifdef WIN32
#include <windows.h>
#define RTLD_LAZY 0
static char *dlerror_value;

static void *dlopen(const char *name, int flags)
{
	void *result = LoadLibrary(name);
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
	dlerror_value = buffer;

	return result;
}

static char *dlerror(void)
{
	/* We need to reset the error */
	char *result = dlerror_value;
	dlerror_value = NULL;
	return result;
}

static void *dlsym(void *handle, const char *name)
{
	void *result = (void *)GetProcAddress((HMODULE)handle, name);
	dlerror_value = result ? NULL : (char *)"function not found";
	return result;
}

static void sleep(int seconds)
{
	Sleep(seconds * 1000);
}
#else
#include <dlfcn.h>
#endif



/* Determining heap size */

#ifdef MACOSX
#include <mach/mach_init.h>
#include <mach/mach_host.h>

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
#elif defined(linux)
size_t get_memory_size(int available_only)
{
	ssize_t page_size = sysconf(_SC_PAGESIZE);
	ssize_t available_pages = sysconf(available_only ?
			_SC_AVPHYS_PAGES : _SC_PHYS_PAGES);
	return page_size < 0 || available_pages < 0 ?
		0 : (size_t)page_size * (size_t)available_pages;
}
#elif defined(WIN32)
#include <windows.h>

size_t get_memory_size(int available_only)
{
	MEMORYSTATUS status;

	GlobalMemoryStatus(&status);
	return available_only ? status.dwAvailPhys : status.dwTotalPhys;
}
#else
size_t get_memory_size(int available_only)
{
	fprintf(stderr, "Unsupported\n");
	return 0;
}
#endif

static long long parse_memory(const char *amount)
{
	char *endp;
	long long result = strtoll(amount, &endp, 0);

	if (endp)
		switch (*endp) {
		case 't': case 'T':
			result <<= 10;
			/* fall through */
		case 'g': case 'G':
			result <<= 10;
			/* fall through */
		case 'm': case 'M':
			result <<= 10;
			/* fall through */
		case 'k': case 'K':
			result <<= 10;
			break;
		case '\0':
			/* fall back to megabyte */
			if (result < 1024)
				result <<= 20;
		}

	return result;
}

static bool parse_bool(string &value)
{
	return value != "0" && value != "false" &&
		value != "False" && value != "FALSE";
}



/* Java stuff */

#ifndef JNI_CREATEVM
#define JNI_CREATEVM "JNI_CreateJavaVM"
#endif

const char *fiji_dir;
char **main_argv;
int main_argc;
const char *main_class;
bool run_precompiled = false;

static size_t mystrlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

char *last_slash(const char *path)
{
	char *slash = strrchr(path, '/');
#ifdef WIN32
	char *backslash = strrchr(path, '\\');

	if (backslash && slash < backslash)
		slash = backslash;
#endif
	return slash;
}

static const char *make_absolute_path(const char *path)
{
	static char bufs[2][PATH_MAX + 1], *buf = bufs[0], *next_buf = bufs[1];
	char cwd[1024] = "";
	int buf_index = 1, len;

	int depth = 20;
	char *last_elem = NULL;
	struct stat st;

	if (mystrlcpy(buf, path, PATH_MAX) >= PATH_MAX) {
		cerr << "Too long path: " << path << endl;
		exit(1);
	}

	while (depth--) {
		if (stat(buf, &st) || !S_ISDIR(st.st_mode)) {
			char *slash = last_slash(buf);
			if (slash) {
				*slash = '\0';
				last_elem = strdup(slash + 1);
			} else {
				last_elem = strdup(buf);
				*buf = '\0';
			}
		}

		if (*buf) {
			if (!*cwd && !getcwd(cwd, sizeof(cwd))) {
				cerr << "Could not get current working dir"
					<< endl;
				exit(1);
			}

			if (chdir(buf)) {
				cerr << "Could not switch to " << buf << endl;
				exit(1);
			}
		}
		if (!getcwd(buf, PATH_MAX)) {
			cerr << "Could not get current working directory"
				<< endl;
			exit(1);
		}

		if (last_elem) {
			int len = strlen(buf);
			if (len + strlen(last_elem) + 2 > PATH_MAX) {
				cerr << "Too long path name: "
					<< buf << "/" << last_elem << endl;
				exit(1);
			}
			buf[len] = '/';
			strcpy(buf + len + 1, last_elem);
			free(last_elem);
			last_elem = NULL;
		}

#ifndef WIN32
		if (!lstat(buf, &st) && S_ISLNK(st.st_mode)) {
			len = readlink(buf, next_buf, PATH_MAX);
			if (len < 0) {
				cerr << "Invalid symlink: " << buf << endl;
				exit(1);
			}
			next_buf[len] = '\0';
			buf = next_buf;
			buf_index = 1 - buf_index;
			next_buf = bufs[buf_index];
		} else
#endif
			break;
	}

	if (*cwd && chdir(cwd)) {
		cerr << "Could not change back to " << cwd << endl;
		exit(1);
	}

	return buf;
}

static bool is_absolute_path(const char *path)
{
#ifdef WIN32
	if (((path[0] >= 'A' && path[0] <= 'Z') ||
			(path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
		return true;
#endif
	return path[0] == '/';
}

static string find_in_path(const char *path)
{
	const char *p = getenv("PATH");

	if (!p) {
		cerr << "Could not get PATH" << endl;
		exit(1);
	}

	for (;;) {
		const char *colon = strchr(p, ':'), *orig_p = p;
		int len = colon ? colon - p : strlen(p);
		struct stat st;
		char buffer[PATH_MAX];

		if (!len) {
			cerr << "Could not find " << path << " in PATH" << endl;
			exit(1);
		}

		p += len + !!colon;
		if (!is_absolute_path(orig_p))
			continue;
		snprintf(buffer, sizeof(buffer), "%.*s/%s", len, orig_p, path);
#ifdef WIN32
#define S_IX S_IXUSR
#else
#define S_IX (S_IXUSR | S_IXGRP | S_IXOTH)
#endif
		if (!stat(buffer, &st) && S_ISREG(st.st_mode) &&
				(st.st_mode & S_IX))
			return make_absolute_path(buffer);
	}
}

static inline int suffixcmp(const char *string, int len, const char *suffix)
{
	int suffix_len = strlen(suffix);
	if (len < suffix_len)
		return -1;
	return strncmp(string + len - suffix_len, suffix, suffix_len);
}

static const char *get_fiji_dir(const char *argv0)
{
	static string buffer;

	if (buffer != "")
		return buffer.c_str();

	if (!last_slash(argv0))
		buffer = find_in_path(argv0);
	else
		buffer = make_absolute_path(argv0);
	argv0 = buffer.c_str();

	const char *slash = last_slash(argv0);
	if (!slash) {
		cerr << "Could not get absolute path for executable" << endl;
		exit(1);
	}

	int len = slash - argv0;
	if (!suffixcmp(argv0, len, "/precompiled") ||
			!suffixcmp(argv0, len, "\\precompiled")) {
		slash -= strlen("/precompiled");
		run_precompiled = true;
	}
#ifdef MACOSX
	else if (!suffixcmp(argv0, len, "/Fiji.app/Contents/MacOS"))
		slash -= strlen("/Contents/MacOS");
#endif

	buffer = buffer.substr(0, slash - argv0);
	return buffer.c_str();
}

static int create_java_vm(JavaVM **vm, void **env, JavaVMInitArgs *args)
{
#ifdef MACOSX
	set_path_to_JVM();
#else
	stringstream java_home, buffer;
	void *handle;
	char *err;
	static jint (*JNI_CreateJavaVM)(JavaVM **pvm, void **penv, void *args);

	java_home << fiji_dir << "/" << relative_java_home;
#ifdef WIN32
	/* support Windows with its ridiculously anachronistic putenv() */
	stringstream java_home_env;
	java_home_env << "JAVA_HOME=" << java_home.str();
	putenv(strdup(java_home_env.str().c_str()));
	/* Windows automatically adds the path of the executable to PATH */
	stringstream path;
	path << "PATH=" << getenv("PATH") << ";" << java_home.str() << "/bin";
	putenv(strdup(path.str().c_str()));
#else
	setenv("JAVA_HOME", java_home.str().c_str(), 1);
#endif
	buffer << java_home.str() << "/" << library_path;

	handle = dlopen(buffer.str().c_str(), RTLD_LAZY);
	if (!handle) {
		const char *error = dlerror();
		if (!error)
			error = "(unknown error)";
		cerr << "Could not load Java library '" <<
			buffer.str() << "': " << error << endl;
		return 1;
	}
	dlerror(); /* Clear any existing error */

	JNI_CreateJavaVM = (typeof(JNI_CreateJavaVM))dlsym(handle,
			JNI_CREATEVM);
	err = dlerror();
	if (err) {
		cerr << "Error loading libjvm: " << err << endl;
		return 1;
	}
#endif

	return JNI_CreateJavaVM(vm, env, args);
}

#ifdef WIN32
struct entry {
	char d_name[PATH_MAX];
	int d_namlen;
} entry;

struct dir {
	string pattern;
	HANDLE handle;
	WIN32_FIND_DATA find_data;
	int done;
	struct entry entry;
};

struct dir *open_dir(const char *path)
{
	struct dir *result = new dir();
	if (!result)
		return result;
	result->pattern = path;
	result->pattern += "/*";
	result->handle = FindFirstFile(result->pattern.c_str(),
			&(result->find_data));
	if (result->handle == INVALID_HANDLE_VALUE) {
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
	FindClose(dir->handle);
	delete dir;
	return 0;
}

#define DIR struct dir
#define dirent entry
#define opendir open_dir
#define readdir read_dir
#define closedir close_dir
#else
#include <dirent.h>
#endif

static int headless;

int build_classpath(string &result, string jar_directory, int no_error) {
	DIR *directory = opendir(jar_directory.c_str());
	if (!directory) {
		if (no_error)
			return 0;
		cerr << "Failed to open: " << jar_directory << endl;
		return 1;
	}
	string extension(".jar");
	unsigned int extension_length = extension.size();
	struct dirent *entry;
	while (NULL != (entry = readdir(directory))) {
		string filename(entry->d_name);
		unsigned int n = filename.size();
		if (n <= extension_length)
			continue;
		unsigned int extension_start = n - extension_length;
		if (!filename.compare(extension_start,
					extension_length,
					extension))
			result += PATH_SEP + jar_directory + "/" + filename;
		else {
			if (filename != "." && filename != ".." &&
					build_classpath(result, jar_directory
						+ "/" + filename, 1))
				return 1;
			continue;
		}

	}
	return 0;
}

struct string_array {
	char **list;
	int nr, alloc;
};

static void append_string(struct string_array& array, char *str)
{
	if (array.nr >= array.alloc) {
		array.alloc = 2 * array.nr + 16;
		array.list = (char **)realloc(array.list,
				array.alloc * sizeof(str));
	}
	array.list[array.nr++] = str;
}

static void prepend_string(struct string_array& array, char *str)
{
	if (array.nr >= array.alloc) {
		array.alloc = 2 * array.nr + 16;
		array.list = (char **)realloc(array.list,
				array.alloc * sizeof(str));
	}
	memmove(array.list + 1, array.list, array.nr * sizeof(str));
	array.list[0] = str;
	array.nr++;
}

static void prepend_string(struct string_array& array, const char *str)
{
	prepend_string(array, strdup(str));
}

static void append_string_array(struct string_array& target,
		struct string_array &source)
{
	if (target.alloc - target.nr < source.nr) {
		target.alloc += source.nr;
		target.list = (char **)realloc(target.list,
				target.alloc * sizeof(target.list[0]));
	}
	memcpy(target.list + target.nr, source.list,
			source.nr * sizeof(target.list[0]));
	target.nr += source.nr;
}

static JavaVMOption *prepare_java_options(struct string_array& array)
{
	JavaVMOption *result = (JavaVMOption *)calloc(array.nr,
			sizeof(JavaVMOption));

	for (int i = 0; i < array.nr; i++)
		result[i].optionString = array.list[i];

	return result;
}

static jobjectArray prepare_ij_options(JNIEnv *env, struct string_array& array)
{
	jstring jstr;
	jobjectArray result;

	if (!(jstr = env->NewStringUTF(array.nr ? array.list[0] : ""))) {
fail:
		cerr << "Failed to create ImageJ option array" << endl;
		exit(1);
	}

	result = env->NewObjectArray(array.nr,
			env->FindClass("java/lang/String"), jstr);
	if (!result)
		goto fail;
	for (int i = 1; i < array.nr; i++) {
		if (!(jstr = env->NewStringUTF(array.list[i])))
			goto fail;
		env->SetObjectArrayElement(result, i, jstr);
	}
	return result;
}

struct options {
	struct string_array java_options, ij_options;
	int debug, use_system_jvm;
};

static void add_option(struct options& options, char *option, int for_ij)
{
	append_string(for_ij ?
			options.ij_options : options.java_options,
			option);
}

static void add_option(struct options& options, const char *option, int for_ij)
{
	add_option(options, strdup(option), for_ij);
}

static void add_option(struct options& options, string &option, int for_ij)
{
	add_option(options, option.c_str(), for_ij);
}

static void add_option(struct options& options, stringstream &option, int for_ij)
{
	add_option(options, option.str().c_str(), for_ij);
}

static bool is_quote(char c)
{
	return c == '\'' || c == '"';
}

static int find_closing_quote(string s, char quote, int index, int len)
{
	for (int i = index; i < len; i++) {
		char c = s[i];
		if (c == quote)
			return i;
		if (is_quote(c))
			i = find_closing_quote(s, c, i + 1, len);
	}
	cerr << "Unclosed quote: " << s << endl << "               ";
	for (int i = 0; i < index; i++)
		cerr << " ";
	cerr << "^" << endl;
	exit(1);
}

static void add_options(struct options &options, string &cmd_line, int for_ij)
{
	int len = cmd_line.length();
	string current = "";

	for (int i = 0; i < len; i++) {
		char c = cmd_line[i];
		if (is_quote(c)) {
			int i2 = find_closing_quote(cmd_line, c, i + 1, len);
			current += cmd_line.substr(i + 1, i2 - i - 1);
			i = i2;
			continue;
		}
		if (c == ' ' || c == '\t' || c == '\n') {
			if (current == "")
				continue;
			add_option(options, current, for_ij);
			current = "";
		} else
			current += c;
	}
	if (current != "")
		add_option(options, current, for_ij);
}

static int read_file_as_string(string file_name, string &contents)
{
	char buffer[1024];
	ifstream in(file_name.c_str());
	while (in.good()) {
		in.get(buffer, sizeof(buffer));
		contents += buffer;
	}
	in.close();
}

static string quote_if_necessary(const char *option)
{
	string result = "";
	for (; *option; option++)
		switch (*option) {
		case '\n':
			result += "\\n";
			break;
		case '\t':
			result += "\\t";
			break;
		case ' ': case '"': case '\\':
			result += "\\";
			/* fallthru */
		default:
			result += *option;
			break;
		}
	return result;
}

static void show_commandline(struct options& options)
{
	cerr << "java";
	for (int j = 0; j < options.java_options.nr; j++)
		cerr << " " << quote_if_necessary(options.java_options.list[j]);
	cerr << " " << main_class;
	for (int j = 0; j < options.ij_options.nr; j++)
		cerr << " " << quote_if_necessary(options.ij_options.list[j]);
	cerr << endl;
}

bool file_exists(string path)
{
	ifstream test(path.c_str());
	if (!test.is_open())
		return false;
	test.close();
	return true;
}

bool handle_one_option(int &i, const char *option, string &arg)
{
	if (!strcmp(main_argv[i], option)) {
		if (++i >= main_argc || !main_argv[i]) {
			cerr << "Option " << option << " needs an argument!"
				<< endl;
			exit(1);
		}
		arg = main_argv[i];
		return true;
	}
	int len = strlen(option);
	if (!strncmp(main_argv[i], option, len) && main_argv[i][len] == '=') {
		arg = main_argv[i] + len + 1;
		return true;
	}
	return false;
}

static void /* no-return */ usage(void)
{
	cerr << "Usage: " << main_argv[0] << " [<Java options>.. --] "
		"[<ImageJ options>..] [<files>..]" << endl
		<< endl
		<< "Java options are passed to the Java Runtime, ImageJ" << endl
		<< "options to ImageJ (or Jython, JRuby, ...)." << endl
		<< endl
		<< "In addition, the following options are supported by Fiji:"
			<< endl
		<< "General options:" << endl
		<< "--help, -h" << endl
		<< "\tshow this help" << endl
		<< "--dry-run" << endl
		<< "\tshow the command line, but do not run anything" << endl
		<< "--system" << endl
		<< "\tdo not try to run bundled Java" << endl
		<< "--headless" << endl
		<< "\trun in text mode" << endl
		<< "--fiji-dir <path>" << endl
		<< "\tset the fiji directory to <path> (used to find" << endl
		<< "\t jars/, plugins/ and macros/)" << endl
		<< "--heap, --mem, --memory <amount>" << endl
		<< "\tset Java's heap size to <amount> (e.g. 512M)" << endl
		<< "--class-path, --classpath, -classpath, --cp, -cp <path>"
			<< endl
		<< "\tappend <path> to the class path" << endl
		<< "--ext <path>" << endl
		<< "\tset Java's extension directory to <path>" << endl
		<< endl
		<< "Options for ImageJ:" << endl
		<< "--allow-multiple" << endl
		<< "\tdo not reuse existing ImageJ instance" << endl
		<< "--plugins <dir>" << endl
		<< "\tuse <dir> to discover plugins" << endl
		<< endl
		<< "Options to run programs other than ImageJ:" << endl
		<< "--jdb" << endl
		<< "\tstart in JDB, the Java debugger" << endl
		<< "--jython" << endl
		<< "\tstart Jython instead of ImageJ (this is the" << endl
		<< "\tdefault when called with a file ending in .py)" << endl
		<< "--jruby" << endl
		<< "\tstart JRuby instead of ImageJ (this is the" << endl
		<< "\tdefault when called with a file ending in .rb)" << endl
		<< "--main-class <class name>" << endl
		<< "\tstart the given class instead of ImageJ" << endl
		<< "--fake" << endl
		<< "\tstart Fake instead of ImageJ" << endl
		<< "--javac" << endl
		<< "\tstart JavaC, the Java Compiler, instead of ImagJ" << endl
		<< endl;
	exit(1);
}

/* the maximal size of the heap on 32-bit systems, in megabyte */
#ifdef WIN32
#define MAX_32BIT_HEAP 1638
#else
#define MAX_32BIT_HEAP 1920
#endif

static int start_ij(void)
{
	JavaVM *vm;
	struct options options;
	JavaVMInitArgs args;
	JNIEnv *env;
	string class_path, ext_option, jvm_options, arg;
	stringstream plugin_path;
	int dashdash = 0;
	bool allow_multiple = false, skip_build_classpath = false;
	bool jdb = false, add_class_path_option = false;

	size_t memory_size = 0;

	memset(&options, 0, sizeof(options));

#ifdef MACOSX
	string value;
	if (!get_fiji_bundle_variable("heap", value) ||
			!get_fiji_bundle_variable("mem", value) ||
			!get_fiji_bundle_variable("memory", value))
		memory_size = parse_memory(value.c_str());
	if (!get_fiji_bundle_variable("system", value) &&
			atol(value.c_str()) > 0)
		options.use_system_jvm++;
	if (get_fiji_bundle_variable("ext", ext_option))
		ext_option = string(fiji_dir)
			+ "/" + relative_java_home + "/Home/lib/ext:"
			"/Library/Java/Extensions:"
			"/System/Library/Java/Extensions:"
			"/System/Library/Frameworks/JavaVM.framework";
	if (!get_fiji_bundle_variable("allowMultiple", value))
		allow_multiple = parse_bool(value);
	get_fiji_bundle_variable("JVMOptions", jvm_options);
#else
	read_file_as_string(string(fiji_dir) + "/jvm.cfg", jvm_options);
#endif

	int count = 1;
	for (int i = 1; i < main_argc; i++)
		if (!strcmp(main_argv[i], "--") && !dashdash)
			dashdash = count;
		else if (!strcmp(main_argv[i], "--dry-run"))
			options.debug++;
		else if (!strcmp(main_argv[i], "--system"))
			options.use_system_jvm++;
		else if (!strcmp(main_argv[i], "--jdb")) {
			add_class_path_option = true;
			jdb = true;
		}
		else if (!strcmp(main_argv[i], "--allow-multiple"))
			allow_multiple = true;
		else if (handle_one_option(i, "--plugins", arg))
			plugin_path << "-Dplugins.dir=" << arg;
		else if (handle_one_option(i, "--heap", arg) ||
				handle_one_option(i, "--mem", arg) ||
				handle_one_option(i, "--memory", arg))
			memory_size = parse_memory(arg.c_str());
		else if (!strcmp(main_argv[i], "--headless")) {
			headless = 1;
			/* handle "--headless script.ijm" gracefully */
			if (i + 2 == main_argc && main_argv[i + 1][0] != '-')
				dashdash = count;
		}
		else if (!strcmp(main_argv[i], "--jython"))
			main_class = "org.python.util.jython";
		else if (!strcmp(main_argv[i], "--jruby"))
			main_class = "org.jruby.Main";
		else if (handle_one_option(i, "--main-class", arg)) {
			class_path += "." PATH_SEP;
			main_class = strdup(arg.c_str());
		}
		else if (handle_one_option(i, "--class-path", arg) ||
				handle_one_option(i, "--classpath", arg) ||
				handle_one_option(i, "-classpath", arg) ||
				handle_one_option(i, "--cp", arg) ||
				handle_one_option(i, "-cp", arg))
			class_path += arg + PATH_SEP;
		else if (handle_one_option(i, "--ext", arg)) {
			if (ext_option != "")
				ext_option += PATH_SEP;
			ext_option += arg;
		}
		else if (!strcmp(main_argv[i], "--fake")) {
			skip_build_classpath = true;
			headless = 1;
			class_path += fiji_dir;
			if (run_precompiled || !file_exists(string(fiji_dir)
						+ "/fake.jar"))
				class_path += "/precompiled";
			class_path += "/fake.jar" PATH_SEP;
			main_class = "Fake";
		}
		else if (!strcmp(main_argv[i], "--javac")) {
			add_class_path_option = true;
			headless = 1;
			class_path += fiji_dir;
			if (run_precompiled || !file_exists(string(fiji_dir)
						+ "/jars/javac.jar"))
				class_path += "/precompiled";
			else
				class_path += "/jars";
			class_path += "/javac.jar" PATH_SEP;
			main_class = "com.sun.tools.javac.Main";
		}
		else if (handle_one_option(i, "--fiji-dir", arg))
			fiji_dir = strdup(arg.c_str());
		else if (!strcmp("--help", main_argv[i]) ||
				!strcmp("-h", main_argv[i]))
			usage();
		else {
			int len = strlen(main_argv[i]);
			if (len > 6 && !strcmp(main_argv[i]
						+ len - 6, ".class")) {
				class_path += "." PATH_SEP;
				string dotted = main_argv[i];
				replace(dotted.begin(), dotted.end(), '/', '.');
				dotted = dotted.substr(0, len - 6);
				main_class = strdup(dotted.c_str());
			}
			else
				main_argv[count++] = main_argv[i];
		}
	main_argc = count;

	if (!headless &&
#ifdef MACOSX
			!getenv("SECURITYSESSIONID") && !getenv("DISPLAY")
#elif defined(__linux__)
			!getenv("DISPLAY")
#else
			false
#endif
			) {
		cerr << "No GUI detected.  Falling back to headless mode."
			<< endl;
		headless = 1;
	}

	if (ext_option != "") {
		ext_option = string("-Djava.ext.dirs=") + ext_option;
		add_option(options, ext_option, 0);
	}

	/* For Jython 2.2.1 to work properly with .jar packages: */
	add_option(options, "-Dpython.cachedir.skip=false", 0);

	class_path = "-Djava.class.path=" + class_path;
	if (skip_build_classpath) {
		/* strip trailing ":" */
		int len = class_path.length();
		if (class_path[len - 1] == PATH_SEP[0])
			class_path = class_path.substr(0, len - 1);
	}
	else {
		if (headless)
			class_path += string(fiji_dir) + "/misc/headless.jar"
				+ PATH_SEP;
		class_path += fiji_dir;
		class_path += "/misc/Fiji.jar";
		class_path += PATH_SEP;
		class_path += fiji_dir;
		class_path += "/ij.jar";

		if (build_classpath(class_path,
					string(fiji_dir) + "/plugins", 0))
			return 1;
		if (build_classpath(class_path, string(fiji_dir) + "/jars", 0))
			return 1;
	}
	add_option(options, class_path, 0);

	if (plugin_path.str() == "")
		plugin_path << "-Dplugins.dir=" << fiji_dir;
	add_option(options, plugin_path, 0);

	// if arguments don't set the memory size, set it after available memory
	if (memory_size == 0) {
		memory_size = get_memory_size(0);
		/* 0.75x, but avoid multiplication to avoid overflow */
		memory_size -= memory_size >> 2;
	}

	if (memory_size > 0) {
		memory_size >>= 20;
		if (sizeof(void *) == 4 && memory_size > MAX_32BIT_HEAP)
			memory_size = MAX_32BIT_HEAP;
		stringstream heap_size;
		heap_size << "-Xmx"<< memory_size << "m";
		add_option(options, heap_size, 0);
	}

	if (headless)
		add_option(options, "-Djava.awt.headless=true", 0);

	if (jvm_options != "")
		add_options(options, jvm_options, 0);

	if (dashdash) {
		for (int i = 1; i < dashdash; i++)
			add_option(options, main_argv[i], 0);
		main_argv += dashdash - 1;
		main_argc -= dashdash - 1;
	}

	if (!main_class) {
		const char *first = main_argv[1];
		int len = main_argc > 1 ? strlen(first) : 0;

		if (len > 1 && !strncmp(first, "--", 2))
			len = 0;
		if (len > 3 && !strcmp(first + len - 3, ".py"))
			main_class = "org.python.util.jython";
		else if (len > 3 && !strcmp(first + len - 3, ".rb"))
			main_class = "org.jruby.Main";
		else
			main_class = "ij.ImageJ";
	}

	if (add_class_path_option) {
		add_option(options, "-classpath", 1);
		add_option(options, class_path.substr(18).c_str(), 1);
	}

	if (jdb) {
		add_option(options, main_class, 1);
		main_class = "com.sun.tools.example.debug.tty.TTY";
	}

	if (allow_multiple && !strcmp(main_class, "ij.ImageJ"))
		add_option(options, "-port0", 1);

	if (!strcmp(main_class, "ij.ImageJ")) {
		stringstream icon_option;
		icon_option << "-icon=" << fiji_dir << "/images/icon.png";
		add_option(options, icon_option, 1);
		add_option(options, "-title=Fiji", 1);
	}

	/* handle "--headless script.ijm" gracefully */
	if (headless && !strcmp(main_class, "ij.ImageJ")) {
		if (main_argc < 2) {
			cerr << "--headless without a parameter?" << endl;
			if (!options.debug)
				exit(1);
		}
		if (main_argc > 1 && *main_argv[1] != '-')
			add_option(options, "-batch", 1);
	}

	for (int i = 1; i < main_argc; i++)
		add_option(options, main_argv[i], 1);

	if (options.debug) {
		show_commandline(options);
		exit(0);
	}

	memset(&args, 0, sizeof(args));
	/* JNI_VERSION_1_4 is used on Mac OS X to indicate 1.4.x and later */
	args.version = JNI_VERSION_1_4;
	args.options = prepare_java_options(options.java_options);
	args.nOptions = options.java_options.nr;
	args.ignoreUnrecognized = JNI_FALSE;

	if (options.use_system_jvm)
		env = NULL;
	else if (create_java_vm(&vm, (void **)&env, &args)) {
		cerr << "Warning: falling back to System JVM" << endl;
		env = NULL;
	} else {
		stringstream java_home_path;
		java_home_path << "-Djava.home=" << fiji_dir << "/"
			<< relative_java_home;
		prepend_string(options.java_options,
			java_home_path.str().c_str());
	}

	if (env) {
		jclass instance;
		jmethodID method;
		jobjectArray args;

		string slashed(main_class);
		replace(slashed.begin(), slashed.end(), '.', '/');
		if (!(instance = env->FindClass(slashed.c_str()))) {
			cerr << "Could not find " << main_class << endl;
			exit(1);
		} else if (!(method = env->GetStaticMethodID(instance,
				"main", "([Ljava/lang/String;)V"))) {
			cerr << "Could not find main method" << endl;
			exit(1);
		}

		args = prepare_ij_options(env, options.ij_options);
		env->CallStaticVoidMethodA(instance,
				method, (jvalue *)&args);
		if (vm->DetachCurrentThread())
			cerr << "Could not detach current thread"
				<< endl;
		/* This does not return until ImageJ exits */
		vm->DestroyJavaVM();
	} else {
		/* fall back to system-wide Java */
#ifdef MACOSX
		/*
		 * On MacOSX, one must (stupidly) fork() before exec() to
		 * clean up some pthread state somehow, otherwise the exec()
		 * will fail with "Operation not supported".
		 */
		if (fork())
			exit(0);

		add_option(options, "-Xdock:name=Fiji", 0);
		string icon_option = "-Xdock:icon=";
		append_icon_path(icon_option);
		add_option(options, icon_option, 0);
#endif

		/* fall back to system-wide Java */
		add_option(options, main_class, 0);
		append_string_array(options.java_options, options.ij_options);
		append_string(options.java_options, NULL);
		prepend_string(options.java_options, "java");

		if (execvp("java", options.java_options.list))
			cerr << "Could not launch system-wide Java" << endl;
		exit(1);
	}
	return 0;
}

#ifdef MACOSX
static void append_icon_path(string &str)
{
	str += fiji_dir;
	/*
	 * Check if we're launched from within an Application bundle or
	 * command line.  If from a bundle, Fiji.app should be in the path.
	 */
	if (!suffixcmp(fiji_dir, strlen(fiji_dir), "Fiji.app"))
		str += "/Contents/Resources/Fiji.icns";
	else
		str += "/images/Fiji.icns";
}

static void set_path_to_JVM(void)
{
	/*
	 * MacOSX specific stuff for system java
	 * -------------------------------------
	 * Non-macosx works but places java into separate pid,
	 * which causes all kinds of strange behaviours (app can
	 * launch multiple times, etc).
	 *
	 * Search for system wide java >= 1.5
	 * and if found, launch Fiji with the system wide java.
	 * This is an adaptation from simple.c from Apple's
	 * simpleJavaLauncher code.
	 */

	CFStringRef targetJVM = CFSTR("1.5"); // Minimum Java5

	/* Look for the JavaVM bundle using its identifier. */
	CFBundleRef JavaVMBundle =
		CFBundleGetBundleWithIdentifier(CFSTR("com.apple.JavaVM"));

	if (!JavaVMBundle)
		return;

	/* Get a path for the JavaVM bundle. */
	CFURLRef JavaVMBundleURL = CFBundleCopyBundleURL(JavaVMBundle);
	CFRelease(JavaVMBundle);
	if (!JavaVMBundleURL)
		return;

	/* Append to the path the Versions Component. */
	CFURLRef JavaVMBundlerVersionsDirURL =
		CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
				JavaVMBundleURL, CFSTR("Versions"), true);
	CFRelease(JavaVMBundleURL);
	if (!JavaVMBundlerVersionsDirURL)
		return;

	/* Append to the path the target JVM's Version. */
	CFURLRef TargetJavaVM =
		CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
				JavaVMBundlerVersionsDirURL, targetJVM, true);
	CFRelease(JavaVMBundlerVersionsDirURL);
	if (!TargetJavaVM)
		return;

	UInt8 pathToTargetJVM[PATH_MAX] = "";
	Boolean result = CFURLGetFileSystemRepresentation(TargetJavaVM, true,
				pathToTargetJVM, PATH_MAX);
	CFRelease(TargetJavaVM);
	if (!result)
		return;

	/*
	 * Check to see if the directory, or a symlink for the target
	 * JVM directory exists, and if so set the environment
	 * variable JAVA_JVM_VERSION to the target JVM.
	 */
	if (access((const char *)pathToTargetJVM, R_OK))
		return;

	/*
	 * Ok, the directory exists, so now we need to set the
	 * environment var JAVA_JVM_VERSION to the CFSTR targetJVM.
	 *
	 * We can reuse the pathToTargetJVM buffer to set the environment
	 * varable.
	 */
	if (CFStringGetCString(targetJVM, (char *)pathToTargetJVM,
				PATH_MAX, kCFStringEncodingUTF8))
		setenv("JAVA_JVM_VERSION",
				(const char *)pathToTargetJVM, 1);

}

static int get_fiji_bundle_variable(const char *key, string &value)
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

	value = CFStringGetCStringPtr(propertyString,
			kCFStringEncodingMacRoman);

	return 0;
}

/* MacOSX needs to run Java in a new thread, AppKit in the main thread. */

static void dummy_call_back(void *info) {}

static void *start_ij_aux(void *dummy)
{
	exit(start_ij());
}

static int start_ij_macosx(void)
{
	/* set the Application's name */
	stringstream name;
	name << "APP_NAME_" << (long)getpid();
	setenv(name.str().c_str(), "Fiji", 1);

	/* set the Dock icon */
	stringstream icon;
	icon << "APP_ICON_" << (long)getpid();;
	string icon_path;
	append_icon_path(icon_path);
	setenv(icon.str().c_str(), icon_path.c_str(), 1);

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
#define start_ij start_ij_macosx

#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/machine.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <string.h>

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
static int is_leopard(void)
{
	int mib[2] = { CTL_KERN, KERN_OSRELEASE };
	char os_release[128];
	size_t len = sizeof(os_release);;

	return sysctl(mib, 2, os_release, &len, NULL, 0) != -1 &&
		atoi(os_release) > 8;
}

static int launch_32bit_on_tiger(int argc, char **argv)
{
	const char *match, *replace;

	if (is_leopard()) {
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

	strcpy(argv[0] + offset, replace);
	execv(argv[0], argv);
	fprintf(stderr, "Could not execute %s: %d(%s)\n",
		argv[0], errno, strerror(errno));
	exit(1);
}
#endif

int main(int argc, char **argv, char **e)
{
#if defined(MACOSX)
	launch_32bit_on_tiger(argc, argv);
#endif
	fiji_dir = get_fiji_dir(argv[0]);
	main_argv = argv;
	main_argc = argc;
	return start_ij();
}
