[![](https://img.shields.io/maven-central/v/net.imagej/imagej-launcher.svg)](https://search.maven.org/#search%7Cgav%7C1%7Cg%3A%22net.imagej%22%20AND%20a%3A%22imagej-launcher%22)
[![](https://travis-ci.org/imagej/imagej-launcher.svg?branch=master)](https://travis-ci.org/imagej/imagej-launcher)
[![](https://ci.appveyor.com/api/projects/status/95q9hoe091w96b2n/branch/master?svg=true)](https://ci.appveyor.com/project/scijava/imagej-launcher)

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
[ImageJ Nexus](https://maven.scijava.org):

- [Windows 64-bit](https://maven.scijava.org/service/local/artifact/maven/redirect?r=snapshots&g=net.imagej&a=imagej-launcher&v=LATEST&e=exe&c=win64)
- [Windows 32-bit](https://maven.scijava.org/service/local/artifact/maven/redirect?r=snapshots&g=net.imagej&a=imagej-launcher&v=LATEST&e=exe&c=win32)
- [Linux 64-bit](https://maven.scijava.org/service/local/artifact/maven/redirect?r=snapshots&g=net.imagej&a=imagej-launcher&v=LATEST&e=exe&c=linux64)
- [macOS 64-bit](https://maven.scijava.org/service/local/artifact/maven/redirect?r=snapshots&g=net.imagej&a=imagej-launcher&v=LATEST&e=exe&c=macosx)
