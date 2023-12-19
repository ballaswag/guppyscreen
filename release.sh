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
    if [ $ZBOLT -eq 1 ]; then
	tar cf guppyscreen-zbolt.tar -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen-zbolt.tar"
    else
	tar cf guppyscreen.tar -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen.tar"
    fi
else
    if [ $ZBOLT -eq 1 ]; then
	tar cf guppyscreen-zbolt-arm.tar -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen-zbolt-arm.tar"
    else
	tar cf guppyscreen-arm.tar -C releases .
	echo -n "GUPPY_ARCHIVE_NAME=guppyscreen-arm.tar"
    fi
fi
