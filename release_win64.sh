#!/bin/bash

export PKG_CONFIG_PATH=/usr/bin/pkg-config
export PATH=$PATH:/usr/bin/pkg-config
export http_proxy="http://192.168.5.101:10809/"
export https_proxy="http://192.168.5.101:10809/"
export ANDROID_HOME=/home/wanghu/android-sdk/
export PATH=$ANDROID_HOME/platform-tools:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$PATH

make -f release_win64.mk
