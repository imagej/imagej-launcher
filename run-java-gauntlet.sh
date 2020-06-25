#!/bin/bash

# run-java-gauntlet - A script to test the ImageJ native launcher against one
# or more Java installation(s)
# Inputs:
#   - base directory containing Java installations (default: ~/Java_all/*/)
# Outputs:
#   Summary logs for each Java installation, one file each, in
#   "target/gauntlet_out"

if [ -z "$1" ]; then
  searchdir=~/Java_all/*/
else
  searchdir=$1
fi

# Ensure the native launcher is built
mvn clean package

# Make JRE discovery directory
mkdir target/java

# Make output dir
mkdir target/gauntlet_out

echo "Testing all Java installations in directory $searchdir"

for d in $searchdir ; do
  echo "testing $d"

  expected=$( basename "$d" )
  logfile=target/gauntlet_out/$expected.log

  # Point imagej to this JRE
  ln -sn $d target/java/$expected

  # Run the script to check this JRE
  source ./check-java-version.sh 2> $logfile

  # Extract the actual JRE used
  actual=$( echo $( tail -n 1 $logfile )|cut -d '=' -f2 )
  actual=$(basename "$actual")

  # Test if the correct JRE was used
  echo "actual: $actual" >> $logfile
  echo "expected: $expected" >> $logfile
  success="FAILED"
  if [ "$actual" == "$expected" ]; then
    success="PASSED"
  fi
  echo "$success" >> $logfile

  # Mark the file as pased/failed
  mv $logfile target/gauntlet_out/$success.$expected.log

  # NB: this is ESSENTIAL because the adjust_java_home_if_necessary() method
  # does a two-pass search such that, even if a desired JDK is "newer", if it
  # is found by the second pass, then even an "older" JDK found by the first
  # pass will win.
  # Rename this JRE out of the "java" folder so it is not discovered in future tests
  mv target/java/$expected target/$expected
done
