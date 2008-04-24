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

static const char *relative_java_home = JAVA_HOME;
static const char *library_path = JAVA_LIB_PATH;

// FIXME: these may need to change on Windows
#include <sys/types.h>
#include <dirent.h>

/* Dynamic library loading stuff */

#ifdef MINGW32
#include <windows.h>
#define RTLD_LAZY 0
static void *dlopen(const char *name, int flags)
{
	return (void *)LoadLibrary(name);
}

static char *dlerror_value;

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
		sprintf(buffer, "./");

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
		cerr << "Could not load Java library!" << endl;
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

int build_classpath(string &result, string jar_directory) {
	if (result == "") {
		result = "-Djava.class.path=";
		result += fiji_dir;
		result += "/ij.jar";
	}
	DIR *directory = opendir(jar_directory.c_str());
	if (!directory) {
		cerr << "Failed to open: " << jar_directory << endl;
		return 1;
	}
	string extension(".jar");
	unsigned int extension_length = extension.size();
	struct dirent *entry;
	while (NULL != (entry = readdir(directory))) {
		string filename(entry->d_name);
		if (entry->d_type == DT_DIR) {
			if (filename != "." && filename != ".." &&
					build_classpath(result, jar_directory
						+ filename + "/"))
				return 1;
			continue;
		}
		unsigned int n = filename.size();
		if (n <= extension_length)
			continue;
		unsigned int extension_start = n - extension_length;
		if (!filename.compare(extension_start,
					extension_length,
					extension))
			result += ":" + jar_directory + filename;
	}
	return 0;
}

/* the maximal size of the heap on 32-bit systems, in megabyte */
#define MAX_32BIT_HEAP 1920

/*
 * The signature of start_ij() is funny because on MacOSX, it has to be called
 * via pthread_create().
 */
static void *start_ij(void *dummy)
{
	int count = 0;
	JavaVM *vm;
	JavaVMOption options[6];
	JavaVMInitArgs args;
	JNIEnv *env;
	jclass instance;
	jmethodID method;
	static string class_path;
	static char plugin_path[PATH_MAX];
	static char ext_path[65536];
	static char java_home_path[65536];
	int debug = 0;

	size_t memory_size = get_memory_size(0);
	static char heap_size[1024];

	memset(options, 0, sizeof(options));

	snprintf(java_home_path, sizeof(java_home_path),
			"-Djava.home=%s/%s",
			fiji_dir, relative_java_home);
	options[count++].optionString = java_home_path;
#ifdef MACOSX
	snprintf(ext_path, sizeof(ext_path),
			"-Djava.ext.dirs=%s/%s/lib/ext",
			fiji_dir, relative_java_home);
	options[count++].optionString = ext_path;
#endif

	if (build_classpath(class_path, string(fiji_dir) + "/jars/"))
		return NULL;
	options[count++].optionString = strdup(class_path.c_str());

	snprintf(plugin_path, sizeof(plugin_path),
			"-Dplugins.dir=%s", fiji_dir);
	options[count++].optionString = plugin_path;

	if (memory_size > 0) {
		memory_size = memory_size / 1024 * 2 / 3 / 1024;
		if (sizeof(void *) == 4 && memory_size > MAX_32BIT_HEAP)
			memory_size = MAX_32BIT_HEAP;
		snprintf(heap_size, sizeof(heap_size),
			"-Xmx%dm", (int)memory_size);
		options[count++].optionString = heap_size;
	}

	options[count++].optionString = strdup("ij.ImageJ");

	memset(&args, 0, sizeof(args));
	args.version  = JNI_VERSION_1_2;
	args.options = options;
	args.nOptions = count;
	args.ignoreUnrecognized = JNI_TRUE;

	if (create_java_vm(&vm, (void **)&env, &args))
		cerr << "Could not create JavaVM" << endl;
	else if (!(instance = env->FindClass("ij/ImageJ")))
		cerr << "Could not find ij.ImageJ" << endl;
	else if (!(method = env->GetStaticMethodID(instance,
					"main", "([Ljava/lang/String;)V")))
		cerr << "Could not find main method" << endl;
	else {
		int i;
		jstring jstr;
		jobjectArray args;

		if (!(jstr = env->NewStringUTF(main_argc > 1 ?
						main_argv[1] : "")))
			goto fail;
		if (!(args = env->NewObjectArray(main_argc - 1,
				env->FindClass("java/lang/String"), jstr)))
			goto fail;
		for (i = 1; i < main_argc; i++) {
			if (!strcmp(main_argv[i], "--dry-run")) {
				if (debug++ == 0) {
					cerr << "java";
					for (int j = 0; j < count; j++)
						cerr << " " <<
							options[j].optionString;
					for (int j = 1; j < i; j++)
						cerr << " " << main_argv[j];
				}
				continue;
			} else if (debug) {
				cerr << " " << main_argv[i];
				continue;
			}
			if (!(jstr = env->NewStringUTF(main_argv[i])))
				goto fail;
			env->SetObjectArrayElement(args, i - 1, jstr);
		}
		if (debug)
			cerr << endl;
		else {
			env->CallStaticVoidMethodA(instance,
					method, (jvalue *)&args);
			if (vm->DetachCurrentThread())
				cerr << "Could not detach current thread"
					<< endl;
		}
		/* This does not return until ImageJ exits */
		vm->DestroyJavaVM();
		return NULL;
	}

fail:
	cerr << "Failed to start Java" << endl;
	exit(1);
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

