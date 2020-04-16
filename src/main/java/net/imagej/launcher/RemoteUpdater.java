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
package net.imagej.launcher;

import java.io.IOException;
import java.io.InputStreamReader;
import java.io.Reader;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;

import javax.script.ScriptEngine;
import javax.script.ScriptEngineManager;
import javax.script.ScriptException;

/**
 * Runs the ImageJ updater via Javascript from the update site.
 * <p>
 * The ImageJ updater has a convenient bootstrap Javascript that allows
 * launching the updater via jrunscript -- or from the ImageJ launcher, e.g.
 * when the ImageJ main class could not be loaded.
 * </p>
 * 
 * @author Johannes Schindelin
 */
public class RemoteUpdater {

	public final static String BOOTSTRAP_REMOTE_URL = "http://update.imagej.net/bootstrap.js";
	public final static String JS_REMOTE_URL = "http://update.imagej.net/js-1.7R2.jar";

	/**
	 * Runs the ImageJ updater via the <code>bootstrap.js</code> script, hosted on
	 * the main update site.
	 * 
	 * @param cause the reason why the ImageJ updater needs to be called, or null
	 * @return whether the script was started successfully
	 */
	public static boolean runRemote(final Throwable cause) {
		final RemoteUpdater updater = new RemoteUpdater();
		try {
			if (!updater.runAsJavascript(cause)) {
				runViaRemoteJavascript(cause);
			}
			return true;
		} catch (final Throwable t) {
			t.printStackTrace();
		}
		return false;
	}

	private boolean runAsJavascript(final Throwable cause) throws IOException, ScriptException {
		System.err.println("Falling back to remote updater at "
				+ BOOTSTRAP_REMOTE_URL);
		final URL url = new URL(BOOTSTRAP_REMOTE_URL);
		final Reader reader = new InputStreamReader(url.openStream());
		ScriptEngineManager scriptEngineManager = new ScriptEngineManager();
		ScriptEngine engine = scriptEngineManager.getEngineByName("ECMAScript");
		if (engine == null) return false;
		if (cause != null) engine.put("cause", cause);
		engine.eval("importPackage(Packages.java.lang);");
		engine.eval(reader);
		return true;
	}

	private static void runViaRemoteJavascript(final Throwable cause) throws ClassNotFoundException,
			IOException, SecurityException, IllegalArgumentException,
			IllegalAccessException, InstantiationException,
			InvocationTargetException, NoSuchMethodException {
		System.err.println("Falling back to remote updater at "
				+ BOOTSTRAP_REMOTE_URL + " via " + JS_REMOTE_URL);
		final URL jsURL = new URL(JS_REMOTE_URL);
		final URLClassLoader loader = new URLClassLoader(new URL[] { jsURL });
		final Object context = invokeStatic(loader,
				"org.mozilla.javascript.Context", "enter");
		final Object scope = construct(loader,
				"org.mozilla.javascript.ImporterTopLevel", context);
		if (cause != null) {
			final Object causeObject = invokeStatic(loader,
				"org.mozilla.javascript.Context", "toObject", cause, scope);
			invoke(scope, "put", "cause", scope, causeObject);
		}
		final URL url = new URL(BOOTSTRAP_REMOTE_URL);
		final Reader reader = new InputStreamReader(url.openStream());
		invoke(context, "evaluateReader", scope, reader, "bootstrap.js", 1,
				null);
	}

	/**
	 * Instantiates a class loaded in the given class loader.
	 * 
	 * @param loader
	 *            the class loader with which to load the class
	 * @param className
	 *            the name of the class to be instantiated
	 * @param parameters
	 *            the parameters to pass to the constructor
	 * @return the new instance
	 * @throws SecurityException
	 * @throws NoSuchMethodException
	 * @throws IllegalArgumentException
	 * @throws IllegalAccessException
	 * @throws InvocationTargetException
	 * @throws ClassNotFoundException
	 * @throws InstantiationException
	 */
	@SuppressWarnings("unchecked")
	private static <T> T construct(final ClassLoader loader,
			final String className, final Object... parameters)
			throws SecurityException, NoSuchMethodException,
			IllegalArgumentException, IllegalAccessException,
			InvocationTargetException, ClassNotFoundException,
			InstantiationException {
		final Class<?> clazz = loader.loadClass(className);
		for (final Constructor<?> constructor : clazz.getConstructors()) {
			if (doParametersMatch(constructor.getParameterTypes(), parameters)) {
				return (T) constructor.newInstance(parameters);
			}
		}
		throw new NoSuchMethodException("No matching method found");
	}

