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

import java.awt.Component;
import java.awt.Graphics;
import java.net.URL;
import java.util.Collection;
import java.util.LinkedList;
import java.util.List;

import javax.swing.ImageIcon;

/**
 * A custom ImageIcon implementation that renders various splash screens onto a
 * main icon.
 *
 * @author Stefan Helfrich
 */
public class SplashIcon extends ImageIcon {

	private static final long serialVersionUID = 1L;
	private final List<ImageIcon> splashs = new LinkedList<>();
	private static final int ICON_HEIGHT = 24;
	private static final int ICON_WIDTH = 24;
	private static final int GAP_WIDTH = 2;

	public SplashIcon(final URL main, final Collection<URL> splashs) {
		super(main);

		// FIXME Simplify with streams when support for Java 6 was dropped
		for (URL url : splashs) {
			if (url == null) {
				continue;
			}
			this.splashs.add(new ImageIcon(url));
		}
	}

	@Override
	public void paintIcon(Component c, Graphics g, int x, int y) {
		// Paint the main icon to show in the background
		super.paintIcon(c, g, x, y);

		// Get size of main icon
		int mainWidth = super.getIconWidth();
		int mainHeight = super.getIconHeight();

		// Compute maximum number of splash icons that fit into one row
		final int maxNumberOfIcons = (int) Math.floor(mainWidth / (ICON_WIDTH + GAP_WIDTH));

		// Compute number of painted icons
		final int numberOfIcons = maxNumberOfIcons < splashs.size() ? maxNumberOfIcons : splashs.size();

		// Compute offset for centering icons
		final int remainder = mainWidth - numberOfIcons * ICON_WIDTH - (numberOfIcons - 1) * GAP_WIDTH;
		final int offset = (int) Math.floor(remainder / 2);

		for (int i = 0; i < numberOfIcons; i++) {
			final ImageIcon imageIcon = splashs.get(i);
			if (imageIcon == null) {
				continue;
			}

			// Use index to determine the position
			g.drawImage(imageIcon.getImage(), offset + i * (ICON_WIDTH + GAP_WIDTH), mainHeight - ICON_HEIGHT,
					offset + i * (ICON_WIDTH + GAP_WIDTH) + ICON_WIDTH, mainHeight, 0, 0, imageIcon.getIconWidth(),
					imageIcon.getIconHeight(), c);
		}
	}
}
