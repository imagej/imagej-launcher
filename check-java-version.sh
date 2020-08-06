#!/bin/sh

set -e

ijJAR="$HOME/.m2/repository/net/imagej/ij/1.53c/ij-1.53c.jar"
test -f "$ijJAR" || mvn dependency:get -Dartifact=net.imagej:ij:1.53c

mkdir -p target/jars
cp "$ijJAR" target/jars
echo ' public class WhatJavaVersion { public static void main(String[] args) { System.out.println(System.getProperty("java.version")); } }' > WhatJavaVersion.java
javac WhatJavaVersion.java

DEBUG=1 $(find target/ -maxdepth 1 -name "ImageJ-*") --main-class WhatJavaVersion
