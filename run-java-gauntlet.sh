#!/bin/bash

# run-java-gauntlet - A script to test the ImageJ native launcher against one
# or more Java installation(s)
# Inputs:
#   - base directory containing a platform-specific folder ('macosx', 'win32', 'win64', 'linux', 'linux-amd64') with Java installations (default: ~/.available_jdks/*/)
# Outputs:
#   Summary logs for each Java installation, one file each, in
#   "target/gauntlet"


runGauntlet() {
	pdir=$1
	echo "Testing all Java installations in platform directory: $pdir"

	# Make a directory for this platform
	platformname=$( basename "$pdir" )
	mkdir -p "target/java/$platformname"

	# Test each java installation for this platform
	for javadir in "$pdir"/* ; do
		echo "testing $javadir"

		expected=$( basename "$javadir" )
		logfile="$outputDir/$expected.log"

		# Point imagej to this JRE
		ln -sn "$javadir"
		mv "$expected" "target/java/$platformname"

		# Run the script to check this JRE
		./check-java-version.sh 2> "$logfile"

		# Extract the actual JRE used
		actual=$( tail -n 1 "$logfile" )

		# Test if the correct JRE was used
		echo "expected: $expected" >> "$logfile"
		success="FAILED"
		if [[ "$actual" == *"$expected"* ]]; then
			success="PASSED"
		fi
		echo "$success" >> "$logfile"

		# Mark the file as pased/failed
		mv "$logfile" "$outputDir/$success.$expected.log"

		# NB: we currently don't guarantee behavior if multiple javas are present.
		# Rename this JRE out of the "java" folder so it is not discovered in future tests
		mv "target/java/$platformname/$expected" "target/$expected"
	done
}

# Ensure the native launcher is built and previous tests cleared
mvn clean package

# Make output dir
outputDir=target/gauntlet
mkdir "$outputDir"

if [ $# -eq 0 ]; then
	for pdir in "$HOME/.available_jdks"/* ; do runGauntlet "$pdir"; done
else
	for pdir in $@ ; do runGauntlet "$pdir"; done
fi
