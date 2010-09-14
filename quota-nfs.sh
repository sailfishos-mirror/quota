#!/bin/bash

# This is a script which generates more user friendly output of quota(1)
# command for systems with NFS-mounted home directories.
#
# In the simplest case it is enough to edit BASEDIR to point to a directory
# under which home directories are mounted. In more complicated cases,
# updating the shell matching code should be simple enough.
#
# Note that you can use also device name (NFS export in case of NFS mount
# point) for your matching.

BASEDIR="/home"

DIRS=`quota -A -Q -v --show-mntpoint --no-wrap |
sed -n -e '3,$p' |
cut -d ' ' -f 1,2 |
while read DEVICE DIR; do
  case $DIR in
    $BASEDIR/$LOGNAME) echo -n "$DIR " ;;
    $BASEDIR/*) ;;
    *) echo -n "$DIR " ;;
  esac
done`

if [ -n "$DIRS" ]; then
  quota $@ -f $DIRS
fi
