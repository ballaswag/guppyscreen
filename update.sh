#!/bin/sh

GUPPY_DIR=$(dirname "$0")
VERSION_FILE=$GUPPY_DIR/.version
CUSTOM_UPGRADE_SCRIPT=$GUPPY_DIR/custom_upgrade.sh

if [ -f $VERSION_FILE ]; then
    CURRENT_VERSION=`cat $VERSION_FILE | jq -r .version`
    THEME=`cat $VERSION_FILE | jq -r .theme`
    ASSET_NAME=`cat $VERSION_FILE | jq .asset_name`
fi

CURL=`which curl`
if grep -Fqs "ID=buildroot" /etc/os-release
then
    wget -q --no-check-certificate https://raw.githubusercontent.com/ballaswag/k1-discovery/main/bin/curl -O /tmp/curl
    chmod +x /tmp/curl
    CURL=/tmp/curl
fi

$CURL -s https://api.github.com/repos/ballaswag/guppyscreen/releases -o /tmp/guppy-releases.json
latest_version=`jq -r '.[0].tag_name' /tmp/guppy-releases.json`

if [ "$(printf '%s\n' "$CURRENT_VERSION" "$latest_version" | sort -V | head -n1)" = "$latest_version" ]; then 
    echo "Current version $CURRENT_VERSION is up to date."
    exit 0
else
    asset_url=`jq -r ".[0].assets[] | select(.name == "$ASSET_NAME").browser_download_url" /tmp/guppy-releases.json`
    echo "Downloading latest version $latest_version, $asset_url"
    $CURL -L "$asset_url" -o /tmp/guppyscreen.tar.gz
fi

## override existing guppyscreen
tar xf /tmp/guppyscreen.tar.gz -C $GUPPY_DIR/..

if [ -f $CUSTOM_UPGRADE_SCRIPT ]; then
    echo "Running custom_upgrade.sh for release $latest_version"
    $CUSTOM_UPGRADE_SCRIPT
fi

echo "Updated Guppy Screen to version $latest_version"
if grep -Fqs "ID=buildroot" /etc/os-release
then
    /etc/init.d/S99guppyscreen restart &> /dev/null
fi

exit 0
