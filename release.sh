#!/bin/bash

if [ $# -ne 1 ]; then
	echo "Usage: release.sh <version>" >&2
	exit 1
fi

VERSION=$1
CURVERSION=`git tag | tail -1`
CURVERSION=${CURVERSION:1}

MAJOR=${VERSION%.*}
MINOR=${VERSION#*.}

# Update version in configure script
sed -i -e 's/\[quota_version_major\],\[.*\]/\[quota_version_major\],\['$MAJOR'\]/' configure.ac
sed -i -e 's/\[quota_version_minor\],\[.*\]/\[quota_version_minor\],\['$MINOR'\]/' configure.ac

echo "Changes in quota-tools from $CURVERSION to $VERSION" >Changelog.new
git log --pretty="* %s (%an)" v$CURVERSION.. >>Changelog.new
echo "" >>Changelog.new
cat Changelog >>Changelog.new
mv Changelog.new Changelog

git add Changelog configure.ac
git commit -s -m "Release quota-tools $VERSION"
git tag v$VERSION

# Create tarball
make dist-gzip
