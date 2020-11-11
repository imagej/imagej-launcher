#!/bin/sh

set -e

ijJAR="$HOME/.m2/repository/net/imagej/ij/1.53c/ij-1.53c.jar"
test -f "$ijJAR" || mvn dependency:get -Dartifact=net.imagej:ij:1.53c

mkdir -p target/jars
cp "$ijJAR" target/jars
echo '
import java.io.*;

public class WhichJava {
  public static void main(String[] args) throws FileNotFoundException {
    String[] props = {"java.version", "java.vendor", "java.home"};
    System.setOut(new PrintStream(new File("which_java.out")));
    for (String p : props)
      System.out.println(p + " = " + System.getProperty(p));
  }
}
' > WhichJava.java
javac WhichJava.java

DEBUG=1 $(find target/ -maxdepth 1 -name "ImageJ-*") --main-class WhichJava

cat which_java.out
rm which_java.out
