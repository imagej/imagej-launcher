[![](https://img.shields.io/maven-central/v/net.imagej/imagej-launcher.svg)](http://search.maven.org/#search%7Cgav%7C1%7Cg%3A%22net.imagej%22%20AND%20a%3A%22imagej-launcher%22)
[![](http://jenkins.imagej.net/job/ImageJ-launcher/lastBuild/badge/icon)](http://jenkins.imagej.net/job/ImageJ-launcher/)
[![](https://ci.appveyor.com/api/projects/status/r3jlwwkg1qm2p3xk?svg=true)](https://ci.appveyor.com/project/scijava/imagej-launcher)

The ImageJ launcher is a native application for launching ImageJ.

It supports the following flavors of ImageJ:

* [ImageJ1](https://github.com/imagej/ImageJA)
* [ImageJ2](https://github.com/imagej/imagej)
* [Fiji](https://github.com/fiji/fiji)

## Purpose

The launcher provides a platform-specific entry point into the ImageJ Java
application. Its most major function is to facilitate the ImageJ Updater
feature by taking care of pending updates when ImageJ is first launched.

## Usage

For an overview of supported options, run:

    ./ImageJ-xyz --help

where `xyz` is your platform.

## Download

Up-to-date versions can always be downloaded from the
[Jenkins](http://jenkins.imagej.net/job/ImageJ-launcher/) platform-specific
configurations.
