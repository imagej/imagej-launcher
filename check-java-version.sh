#!/bin/sh
mkdir -p target/jars &&
cp ~/.m2/repository/net/imagej/ij/1.53b/ij-1.53b.jar target/jars &&
echo ' public class WhatJavaVersion { public static void main(String[] args) { System.out.println(System.getProperty("java.version")); } }' > WhatJavaVersion.java && 
javac WhatJavaVersion.java &&

DEBUG=1 $(find target/ -maxdepth 1 -name "ImageJ-*") --main-class WhatJavaVersion
