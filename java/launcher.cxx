#include <dlfcn.h>
#include <jni.h>
#include <iostream>

static const char *relative_library_path =
	"linux/jdk1.6.0/jre/lib/i386/client/libjvm.so";

static int create_java_vm(const char *argv0,
		JavaVM **vm, void **env, JavaVMInitArgs *args)
{
	const char *slash = strrchr(argv0, '/');
	char buffer[PATH_MAX];
	void *handle;
	char *err;
	static jint (*JNI_CreateJavaVM)(JavaVM **pvm, void **penv, void *args);

	if (slash)
		snprintf(buffer, sizeof(buffer), "%.*s%s",
			slash - argv0 + 1, argv0, relative_library_path);
	else
		snprintf(buffer, sizeof(buffer), "%s", relative_library_path);

	handle = dlopen(buffer, RTLD_LAZY);
	if (!handle) {
		std::cerr << "Could not load Java library!" << std::endl;
		return 1;
	}
	dlerror(); /* Clear any existing error */

	JNI_CreateJavaVM = (typeof(JNI_CreateJavaVM))dlsym(handle,
			"JNI_CreateJavaVM");
	err = dlerror();
	if (err) {
		std::cerr << "Error loading libjvm: " << err << std::endl;
		return 1;
	}

	return JNI_CreateJavaVM(vm, env, args);
}

int main(int argc, char **argv, char **e)
{
	JavaVM *vm;
	JavaVMOption options[2];
	JavaVMInitArgs args;
	JNIEnv *env;
	jclass instance;
	jmethodID method;

	memset(options, 0, sizeof(options));
	options[0].optionString = "-Djava.class.path=../ImageJ/ij.jar";
	options[1].optionString = "../ImageJ/ij.jar";

	memset(&args, 0, sizeof(args));
	args.version  = JNI_VERSION_1_2;
	args.options = options;
	args.nOptions = sizeof(options) / sizeof(options[0]) - 1;
	args.ignoreUnrecognized = JNI_TRUE;

	if (create_java_vm(argv[0], &vm, (void **)&env, &args))
		std::cerr << "Could not create JavaVM" << std::endl;
	else if (!(instance = env->FindClass("ij/ImageJ")))
		std::cerr << "Could not find ij.ImageJ" << std::endl;
	else if (!(method = env->GetStaticMethodID(instance,
					"main", "([Ljava/lang/String;)V")))
		std::cerr << "Could not find main method" << std::endl;
	else {
		jstring jstr;
		jobjectArray args;

		if (!(jstr = env->NewStringUTF("linux/jdk1.6.0/jre/lib/deploy/splash.jpg")))
			return 1;
		if (!(args = env->NewObjectArray(1, env->FindClass("java/lang/String"), jstr)))
			return 2;
		env->CallStaticVoidMethodA(instance, method, (jvalue *)&args);
		sleep(9999);
		std::cerr << "Alright" << std::endl;
		return 0;
	}

	return 1;
}