	/**
	 * Invokes a static method of a given class.
	 * <p>
	 * This method tries to find a static method matching the given name and the
	 * parameter list. Just like {@link #newInstance(String, Object...)}, this
	 * works via reflection to avoid a compile-time dependency on ImageJ2.
	 * </p>
	 * 
	 * @param loader
	 *            the class loader with which to load the class
	 * @param className
	 *            the name of the class whose static method is to be called
	 * @param methodName
	 *            the name of the static method to be called
	 * @param parameters
	 *            the parameters to pass to the static method
	 * @return the return value of the static method, if any
	 * @throws SecurityException
	 * @throws NoSuchMethodException
	 * @throws IllegalArgumentException
	 * @throws IllegalAccessException
	 * @throws InvocationTargetException
	 * @throws ClassNotFoundException
	 */
	@SuppressWarnings("unchecked")
	private static <T> T invokeStatic(final ClassLoader loader,
			final String className, final String methodName,
			final Object... parameters) throws SecurityException,
			NoSuchMethodException, IllegalArgumentException,
			IllegalAccessException, InvocationTargetException,
			ClassNotFoundException {
		final Class<?> clazz = loader.loadClass(className);
		for (final Method method : clazz.getMethods()) {
			if (method.getName().equals(methodName)
					&& doParametersMatch(method.getParameterTypes(), parameters)) {
				return (T) method.invoke(null, parameters);
			}
		}
		throw new NoSuchMethodException("No matching method found");
	}

	/**
	 * Invokes a method of a given object.
	 * <p>
	 * This method tries to find a method matching the given name and the
	 * parameter list. Just like {@link #newInstance(String, Object...)}, this
	 * works via reflection to avoid a compile-time dependency on ImageJ2.
	 * </p>
	 * 
	 * @param loader
	 *            the class loader with which to load the class
	 * @param object
	 *            the object whose method is to be called
	 * @param methodName
	 *            the name of the static method to be called
	 * @param parameters
	 *            the parameters to pass to the static method
	 * @return the return value of the method, if any
	 * @throws SecurityException
	 * @throws NoSuchMethodException
	 * @throws IllegalArgumentException
	 * @throws IllegalAccessException
	 * @throws InvocationTargetException
	 * @throws ClassNotFoundException
	 */
	@SuppressWarnings("unchecked")
	private static <T> T invoke(final Object object, final String methodName,
			final Object... parameters) throws SecurityException,
			NoSuchMethodException, IllegalArgumentException,
			IllegalAccessException, InvocationTargetException,
			ClassNotFoundException {
		final Class<?> clazz = object.getClass();
		for (final Method method : clazz.getMethods()) {
			if (method.getName().equals(methodName)
					&& doParametersMatch(method.getParameterTypes(), parameters)) {
				return (T) method.invoke(object, parameters);
			}
		}
		throw new NoSuchMethodException("No matching method found");
	}

	/**
	 * Check whether a list of parameters matches a list of parameter types.
	 * This is used to find matching constructors and (possibly static) methods.
	 * 
	 * @param types
	 *            the parameter types
	 * @param parameters
	 *            the parameters
	 * @return whether the parameters match the types
	 */
	private static boolean doParametersMatch(Class<?>[] types,
			Object[] parameters) {
		if (types.length != parameters.length)
			return false;
		for (int i = 0; i < types.length; i++)
			if (parameters[i] != null) {
				Class<?> clazz = parameters[i].getClass();
				if (types[i].isPrimitive()) {
					if (types[i] != Long.TYPE && types[i] != Integer.TYPE
							&& types[i] != Boolean.TYPE)
						throw new RuntimeException(
								"unsupported primitive type " + clazz);
					if (types[i] == Long.TYPE && clazz != Long.class)
						return false;
					else if (types[i] == Integer.TYPE && clazz != Integer.class)
						return false;
					else if (types[i] == Boolean.TYPE && clazz != Boolean.class)
						return false;
				} else if (!types[i].isAssignableFrom(clazz))
					return false;
			}
		return true;
	}

}
