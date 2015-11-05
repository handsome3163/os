#!/bin/sh
## Copyright (c) 2015 Minoca Corp. All Rights Reserved.
##
## Script Name:
##
##     setup_distrib.sh
##
## Abstract:
##
##     This script adds any additional files before the distribution build
##     image is created.
##
## Author:
##
##     Evan Green 20-Feb-2015
##
## Environment:
##
##     Minoca Build
##

set -e

if test -z "$SRCROOT"; then
    SRCROOT=`pwd`/src
fi

if test -z "$ARCH"; then
    echo "ARCH must be set."
    exit 1
fi

if test -z "$DEBUG"; then
    echo "DEBUG must be set."
    exit 1
fi

export TMPDIR=$PWD
export TEMP=$TMPDIR
APPSROOT=$SRCROOT/$ARCH$DEBUG/bin/apps/

##
## Extract a few packages directly into the distribution image.
##

mkdir -p "$APPSROOT"

##
## Create a package index.
##

make_index="$SRCROOT/third-party/build/opkg-utils/opkg-make-index"
package_dir="$SRCROOT/$ARCH$DEBUG/bin/packages"
index_file="$package_dir/Packages"
python "$make_index" "$package_dir" > "$index_file"
cat "$index_file" | tr -d '\r' | gzip > "${index_file}.gz"

##
## Create a local configuration that prefers the local package repository.
##

sed "s|src/gz main.*|src/gz local file:///$package_dir|" /etc/opkg/opkg.conf > \
    ./myopkg.conf

if test -n "$QUARK"; then
    sed "s|i686|i586|g" ./myopkg.conf > ./myopkg.conf2
    mv ./myopkg.conf2 ./myopkg.conf
fi

##
## Perform an offline install of distributed.
##

PACKAGES="opkg gzip tar wget nano"
mkdir -p "$APPSROOT/usr/lib/opkg/"

opkg --conf=$PWD/myopkg.conf --offline-root="$APPSROOT" update
opkg --conf=$PWD/myopkg.conf --offline-root="$APPSROOT" --force-postinstall \
    install $PACKAGES

rm -rf "$APPSROOT/var/opkg-lists"
echo Completed adding files
