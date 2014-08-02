#!/usr/bin/env bash
#Build Arduino Yun Image for Dragino2. MS14, HE. 

USAGE="Usage: . ./build_image.sh oem-application"

OPENWRT_PATH=$(pwd)

APP=common

if [ $1 ]; then 
	APP=$1
	if [ -d files-$APP ] && [ -f .config.$APP ]; then
		#########################
		echo ''
		echo "Start build process for application $APP"
		echo ''
		###########################
	else
		echo ''
		echo 'APP directory and .config are not existing'
		echo ''
		exit 0
	fi
fi

VERSION=1.3.4
BUILD=$APP-$VERSION
BUILD_TIME="`date`"

echo ""
echo "Remove custom files from last build"
rm -rf files

echo ""
echo "Copy config and files"
echo ""
cp .config.$APP .config
cp -r files-common files
cp -rf files-$APP/* files

echo ""
echo "Update version and build date"
sed -i "s/VERSION/$BUILD/g" files/etc/banner
sed -i "s/TIME/$BUILD_TIME/g" files/etc/banner
echo ""

echo ""
echo "Run make for ms14"
make -j8 V=99

echo "Copy Image"
if [ ! -d image ]
then
mkdir image
fi

echo "Set up new directory name with date"
DATE=`date +%Y%m%d-%H%M`
mkdir image/$APP-build--v$VERSION--$DATE
IMAGE_DIR=image/$APP-build--v$VERSION--$DATE

echo  "Copy files to ./image folder"
cp ./bin/ar71xx/openwrt-ar71xx-generic-linino-16M-kernel.bin     ./$IMAGE_DIR/dragino2-yun-$APP-v$VERSION-kernel.bin
cp ./bin/ar71xx/openwrt-ar71xx-generic-linino-16M-rootfs-squashfs.bin   ./$IMAGE_DIR/dragino2-yun-$APP-v$VERSION-rootfs-squashfs.bin
cp ./bin/ar71xx/openwrt-ar71xx-generic-linino-16M-squashfs-sysupgrade.bin ./$IMAGE_DIR/dragino2-yun-$APP-v$VERSION-squashfs-sysupgrade.bin


echo "Update md5sums"
cat ./bin/ar71xx/md5sums | grep "linino-16M" | awk '{gsub(/openwrt-ar71xx-generic-linino-16M/,"dragino2-yun-'"$APP"'-v'"$VERSION"'")}{print}' >> $IMAGE_DIR/md5sums


echo ""
echo "restore the files and config to common build"
rm -rf files

echo ""
echo "Copy config and files"
echo ""
cp .config.common .config
cp -r files-common files

echo ""
echo "End Dragino2 build"
echo ""


