#!/bin/sh
curl -fsLO https://raw.githubusercontent.com/scijava/scijava-scripts/master/travis-build.sh
sh travis-build.sh $encrypted_bfd4c2633768_key $encrypted_bfd4c2633768_iv

if [ "$TRAVIS_SECURE_ENV_VARS" = true \
	-a "$TRAVIS_PULL_REQUEST" = false \
	-a -f "target/checkout/release.properties" ]
then
	echo "== Deploying binaries =="
	# Get GAV
	groupId="$(sed -n 's/^	<groupId>\(.*\)<\/groupId>$/\1/p' pom.xml)"
	groupIdForURL="$(echo $groupId | sed -e 's/\./\//g')"
	artifactId="$(sed -n 's/^	<artifactId>\(.*\)<\/artifactId>$/\1/p' pom.xml)"
	version="$(sed -n 's/^	<version>\(.*\)<\/version>$/\1/p' pom.xml)"

	# Check if a release has been deployed for that version
	folderStatus=$(curl -s -o /dev/null -I -w '%{http_code}' http://maven.imagej.net/content/repositories/releases/$groupIdForURL/$artifactId/$version/)

	if [ "$TRAVIS_OS_NAME" = "linux" ]
	then
		classifier="linux64"
		executablePath="target/checkout/target/ImageJ-$classifier"
	else
		classifier="macosx"
		executablePath="target/checkout/target/Contents/MacOSX/ImageJ-$classifier"
	fi

	# Check if the launcher for that version has already been deployed
	fileStatus=$(curl -s -o /dev/null -I -w '%{http_code}' http://maven.imagej.net/content/repositories/releases/$groupIdForURL/$artifactId/$version/$artifactId-$version-$classifier.exe)

	if [ "$folderStatus" = "200" -a "$fileStatus" != "200" ]
	then
		mvn deploy:deploy-file -Dfile="$executablePath" -DrepositoryId="imagej.releases" -Durl="dav:https://maven.imagej.net/content/repositories/releases" -DgeneratePom="false" -DgroupId="$groupId" -DartifactId="$artifactId" -Dversion="$version" -Dclassifier="$classifier" -Dpackaging="exe"
	fi
fi
