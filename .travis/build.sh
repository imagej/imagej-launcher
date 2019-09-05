#!/bin/sh
curl -fsLO https://raw.githubusercontent.com/scijava/scijava-scripts/master/travis-build.sh
sh travis-build.sh $encrypted_944d1976a731_key $encrypted_944d1976a731 || exit 1

# Get GAV
groupId="$(sed -n 's/^	<groupId>\(.*\)<\/groupId>$/\1/p' pom.xml)"
groupIdForURL="$(echo $groupId | sed -e 's/\./\//g')"
artifactId="$(sed -n 's/^	<artifactId>\(.*\)<\/artifactId>$/\1/p' pom.xml)"
version="$(sed -n 's/^	<version>\(.*\)<\/version>$/\1/p' pom.xml)"

if [ "$TRAVIS_OS_NAME" = "linux" ]
then
	classifier="linux64"
else
	classifier="macosx"
fi
executablePath="target/ImageJ-$classifier"

if [ "$TRAVIS_SECURE_ENV_VARS" = true \
	-a "$TRAVIS_PULL_REQUEST" = false \
	-a "$TRAVIS_BRANCH" = master ]
then
	mvn deploy:deploy-file -Dfile="$executablePath" -DrepositoryId="scijava.snapshots" -Durl="dav:https://maven.scijava.org/content/repositories/snapshots" -DgeneratePom="false" -DgroupId="$groupId" -DartifactId="$artifactId" -Dversion="$version" -Dclassifier="$classifier" -Dpackaging="exe"
elif [ "$TRAVIS_SECURE_ENV_VARS" = true \
	-a "$TRAVIS_PULL_REQUEST" = false \
	-a -f "target/checkout/release.properties" ]
then
	echo "== Deploying binaries =="
	# Check if a release has been deployed for that version
	folderStatus=$(curl -s -o /dev/null -I -w '%{http_code}' https://maven.scijava.org/content/repositories/releases/$groupIdForURL/$artifactId/$version/)

	# Check if the launcher for that version has already been deployed
	fileStatus=$(curl -s -o /dev/null -I -w '%{http_code}' https://maven.scijava.org/content/repositories/releases/$groupIdForURL/$artifactId/$version/$artifactId-$version-$classifier.exe)

	if [ "$folderStatus" = "200" -a "$fileStatus" != "200" ]
	then
		mvn deploy:deploy-file -Dfile="target/checkout/$executablePath" -DrepositoryId="scijava.releases" -Durl="dav:https://maven.scijava.org/content/repositories/releases" -DgeneratePom="false" -DgroupId="$groupId" -DartifactId="$artifactId" -Dversion="$version" -Dclassifier="$classifier" -Dpackaging="exe"
	fi
fi
