#include "jni.h"

#include <iostream>
using std::cerr;
using std::endl;

#include <string>
using std::string;

#ifdef MACOSX
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef MINGW32
#include <process.h>
#define PATH_SEP ";"
#else
#define PATH_SEP ":"
#endif

static const char *relative_java_home = JAVA_HOME;
static const char *library_path = JAVA_LIB_PATH;

// FIXME: these may need to change on Windows
#include <sys/types.h>
#include <dirent.h>

/* Dynamic library loading stuff */

#ifdef MINGW32
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
	return dlerror_value;
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



/* Java stuff */

#ifndef JNI_CREATEVM
#define JNI_CREATEVM "JNI_CreateJavaVM"
#endif

const char *fiji_dir;
char **main_argv;
int main_argc;

static char *get_fiji_dir(const char *argv0)
{
	const char *slash = strrchr(argv0, '/');
	static char buffer[PATH_MAX];
#ifdef WIN32
	const char *backslash = strrchr(argv0, '\\');

	if (backslash && slash < backslash)
		slash = backslash;
#endif

	if (slash)
		snprintf(buffer, slash - argv0 + 1, argv0);
	else
		sprintf(buffer, ".");

	return buffer;
}

static int create_java_vm(JavaVM **vm, void **env, JavaVMInitArgs *args)
{
	char java_home[PATH_MAX], buffer[PATH_MAX];
	void *handle;
	char *err;
	static jint (*JNI_CreateJavaVM)(JavaVM **pvm, void **penv, void *args);

	snprintf(java_home, sizeof(java_home), "JAVA_HOME=%s/%s",
			fiji_dir, relative_java_home);
	putenv(java_home);
	snprintf(buffer, sizeof(buffer), "%s/%s", java_home + 10, library_path);

	handle = dlopen(buffer, RTLD_LAZY);
	if (!handle) {
		const char *error = dlerror();
		if (!error)
			error = "(unknown error)";
		cerr << "Could not load Java library '" <<
			buffer << "': " << error << endl;
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

	return JNI_CreateJavaVM(vm, env, args);
}

static int headless;

int build_classpath(string &result, string jar_directory, int no_error) {
	if (result == "") {
		result = "-Djava.class.path=";
		if (headless)
			result += string(fiji_dir) + "/misc/headless.jar:";
		result += fiji_dir;
		result += "/ij.jar";
	}
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
	if (!strcmp(option, "--dry-run"))
		options.debug++;
	else if (!strcmp(option, "--system"))
		options.use_system_jvm++;
	else if (strcmp(option, "--headless") &&
			strncmp(option, "--plugins=", 10))
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

static void show_commandline(struct options& options)
{
	cerr << "java";
	for (int j = 0; j < options.java_options.nr; j++)
		cerr << " " << options.java_options.list[j];
	cerr << " ij.ImageJ";
	for (int j = 0; j < options.ij_options.nr; j++)
		cerr << " " << options.ij_options.list[j];
	cerr << endl;
}

/* the maximal size of the heap on 32-bit systems, in megabyte */
#define MAX_32BIT_HEAP 1920

/*
 * The signature of start_ij() is funny because on MacOSX, it has to be called
 * via pthread_create().
 */
static void *start_ij(void *dummy)
{
	JavaVM *vm;
	struct options options;
	JavaVMInitArgs args;
	JNIEnv *env;
	static string class_path;
	static char plugin_path[PATH_MAX] = "";
	static char ext_path[65536];
	static char java_home_path[65536];
	int dashdash = 0;

	for (int i = 1; i < main_argc; i++)
		if (!strcmp(main_argv[i], "--"))
			dashdash = i;
		else if (!strncmp(main_argv[i], "--plugins=", 10))
			snprintf(plugin_path, sizeof(plugin_path),
					"-Dplugins.dir=%s", main_argv[i] + 10);
		else if (!strcmp(main_argv[i], "--headless"))
			headless = 1;

	size_t memory_size = get_memory_size(0);
	static char heap_size[1024];

	memset(&options, 0, sizeof(options));

#ifdef MACOSX
	snprintf(ext_path, sizeof(ext_path),
			"-Djava.ext.dirs=%s/%s/lib/ext",
			fiji_dir, relative_java_home);
	add_option(options, ext_path, 0);
#endif

	if (build_classpath(class_path, string(fiji_dir) + "/plugins", 0))
		return NULL;
	if (build_classpath(class_path, string(fiji_dir) + "/jars", 0))
		return NULL;
	add_option(options, class_path, 0);

	if (!plugin_path[0])
		snprintf(plugin_path, sizeof(plugin_path),
				"-Dplugins.dir=%s", fiji_dir);
	add_option(options, plugin_path, 0);

	if (memory_size > 0) {
		memory_size = memory_size / 1024 * 2 / 3 / 1024;
		if (sizeof(void *) == 4 && memory_size > MAX_32BIT_HEAP)
			memory_size = MAX_32BIT_HEAP;
		snprintf(heap_size, sizeof(heap_size),
			"-Xmx%dm", (int)memory_size);
		add_option(options, heap_size, 0);
	}

	if (headless)
		add_option(options, "-Djava.awt.headless=true", 0);

	if (dashdash) {
		for (int i = 1; i < dashdash; i++)
			add_option(options, main_argv[i], 0);
		main_argv += dashdash;
		main_argc -= dashdash;
	}

	add_option(options, "-port0", 1);
	for (int i = 1; i < main_argc; i++)
		add_option(options, main_argv[i], 1);

	if (options.debug) {
		show_commandline(options);
		exit(0);
	}

	memset(&args, 0, sizeof(args));
	args.version  = JNI_VERSION_1_2;
	args.options = prepare_java_options(options.java_options);
	args.nOptions = options.java_options.nr;
	args.ignoreUnrecognized = JNI_FALSE;

	if (options.use_system_jvm)
		env = NULL;
	else if (create_java_vm(&vm, (void **)&env, &args)) {
		cerr << "Warning: falling back to System JVM" << endl;
		env = NULL;
	} else {
		snprintf(java_home_path, sizeof(java_home_path),
				"-Djava.home=%s/%s",
				fiji_dir, relative_java_home);
		prepend_string(options.java_options, java_home_path);
	}

	if (env) {
		jclass instance;
		jmethodID method;
		jobjectArray args;

		if (!(instance = env->FindClass("ij/ImageJ"))) {
			cerr << "Could not find ij.ImageJ" << endl;
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
		add_option(options, "ij.ImageJ", 0);
		append_string_array(options.java_options, options.ij_options);
		append_string(options.java_options, NULL);
		prepend_string(options.java_options, "java");
#ifdef MACOSX
		/*
		 * On MacOSX, one must (stupidly) fork() before exec() to
		 * clean up some pthread state somehow, otherwise the exec()
		 * will fail with "Operation not supported".
		 */
		if (fork())
			exit(0);
#endif
		if (execvp("java", options.java_options.list))
			cerr << "Could not launch system-wide Java" << endl;
		exit(1);
	}
	return NULL;
}

#ifdef MACOSX
/* MacOSX needs to run Java in a new thread, AppKit in the main thread. */

static void dummy_call_back(void *info) {}

static void start_ij_macosx(void *dummy)
{
	/* set the Application's name */
	char name[32];
	sprintf(name, "APP_NAME_%ld", (long)getpid());
	setenv(name, "ImageJ", 1);

	/* set the Dock icon */
	string icon = "APP_ICON_";
	icon += getpid();
	string icon_path = fiji_dir;
	icon_path += "/images/Fiji.icns";
	setenv(strdup(icon.c_str()), strdup(icon_path.c_str()), 1);

	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	/* Start the thread that we will start the JVM on. */
	pthread_create(&thread, &attr, start_ij, NULL);
	pthread_attr_destroy(&attr);

	CFRunLoopSourceContext context;
	memset(&context, 0, sizeof(context));
	context.perform = &dummy_call_back;

	CFRunLoopSourceRef ref = CFRunLoopSourceCreate(NULL, 0, &context);
	CFRunLoopAddSource (CFRunLoopGetCurrent(), ref, kCFRunLoopCommonModes); 
	CFRunLoopRun();
}
#define start_ij start_ij_macosx
#endif

int main(int argc, char **argv, char **e)
{
	fiji_dir = get_fiji_dir(argv[0]);
	main_argv = argv;
	main_argc = argc;
	start_ij(NULL);
	return 0;
}

