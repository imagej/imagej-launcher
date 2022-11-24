/*
 * #%L
 * ImageJ2 software for multidimensional image processing and analysis.
 * %%
 * Copyright (C) 2007 - 2022 ImageJ2 developers.
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

package net.imagej.launcher;

import java.awt.GraphicsEnvironment;
import java.io.File;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URLClassLoader;
import java.util.Arrays;

/**
 * This class is the central entry point into Java from the ImageJ launcher.
 * <p>
 * Except for ImageJ 1.x, for backwards-compatibility, the ImageJ launcher calls
 * all Java main classes through this class, to be able to generate appropriate
 * class paths using the platform-independent convenience provided by Java's
 * class library.
 * </p>
 * 
 * @author Johannes Schindelin
 */
public class ClassLauncher {

	protected static boolean debug;

	static {
		try {
			debug = Boolean.getBoolean("ij.debug") ||
				"debug".equalsIgnoreCase(System.getProperty("scijava.log.level")) ||
				System.getenv("DEBUG_IJ_LAUNCHER") != null;
		} catch (Throwable t) {
			// ignore; Java 1.4 pretended that getenv() goes away
		}

		// Allow Java Native Access (JNA) to search java.library.path
		// for versioned shared libraries on Linux.
		final String java_library_path = System.getProperty("java.library.path");
		if (java_library_path != null && System.getProperty("jna.library.path") == null) {
			System.setProperty("jna.library.path", java_library_path);
		}
	}

	protected static String[] originalArguments;

	/**
	 * Patch ij.jar and launch the class given as first argument passing on the
	 * remaining arguments
	 * 
	 * @param arguments A list containing the name of the class whose main()
	 *          method is to be called with the remaining arguments.
	 */
	public static void main(final String[] arguments) {
		originalArguments = arguments;
		run(arguments);
	}

	public static void restart() {
		Thread.currentThread().setContextClassLoader(
			ClassLoader.class.getClassLoader());
		run(originalArguments);
	}

	protected static void run(String[] arguments) {
		boolean jdb = false, passClasspath = false;
		URLClassLoader classLoader = null;
		int i = 0;
		for (; i < arguments.length && arguments[i].charAt(0) == '-'; i++) {
			final String option = arguments[i];
			if (option.equals("-cp") || option.equals("-classpath")) {
				classLoader = ClassLoaderPlus.get(classLoader, new File(arguments[++i]));
			}
			else if (option.equals("-ijcp") || option.equals("-ijclasspath")) {
				classLoader = ClassLoaderPlus.getInImageJDirectory(classLoader, arguments[++i]);
			}
			else if (option.equals("-jarpath")) {
				classLoader =
					ClassLoaderPlus.getRecursively(classLoader, true, new File(arguments[++i]));
			}
			else if (option.equals("-ijjarpath")) {
				classLoader =
					ClassLoaderPlus.getRecursivelyInImageJDirectory(classLoader, true, arguments[++i]);
				if ("plugins".equals(arguments[i])) {
					final String ij1PluginDirs = System.getProperty("ij1.plugin.dirs");
					if (debug) System.err.println("ij1.plugin.dirs: " + ij1PluginDirs);
					if (ij1PluginDirs != null) {
						for (final String path : ij1PluginDirs.split(File.pathSeparator)) {
							final File dir = new File(path);
							if (dir.exists()) {
								ClassLoaderPlus.getRecursively(classLoader, false, dir);
							}
						}
					} else {
						final File dir = new File(System.getProperty("user.home"), ".plugins");
						if (debug) {
							System.err.println("$HOME/.plugins: " + dir + " "
									+ (dir.exists() ? "exists" : "does not exist"));
						}
						if (dir.exists()) {
							ClassLoaderPlus.getRecursively(classLoader, false, dir);
						}
					}
				}
			}
			else if (option.equals("-jdb")) jdb = true;
			else if (option.equals("-pass-classpath")) passClasspath = true;
			else if (option.equals("-freeze-classloader")) ClassLoaderPlus.freeze(classLoader);
			else {
				System.err.println("Unknown option: " + option + "!");
				System.exit(1);
			}
		}

		// If the imagej.splash system property is not set, the splash screen is
		// not shown. The --dry-run option of the launcher will set this
		// property to true by default. Starting the launcher with --no-splash
		// does not set the property.
		if (Boolean.getBoolean("imagej.splash")) {
			try {
				// Invoke SplashScreen.show() with the previously
				// constructed class loader
				Class<?> splashScreen = classLoader.loadClass("net.imagej.launcher.SplashScreen");
				Method method = splashScreen.getMethod("show");
				method.invoke(null);
			} catch (final ClassNotFoundException e) {
				if (debug)
					e.printStackTrace();
			} catch (final NoSuchMethodException e) {
				if (debug)
					e.printStackTrace();
			} catch (final IllegalAccessException e) {
				if (debug)
					e.printStackTrace();
			} catch (final InvocationTargetException e) {
				if (debug)
					e.getTargetException().printStackTrace();
			}
		}

		if (i >= arguments.length) {
			System.err.println("Missing argument: main class");
			System.exit(1);
		}

		String mainClass = arguments[i];
		arguments = slice(arguments, i + 1);

		if (!"false".equals(System.getProperty("patch.ij1")) &&
			!mainClass.equals("net.imagej.Main") &&
			!mainClass.equals("org.scijava.minimaven.MiniMaven"))
		{
			classLoader = ClassLoaderPlus.getInImageJDirectory(null, "jars/fiji-compat.jar");
			try {
				patchIJ1(classLoader);
			}
			catch (final Exception e) {
				if (!"fiji.IJ1Patcher".equals(e.getMessage())) {
					e.printStackTrace();
				}
			}
		}

		if (passClasspath && classLoader != null) {
			arguments = prepend(arguments, "-classpath", ClassLoaderPlus.getClassPath(classLoader));
		}

		if (jdb) {
			arguments = prepend(arguments, mainClass);
			if (classLoader != null) {
				arguments =
					prepend(arguments, "-classpath", ClassLoaderPlus.getClassPath(classLoader));
			}
			mainClass = "com.sun.tools.example.debug.tty.TTY";
		}

		if (debug) System.err.println("Launching main class " + mainClass +
			" with parameters " + Arrays.toString(arguments));

		try {
			launch(classLoader, mainClass, arguments);
		} catch (final Throwable t) {
			t.printStackTrace();
			if ("net.imagej.Main".equals(mainClass) &&
					!containsBatchOption(arguments) &&
					!GraphicsEnvironment.isHeadless() &&
					RemoteUpdater.runRemote(t)) {
				return;
			}
			System.exit(1);
		}
	}

