#!/bin/bash

RELEASES_DIR=./releases/guppyscreen
rm -rf $RELEASES_DIR
mkdir -p $RELEASES_DIR

ASSET_NAME=$1
GIT_SHA=$2
GIT_BRANCH=$3

"$CROSS_COMPILE"strip ./build/bin/guppyscreen
cp ./build/bin/guppyscreen $RELEASES_DIR/guppyscreen
cp -r ./themes $RELEASES_DIR
cp guppyscreen.json $RELEASES_DIR
echo "GIT_SHA=$GIT_SHA" > $RELEASES_DIR/release.info
echo "GIT_BRANCH=$GIT_BRANCH" >> $RELEASES_DIR/release.info
if [ "$ASSET_NAME" = "guppyscreen-smallscreen" ]; then
  sed -i 's/"display_rotate": 3/"display_rotate": 2/g' $RELEASES_DIR/guppyscreen.json
elif [ "$ASSET_NAME" = "guppyscreen-rpi" ]; then
  sed -i 's/"display_rotate": 3/"display_rotate": 1/g' $RELEASES_DIR/guppyscreen.json
fi
tar czf $ASSET_NAME.tar.gz -C $RELEASES_DIR/ .
