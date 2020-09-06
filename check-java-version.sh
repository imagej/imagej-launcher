#!/bin/sh

set -e

ijJAR="$HOME/.m2/repository/net/imagej/ij/1.53c/ij-1.53c.jar"
test -f "$ijJAR" || mvn dependency:get -Dartifact=net.imagej:ij:1.53c

mkdir -p target/jars
cp "$ijJAR" target/jars
echo '
public class WhichJava {
  public static void main(String[] args) {
    String[] props = {"java.home", "java.vendor", "java.version"};
    for (String p : props)
      System.out.println(p + " = " + System.getProperty(p));
  }
}
' > WhichJava.java
javac WhichJava.java

DEBUG=1 $(find target/ -maxdepth 1 -name "ImageJ-*") --main-class WhichJava
