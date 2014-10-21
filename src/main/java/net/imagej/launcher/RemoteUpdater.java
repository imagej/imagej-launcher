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
 * #L%
 */
package net.imagej.launcher;

import java.io.IOException;
import java.io.InputStreamReader;
import java.io.Reader;
import java.net.URL;

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

	/**
	 * Runs the ImageJ updater via the <code>bootstrap.js</code> script, hosted on
	 * the main update site.
	 * 
	 * @param cause the reason why the ImageJ updater needs to be called, or null
	 * @return whether the script was started successfully
	 */
	public static boolean runRemote() {
		final RemoteUpdater updater = new RemoteUpdater();
		try {
			updater.runAsJavascript();
			return true;
		} catch (final Throwable t) {
			t.printStackTrace();
		}
		return false;
	}

	private void runAsJavascript() throws IOException, ScriptException {
		System.err.println("Falling back to remote updater at "
				+ BOOTSTRAP_REMOTE_URL);
		final URL url = new URL(BOOTSTRAP_REMOTE_URL);
		final Reader reader = new InputStreamReader(url.openStream());
		ScriptEngineManager scriptEngineManager = new ScriptEngineManager();
		ScriptEngine engine = scriptEngineManager.getEngineByName("ECMAScript");
		engine.eval("importPackage(Packages.java.lang);");
		engine.eval(reader);
	}
}
