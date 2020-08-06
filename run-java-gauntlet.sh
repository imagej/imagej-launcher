#!/bin/bash

# run-java-gauntlet - A script to test the ImageJ native launcher against one
# or more Java installation(s)
# Inputs:
#   - base directory containing a platform-specific folder ('macosx', 'win32', 'win64', 'linux32', 'linux64') with Java installations (default: ~/Java_all/*/)
# Outputs:
#   Summary logs for each Java installation, one file each, in
#   "target/gauntlet_out"

if [ -z "$1" ]; then
  searchdir=~/Java_all/*/
else
  searchdir=$1
fi

# Ensure the native launcher is built and previous tests cleared
mvn clean package

# Make output dir
mkdir target/gauntlet_out

for pdir in $searchdir ; do

	echo "Testing all Java installations in platform directory: $pdir"

	# Make a directory for this platform
	platformname=$( basename "$pdir" )
	mkdir -p "target/java/$platformname"

	# Test each java installation for this platform
	for java in $(ls $pdir) ; do
		javadir=$pdir$java
		echo "testing $javadir"

		expected=$( basename "$javadir" )
		logfile=target/gauntlet_out/$expected.log

		# Point imagej to this JRE
		echo "linking $javadir to target/java/$platformname/$expected"
		ln -sn $javadir "target/java/$platformname/$expected"

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

		# NB: we currently don't guarantee behavior if multiple javas are present.
		# Rename this JRE out of the "java" folder so it is not discovered in future tests
		mv target/java/$platformname/$expected target/$expected
	done
done
