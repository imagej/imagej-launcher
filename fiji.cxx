#include "jni.h"
#include <iostream>
#ifdef MACOSX
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

static const char *relative_java_home = JAVA_HOME;
static const char *library_path = JAVA_LIB_PATH;

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
		std::cerr << "Could not load Java library!" << std::endl;
		return 1;
	}
	dlerror(); /* Clear any existing error */

	JNI_CreateJavaVM = (typeof(JNI_CreateJavaVM))dlsym(handle,
			JNI_CREATEVM);
	err = dlerror();
	if (err) {
		std::cerr << "Error loading libjvm: " << err << std::endl;
		return 1;
	}

	return JNI_CreateJavaVM(vm, env, args);
}

/*
 * The signature of start_ij() is funny because on MacOSX, it has to be called
 * via pthread_create().
 */
static void *start_ij(void *dummy)
{
	int count = 0;
	JavaVM *vm;
	JavaVMOption options[5];
	JavaVMInitArgs args;
	JNIEnv *env;
	jclass instance;
	jmethodID method;
	static char class_path[65536];
	static char plugin_path[PATH_MAX];
	static char ext_path[65536];
	static char java_home_path[65536];

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

	snprintf(class_path, sizeof(class_path),
			"-Djava.class.path=%s/ij.jar", fiji_dir);
	options[count++].optionString = class_path;

	snprintf(plugin_path, sizeof(plugin_path),
			"-Dplugins.dir=%s", fiji_dir);
	options[count++].optionString = plugin_path;

	options[count++].optionString = "ij.ImageJ";

	memset(&args, 0, sizeof(args));
	args.version  = JNI_VERSION_1_2;
	args.options = options;
	args.nOptions = count;
	args.ignoreUnrecognized = JNI_TRUE;

	if (create_java_vm(&vm, (void **)&env, &args))
		std::cerr << "Could not create JavaVM" << std::endl;
	else if (!(instance = env->FindClass("ij/ImageJ")))
		std::cerr << "Could not find ij.ImageJ" << std::endl;
	else if (!(method = env->GetStaticMethodID(instance,
					"main", "([Ljava/lang/String;)V")))
		std::cerr << "Could not find main method" << std::endl;
	else {
		int i;
		jstring jstr;
		jobjectArray args;

		if (main_argc > 1 && !(jstr = env->NewStringUTF(main_argv[1])))
			goto fail;
		if (!(args = env->NewObjectArray(main_argc - 1,
				env->FindClass("java/lang/String"), jstr)))
			goto fail;
		for (i = 2; i < main_argc; i++) {
			if (!(jstr = env->NewStringUTF(main_argv[i])))
				goto fail;
			env->SetObjectArrayElement(args, i - 1, jstr);
		}
		env->CallStaticVoidMethodA(instance, method, (jvalue *)&args);
		if (vm->DetachCurrentThread())
			std::cerr << "Could not detach current thread"
				<< std::endl;
		return NULL;
	}

fail:
	std::cerr << "Failed to start Java" << std::endl;
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
	sleep((unsigned long)-1l);
}

