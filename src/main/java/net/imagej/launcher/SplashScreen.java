/*
 * #%L
 * ImageJ software for multidimensional image processing and analysis.
 * %%
 * Copyright (C) 2007 - 2018 ImageJ developers.
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

	private static final String LOGO_PATH = "images/logo.png";

	private static Object splashWindow;
	private static Object progressBar;

	public static void show() {
		if (Boolean.getBoolean("java.awt.headless")) return;
		final JWindow window = new JWindow();
		splashWindow = window; // Save a non-AWT reference to the window.
		final File logoFile = new File(new File(System.getProperty("imagej.dir")), LOGO_PATH);
		final JLabel logoImage = new JLabel(new ImageIcon(logoFile.getPath()));
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
		new Thread((Runnable) () -> {
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

	public static void main(final String[] args) throws Exception {
		SplashScreen.show();
		int steps = 40;
		for (int i=1; i<=steps; i++) {
			SplashScreen.update(i + "/" + steps, (double) i / steps);
			Thread.sleep(100);
		}
		SplashScreen.hide();
	}
}
