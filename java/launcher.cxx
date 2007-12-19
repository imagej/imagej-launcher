#include <jni.h>
#include <iostream>

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

	if (JNI_CreateJavaVM(&vm, (void **)&env, &args))
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
