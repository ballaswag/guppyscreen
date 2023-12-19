#!/bin/bash

RELEASES_DIR=./releases/guppyscreen
rm -rf $RELEASES_DIR

mkdir -p $RELEASES_DIR
"$CROSS_COMPILE"strip ./build/bin/guppyscreen
cp ./build/bin/guppyscreen $RELEASES_DIR/guppyscreen
cp -r ./k1/k1_mods $RELEASES_DIR
cp -r ./k1/scripts $RELEASES_DIR
cp ./installer.sh $RELEASES_DIR
cp reinstall-creality.sh $RELEASES_DIR

if [ x"$CROSS_COMPILE" == x"mips-linux-gnu-" ]; then
    ## k1
    if [ x"$GUPPY_THEME" == x"zbolt" ]; then
	tar czf guppyscreen-zbolt.tar.gz -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen-zbolt"
    else
	tar czf guppyscreen.tar.gz -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen"
    fi
else
    if [ x"$GUPPY_THEME" == x"zbolt" ]; then
	tar czf guppyscreen-zbolt-arm.tar.gz -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen-zbolt-arm"
    else
	tar czf guppyscreen-arm.tar.gz -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen-arm"
    fi
fi
