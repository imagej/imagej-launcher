#!/bin/bash

# run-java-gauntlet - A script to test the ImageJ native launcher against one
# or more Java installation(s)
# Inputs:
#   - base directory containing Java installations (default: ~/Java_vers/*/)
# Outputs:
#   Summary logs for each Java installation, one file each, in
#   "target/gauntlet_out"

if [ -z "$1" ]; then
  searchdir=~/Java_vers/*
else
  searchdir=$1
fi

# Ensure the native launcher is built
mvn clean package

mkdir target/gauntlet_out


echo "Testing all Java installations in directory $searchdir"

for d in $searchdir ; do
  # Point imagej to this JRE
  ln -s "$d" target/java

  expected=$( basename "$d" )
  logfile=target/gauntlet_out/$expected.log

  # Run the script to check this jre
  source ./check-java-version.sh 2> $logfile

  # Extract the actual JDK used
  actual=$( echo $( tail -n 1 $logfile )|cut -d '=' -f2 )
  actual=$(basename "$actual")

  echo "actual: $actual" >> $logfile
  echo "expected: $expected" >> $logfile
  if [ "$actual" == "$expected" ]; then
    echo "PASSED" >> $logfile
  else
    echo "FAILED" >> $logfile
  fi
done
