#!/bin/sh

if [ ! -f ~/.m2/repository/net/imagej/ij/1.53c/ij-1.53c.jar ];
then
  mvn org.apache.maven.plugins:maven-dependency-plugin:3.1.2:get -Dartifact=net.imagej:ij:1.53c
fi

mkdir -p target/jars &&
cp ~/.m2/repository/net/imagej/ij/1.53c/ij-1.53c.jar target/jars &&
echo ' public class WhatJavaVersion { public static void main(String[] args) { System.out.println(System.getProperty("java.version")); } }' > WhatJavaVersion.java && 
javac WhatJavaVersion.java &&

DEBUG=1 $(find target/ -maxdepth 1 -name "ImageJ-*") --main-class WhatJavaVersion
