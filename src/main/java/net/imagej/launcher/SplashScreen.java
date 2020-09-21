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

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Window;
import java.io.File;
import java.net.URL;

import javax.swing.ImageIcon;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JProgressBar;
import javax.swing.JWindow;

/**
 * Application splash screen.
 *
 * @author Curtis Rueden
 */
public class SplashScreen {

	private static final int PROGRESS_MAX = 10000;

	private static final String LOGO_PATH = "splash/imagej.png";

	private static Object splashWindow;
	private static Object progressBar;

	public static void show() {
		if (Boolean.getBoolean("java.awt.headless")) return;
		final JWindow window = new JWindow();
		splashWindow = window; // Save a non-AWT reference to the window.
		final ClassLoader classLoader = //
			Thread.currentThread().getContextClassLoader();
		final URL logoURL = classLoader.getResource(LOGO_PATH);
		final ImageIcon imageIcon;
		if (logoURL == null) {
			// Look for images/icon.png on disk, as a fallback.
			// For backwards compatibility with old Fiji installations.
			final String parent = System.getProperty("imagej.dir") != null ? //
				System.getProperty("imagej.dir") : ".";
			final File logoFile = new File(parent, "images/icon.png");
			if (!logoFile.exists()) return;
			imageIcon = new ImageIcon(logoFile.getPath());
		}
		else imageIcon = new ImageIcon(logoURL);
		final JLabel logoImage = new JLabel(imageIcon);
		final JProgressBar bar = new JProgressBar();
		bar.setMaximum(PROGRESS_MAX);
		progressBar = bar; // Save a non-AWT reference to the progress bar.
		bar.setStringPainted(true);
		bar.setString("Starting ImageJ...");

		// lay out components
		final JPanel pane = new JPanel();
		pane.setOpaque(false);
		pane.setLayout(new BorderLayout());
		pane.add(logoImage, BorderLayout.CENTER);
		pane.add(bar, BorderLayout.SOUTH);
		window.setContentPane(pane);
		window.pack();

		window.setAlwaysOnTop(true);
		window.setLocationRelativeTo(null);
		window.setBackground(new Color(0, 0, 0, 0));

		window.setVisible(true);

		// Watch for other windows; kill the splash screen when one shows up.
		new Thread(new Runnable() {

			@Override
			public void run() {
				while (true) {
					try {
						Thread.sleep(100);
					}
					catch (final InterruptedException exc) {}
					if (splashWindow == null) return;
					final Window[] windows = Window.getWindows();
					for (final Window win : windows) {
						if (win.isVisible() && win != splashWindow) {
							hide();
							return;
						}
					}
				}
			}
		}, "Splash-Monitor").start();
	}

	public static void update(final String message, final double progress) {
		if (progressBar == null) return;
		((JProgressBar) progressBar).setString(message);
		((JProgressBar) progressBar).setValue((int) (progress * PROGRESS_MAX));
	}

	public static void hide() {
		if (splashWindow == null) return;
		((Window) splashWindow).dispose();
		splashWindow = null;
	}
}
