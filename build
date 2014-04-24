#!/bin/sh

# build - Compile the launcher for debugging purposes.

mvn -Pdebug clean package &&
cp target/nar/*/bin/*/imagej-launcher ImageJ &&
cp ImageJ debug
