#!/bin/bash

# Install SDK needed for building
echo "Installing SDKs..."
git submodule init
git submodule update --init --recursive

# Pin the building versions
echo "Pinning the SDK versions..."
cd fatfs-sdk
#git checkout tags/v1.1.1
#git checkout v1.2.4
git checkout v2.6.0

cd ../pico-sdk
git checkout tags/1.5.1

cd ../pico-extras
git checkout tags/sdk-1.5.1
cd ..

# This is a dirty hack to guarantee that I can use the fatfs-sdk submodule
echo "Patching the fatfs-sdk... to use chmod"
sed -i.bak 's/#define FF_USE_CHMOD[[:space:]]*0/#define FF_USE_CHMOD 1/' fatfs-sdk/src/ff15/source/ffconf.h && mv fatfs-sdk/src/ff15/source/ffconf.h.bak .

# Set the environment variables of the SDKs
export FATFS_SDK_PATH=$PWD/fatfs-sdk
export PICO_SDK_PATH=$PWD/pico-sdk
export PICO_EXTRAS_PATH=$PWD/pico-extras

# Check if the third parameter is provided
export RELEASE_TYPE=${3:-""}
echo "Release type: $RELEASE_TYPE"

# Determine the file to use based on RELEASE_TYPE
if [ -z "$RELEASE_TYPE" ] || [ "$RELEASE_TYPE" = "final" ]; then
    VERSION_FILE="version.txt"
else
    VERSION_FILE="version-$RELEASE_TYPE.txt"
fi

# Read the release version from the version.txt file
export RELEASE_VERSION=$(cat "$VERSION_FILE" | tr -d '\r\n ')
echo "Release version: $RELEASE_VERSION"

# Get the release date and time from the current date
export RELEASE_DATE=$(date +"%Y-%m-%d %H:%M:%S")
echo "Release date: $RELEASE_DATE"

# Set the board type to be used for building
# If nothing passed as first argument, use pico w
export BOARD_TYPE=${1:-pico_w}
echo "Board type: $BOARD_TYPE"

# Set the release or debug build type
# If nothing passed as second argument, use release
export BUILD_TYPE=${2:-release}
echo "Build type: $BUILD_TYPE"

# If the build type is release, set DEBUG_MODE environment variable to 0
# Otherwise set it to 1
if [ "$BUILD_TYPE" = "release" ]; then
    export DEBUG_MODE=0
else
    export DEBUG_MODE=1
fi

# Set the build directory. Delete previous contents if any
rm -rf build
mkdir build

# We assume that the last firmware was built for the same board type
# And previously pushed to the repo version

# Build the project
cd build
cmake ../romemul
make -j4

# Copy the built firmware to the /dist folder
cd ..
mkdir -p dist
if [ "$BUILD_TYPE" = "release" ]; then
    cp build/romemul.uf2 dist/sidecart-$BOARD_TYPE.uf2
else
    cp build/romemul.uf2 dist/sidecart-$BOARD_TYPE-$BUILD_TYPE.uf2
fi