	private static boolean containsBatchOption(String[] arguments) {
		for (final String argument : arguments) {
			if ("-batch".equals(argument) || "-batch-no-exit".equals(argument)) {
				return true;
			}
		}
		return false;
	}

	protected static void patchIJ1(final ClassLoader classLoader)
		throws ClassNotFoundException, IllegalAccessException,
		InstantiationException
	{
		@SuppressWarnings("unchecked")
		final Class<Runnable> clazz =
			(Class<Runnable>) classLoader.loadClass("fiji.IJ1Patcher");
		final Runnable ij1Patcher = clazz.newInstance();
		ij1Patcher.run();
	}

	protected static String[] slice(final String[] array, final int from) {
		return slice(array, from, array.length);
	}

	protected static String[] slice(final String[] array, final int from,
		final int to)
	{
		final String[] result = new String[to - from];
		if (result.length > 0) System.arraycopy(array, from, result, 0,
			result.length);
		return result;
	}

	protected static String[] prepend(final String[] array,
		final String... before)
	{
		if (before.length == 0) return array;
		final String[] result = new String[before.length + array.length];
		System.arraycopy(before, 0, result, 0, before.length);
		System.arraycopy(array, 0, result, before.length, array.length);
		return result;
	}

	protected static void launch(ClassLoader classLoader, final String className,
		final String[] arguments)
	{
		Class<?> main = null;
		if (classLoader == null) {
			classLoader = Thread.currentThread().getContextClassLoader();
		}
		if (debug) System.err.println("Class loader = " + classLoader);
		final String noSlashes = className.replace('/', '.');
		try {
			main = classLoader.loadClass(noSlashes);
		}
		catch (final ClassNotFoundException e) {
			if (debug) e.printStackTrace();
			if (noSlashes.startsWith("net.imagej.")) try {
				// fall back to old package name
				main = classLoader.loadClass(noSlashes.substring(4));
			}
			catch (final ClassNotFoundException e2) {
				if (debug) e2.printStackTrace();
				System.err.println("Class '" + noSlashes + "' was not found");
				System.exit(1);
			}
		}
		final Class<?>[] argsType = new Class<?>[] { arguments.getClass() };
		Method mainMethod = null;
		try {
			mainMethod = main.getMethod("main", argsType);
		}
		catch (final NoSuchMethodException e) {
			if (debug) e.printStackTrace();
			System.err.println("Class '" + className +
				"' does not have a main() method.");
			System.exit(1);
		}
		Integer result = new Integer(1);
		try {
			result = (Integer) mainMethod.invoke(null, new Object[] { arguments });
		}
		catch (final IllegalAccessException e) {
			if (debug) e.printStackTrace();
			System.err.println("The main() method of class '" + className +
				"' is not public.");
		}
		catch (final InvocationTargetException e) {
			System.err.println("Error while executing the main() " +
				"method of class '" + className + "':");
			e.getTargetException().printStackTrace();
		}
		if (result != null) System.exit(result.intValue());
	}

}
