#!/bin/bash

if [ $# -ne 1 ]; then
	echo "Usage: release.sh <version>" >&2
	exit 1
fi

VERSION=$1
CURVERSION=`git tag | tail -1`
CURVERSION=${CURVERSION:1}

# Update version in configure script
sed -i -e 's/\[PACKAGE_VERSION\], \[".*"\],/\[PACKAGE_VERSION\], \["'$VERSION'"\],/' configure.in
autoheader
autoconf

echo "Changes in quota-tools from $CURVERSION to $VERSION" >Changelog.new
git log --pretty="* %s (%an)" v$CURVERSION.. >>Changelog.new
echo "" >>Changelog.new
cat Changelog >>Changelog.new
mv Changelog.new Changelog

git add Changelog configure.in
git commit -s -m "Release quota-tools $VERSION"
git tag v$VERSION

# Create tarball
make realclean
cd ..
tar czvf quota-$VERSION.tar.gz --exclude .git --exclude autom4te.cache quota-tools/
