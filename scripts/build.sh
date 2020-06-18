#!/usr/bin/env bash
###############################################################################
# Author  : Kevin Zhou<ziling.ko@gmail.com>
# Date    : 2020-06-10
# Version : v0.1
# Abstract: build armnn library under ubuntu 16.04 x86_64
# Usages  : build.sh [args]
# History : Please check the end of file.
###############################################################################

export TOPDIR=`pwd`
export logfile=$TOPDIR/build.log
export ARMNN=$TOPDIR/depends
export INSED=installed
export x86_64_PB=$ARMNN/$INSED/x86_64_protobuf
export arm64_PB=$ARMNN/$INSED/arm64_protobuf
export arm64_boost=$ARMNN/$INSED/arm64_boost
export tf_pb=$ARMNN/$INSED/tensorflow-protobuf
export x86_64_FB=$ARMNN/$INSED/x86_64_flatbuf
export arm64_FB=$ARMNN/$INSED/arm64_flatbuf

function install_tools()
{
    echo "Initial tools..." | tee -a $logfile

    sudo apt install -y git p7zip p7zip-full p7zip-rar
    sudo apt install -y crossbuild-essential-arm64 cmake scons git
    sudo apt install -y curl autoconf libtool build-essential g++
    sudo apt install -y libleveldb-dev libsnappy-dev libopencv-dev libhdf5-serial-dev
    sudo apt install -y --no-install-recommends libboost-all-dev
    sudo apt install -y libgflags-dev libgoogle-glog-dev liblmdb-dev
    sudo apt install -y libopenblas-dev
    sudo apt install -y libatlas-base-dev

    echo "tools is ready!" | tee -a $logfile
}

function download_dep_src()
{
    echo "Download depends source..." | tee -a $logfile
    [ ! -d $ARMNN ] && mkdir -p $ARMNN
    cd $ARMNN

    [ -d protobuf ] || git clone -b v3.5.2 https://github.com/google/protobuf.git protobuf
    cd protobuf && git checkout -b v3_5_2 ; git submodule update --init --recursive ; ./autogen.sh ; cd ..

    [ -d caffe ] || git clone https://github.com/BVLC/caffe.git

    [ -d boost_1_64_0 ] || wget https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.bz2
    tar xvf boost_1_64_0.tar.gz > /dev/null

    [ -d ComputeLibrary ] || git clone https://github.com/ARM-software/ComputeLibrary.git
    cd ComputeLibrary && git checkout -b v19_05 v19.05 ; cd ..

    [ -d tensorflow ] || git clone https://github.com/tensorflow/tensorflow.git
    cd tensorflow && git checkout -b v1_15 590d6ee ; cd ..

    [ -d flatbuffers-1.10.0 ] || wget -O flatbuffers-1.10.0.tar.gz https://github.com/google/flatbuffers/archive/v1.10.0.tar.gz
    tar xvf flatbuffers-1.10.0.tar.gz

    cd $TOPDIR && [ -d onnx ] || git clone https://github.com/onnx/onnx.git
    cd onnx && git fetch https://github.com/onnx/onnx.git f6125328 ; git checkout FETCH_HEAD ; cd $ARMNN

    [ -d armnn ] || git clone https://github.com/ARM-software/armnn.git
    cd armnn && git checkout -b v19_05 origin/branches/armnn_19_05 ; git checkout -b v19_05 ; cd ..

    echo "depends download finish!" | tee -a $logfile
}

function use_local_dep_src()
{
    echo "Extracting local depends source..." | tee -a $logfile
    [ ! -d $ARMNN ] && mkdir -p $ARMNN
    cd $ARMNN
    local RES=$TOPDIR/resources

    [ -d protobuf ] || 7z x $RES/protobuf.git.7z.001
    cd protobuf && git checkout -b v3_5_2 ; git submodule update --init --recursive ; ./autogen.sh ; cd ..

    [ -d caffe ] || 7z x $RES/caffe.git.7z.001

    [ -d boost_1_64_0 ] || 7z x $RES/boost_1_64_0.7z.001

    [ -d ComputeLibrary ] || 7z x $RES/ComputeLibrary.git.7z.001
    cd ComputeLibrary && git checkout -b v19_05 v19.05 ; cd ..

    [ -d tensorflow ] || 7z x $RES/tensorflow.git.7z.001
    cd tensorflow && git checkout -b v1_15 590d6ee ; cd ..

    [ -d flatbuffers-1.10.0 ] || 7z x $RES/flatbuffers-1.10.0.7z.001

    [ -d onnx ] || 7z x $RES/onnx.git.7z.001
    cd onnx && git checkout -b onnx_for_armnn f6125328 ; cd ..

    cd $TOPDIR && [ -d armnn ] || 7z x $RES/armnn.git.7z.001
    cd armnn && git checkout -b v19_05 origin/branches/armnn_19_05 ; cd $ARMNN

    echo "depends extracting finish!" | tee -a $logfile
}

function build_protobuf()
{
    echo "Build protobuf..." | tee -a $logfile
    cd $ARMNN/protobuf
    rm -rf build_x86_64 build_arm64 $x86_64_PB $arm64_PB

    mkdir -p build_x86_64
    cd build_x86_64
    # 已执行过配置,想更换安装路径时,需要关闭当前终端,重新在新终端中执行如下两个步骤
    ../configure --prefix=$x86_64_PB
    make install -j16
    [[ $? -ne 0 ]] && echo "build x86_64 protobuf failed!" | tee -a $logfile && exit 1
    cd ..

    mkdir -p build_arm64
    cd build_arm64
    export CC=aarch64-linux-gnu-gcc
    export CXX=aarch64-linux-gnu-g++
    ../configure --host=aarch64-linux --prefix=$arm64_PB --with-protoc=$x86_64_PB/bin/protoc
    make install -j16
    [[ $? -ne 0 ]] && echo "build arm64 protobuf failed!" | tee -a $logfile && exit 1

    cd $ARMNN
    echo "protobuf is ready!" | tee -a $logfile
}

function build_caffe()
{
    echo "Build caffe..." | tee -a $logfile
    cd $ARMNN/caffe

    cp Makefile.config.example Makefile.config
    sed -i "s/.*CPU_ONLY.*/CPU_ONLY := 1/"               Makefile.config
    sed -i "s/.*USE_OPENCV.*/USE_OPENCV := 1/"           Makefile.config
    sed -i "s/.*OPENCV_VERSION.*/# OPENCV_VERSION := 3/" Makefile.config
    sed -i "s|^INCLUDE_DIRS.*|INCLUDE_DIRS := \$(PYTHON_INCLUDE) /usr/local/include /usr/include/hdf5/serial \$(x86_64_PB)/include|"           Makefile.config
    sed -i "s|^LIBRARY_DIRS.*|LIBRARY_DIRS := \$(PYTHON_LIB) /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu/hdf5/serial \$(x86_64_PB)/lib|" Makefile.config

    export PATH=$x86_64_PB/bin:$PATH
    export LD_LIBRARY_PATH=$x86_64_PB/lib:$LD_LIBRARY_PATH

    make clean
    make all -j16
    [[ $? -ne 0 ]] && echo "build caffe make-all failed! [Memory may be out of stock, please try again!]" | tee -a $logfile && exit 1
    make test -j16
    [[ $? -ne 0 ]] && echo "build caffe make-test failed!" | tee -a $logfile && exit 1
    make runtest -j16
    [[ $? -ne 0 ]] && echo "build caffe make-runtest failed!" | tee -a $logfile && exit 1

    cd $ARMNN
    echo "caffe is ready!" | tee -a $logfile
}

function build_boost()
{
    echo "Build boost..." | tee -a $logfile
    cd $ARMNN/boost_1_64_0
    rm -rf $arm64_boost

    echo "using gcc : arm : aarch64-linux-gnu-g++ ;" > user_config.jam
    ./bootstrap.sh --prefix=$arm64_boost
    ./b2 install toolset=gcc-arm link=static cxxflags=-fPIC --with-filesystem --with-test --with-log --with-program_options -j32 --user-config=user_config.jam

    [[ $? -ne 0 ]] && echo "build boost failed!" | tee -a $logfile && exit 1
    cd $ARMNN
    echo "boost is ready!" | tee -a $logfile
}

function build_computelib()
{
    echo "Build ComputeLibrary..." | tee -a $logfile
    cd $ARMNN/ComputeLibrary
    rm -rf build

    scons arch=arm64-v8a neon=1 opencl=1 embed_kernels=1 extra_cxx_flags="-fPIC" -j8 internal_only=0

    [[ $? -ne 0 ]] && echo "build computelib failed!" | tee -a $logfile && exit 1
    cd $ARMNN
    echo "ComputeLibrary is ready!" | tee -a $logfile
}

function build_tensoflow()
{
    echo "Build tensorflow..." | tee -a $logfile
    cd $ARMNN/tensorflow
    rm -rf $tf_pb

    $TOPDIR/armnn/scripts/generate_tensorflow_protobuf.sh $tf_pb $x86_64_PB

    [[ $? -ne 0 ]] && echo "build tensoflow failed!" | tee -a $logfile && exit 1
    cd $ARMNN
    echo "tensorflow is ready!" | tee -a $logfile
}

function build_flatbuffer()
{
    echo "Build Flatbuffers..." | tee -a $logfile
    cd $ARMNN/flatbuffers-1.10.0
    rm -rf build_x86_64 build_arm64 $x86_64_FB $arm64_FB

    rm -f CMakeCache.txt

    mkdir -p build_x86_64
    cd build_x86_64
    cmake .. -DFLATBUFFERS_BUILD_FLATC=1 -DFLATBUFFERS_BUILD_TESTS=0 -DCMAKE_INSTALL_PREFIX:PATH=$x86_64_FB
    make all -j16
    [[ $? -ne 0 ]] && echo "build x86_64 flatbuffer make-all failed!" | tee -a $logfile && exit 1
    make install
    [[ $? -ne 0 ]] && echo "build x86_64 flatbuffer make-install failed!" | tee -a $logfile && exit 1
    cd ..

    mkdir -p build_arm64
    cd build_arm64
    cmake .. -DFLATBUFFERS_BUILD_FLATC=1 -DFLATBUFFERS_BUILD_TESTS=0 -DCMAKE_INSTALL_PREFIX:PATH=$arm64_FB -DCMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++
    make all -j16
    [[ $? -ne 0 ]] && echo "build arm64 flatbuffer make-all failed!" | tee -a $logfile && exit 1
    make install
    [[ $? -ne 0 ]] && echo "build arm64 flatbuffer make-install failed!" | tee -a $logfile && exit 1

    cd $ARMNN
    echo "Flatbuffers is ready!" | tee -a $logfile
}

function build_onnx()
{
    echo "Build onnx..." | tee -a $logfile
    cd $ARMNN/onnx

    export LD_LIBRARY_PATH=$arm64_PB/lib:$LD_LIBRARY_PATH

    $x86_64_PB/bin/protoc onnx/onnx.proto --proto_path=. --proto_path=$x86_64_PB/include --cpp_out ../onnx

    [[ $? -ne 0 ]] && echo "build onnx failed!" | tee -a $logfile && exit 1
    cd $ARMNN
    echo "onnx is ready!" | tee -a $logfile
}

function build_tflite()
{
    echo "Build tflite..." | tee -a $logfile
    rm -rf $ARMNN/tflite && mkdir -p $ARMNN/tflite
    cd $ARMNN/tflite

    cp $ARMNN/tensorflow/tensorflow/lite/schema/schema.fbs .
    $ARMNN/flatbuffers-1.10.0/build_x86_64/flatc -c --gen-object-api --reflect-types --reflect-names schema.fbs

    [[ $? -ne 0 ]] && echo "build tflite failed!" | tee -a $logfile && exit 1
    cd $ARMNN
    echo "tflite is ready!" | tee -a $logfile
}

SAMPLE_DYNAMIC_BACKEND_PATH=""

function config_armnn()
{
    echo "Config armnn..." | tee -a $logfile

    local dynamic_backend_args=" "
    if [[ $# -ge 1 ]]; then
        # (Version ≥ armnn_20_02) If you want to include standalone sample dynamic backend tests,
        # add the argument to enable the tests and the dynamic backend path to the CMake command.
        # -DSAMPLE_DYNAMIC_BACKEND=1 \
        # -DDYNAMIC_BACKEND_PATHS=$SAMPLE_DYNAMIC_BACKEND_PATH
        SAMPLE_DYNAMIC_BACKEND_PATH="$1"
        dynamic_backend_args='-DSAMPLE_DYNAMIC_BACKEND=1 -DDYNAMIC_BACKEND_PATHS=$1'
    fi

    export CXX=aarch64-linux-gnu-g++
    export CC=aarch64-linux-gnu-gcc

cmake .. \
-DARMCOMPUTE_ROOT=$ARMNN/ComputeLibrary \
-DARMCOMPUTE_BUILD_DIR=$ARMNN/ComputeLibrary/build/ \
-DBOOST_ROOT=$arm64_boost \
-DARMCOMPUTENEON=1 -DARMCOMPUTECL=1 -DARMNNREF=1 \
-DCAFFE_GENERATED_SOURCES=$ARMNN/caffe/build/src \
-DBUILD_CAFFE_PARSER=1 \
-DONNX_GENERATED_SOURCES=$ARMNN/onnx \
-DBUILD_ONNX_PARSER=1 \
-DTF_GENERATED_SOURCES=$tf_pb \
-DBUILD_TF_PARSER=1 \
-DTF_LITE_GENERATED_PATH=$ARMNN/tflite \
-DBUILD_TF_LITE_PARSER=1 \
-DFLATBUFFERS_ROOT=$arm64_FB \
-DFLATC_DIR=$ARMNN/flatbuffers-1.10.0/build \
-DPROTOBUF_ROOT=$arm64_PB \
-DPROTOBUF_LIBRARY_DEBUG=$arm64_PB/lib/libprotobuf.so.15.0.1 \
-DPROTOBUF_LIBRARY_RELEASE=$arm64_PB/lib/libprotobuf.so.15.0.1 \
${dynamic_backend_args}

    [[ $? -ne 0 ]] && echo "config armnn failed!" | tee -a $logfile && exit 1
    echo "armnn config ok!" | tee -a $logfile
}

function config_dynamic_backend()
{
    echo "Config dynamic backend..." | tee -a $logfile
    export CXX=aarch64-linux-gnu-g++
    export CC=aarch64-linux-gnu-gcc

cmake .. \
-DCMAKE_CXX_FLAGS=--std=c++14 \
-DBOOST_ROOT=$arm64_boost \
-DBoost_SYSTEM_LIBRARY=$arm64_boost/lib/libboost_system.a \
-DBoost_FILESYSTEM_LIBRARY=$arm64_boost/lib/libboost_filesystem.a \
-DARMNN_PATH=$ARMNN/armnn/build_arm64/libarmnn.so

    [[ $? -ne 0 ]] && echo "config dynamic backend failed!" | tee -a $logfile && exit 1
    echo "dynamic_backend config ok!" | tee -a $logfile
}

function build_armnn()
{
    echo "Build armnn..." | tee -a $logfile
    cd $TOPDIR/armnn

    mkdir -p build_armnn_for_arm64
    cd build_armnn_for_arm64

    [[ ! -f Makefile ]] && config_armnn $@
    make -j16
    [[ $? -ne 0 ]] && echo "build armnn failed!" | tee -a $logfile && exit 1

    if [[ $# -ge 1 ]]; then
        cd $ARMNN/armnn/src/dynamic/sample
        mkdir build_sample
        cd build_sample
        config_dynamic_backend
        make -j16
        [[ $? -ne 0 ]] && echo "build armnn sample failed!" | tee -a $logfile && exit 1
    fi
    ln -sf $TOPDIR/armnn/build_armnn_for_arm64 $TOPDIR/ArmNN
    echo "armnn is ok!" | tee -a $logfile
    echo "syncing..."
    [ ! -d $TOPDIR/libArmNN ] && mkdir -p $TOPDIR/libArmNN
    rsync -av *.so *.a UnitTests $TOPDIR/libArmNN
    rsync -av $arm64_PB/lib/libprotobuf.so.* $TOPDIR/libArmNN
}

function packaging_armnn()
{
    echo "Packaging armnn..." | tee -a $logfile
    cd $TOPDIR

    rm -rf arm64_armnn_package && mkdir -p arm64_armnn_package
    # cp libArm_SampleDynamic_backend.so arm64_armnn_package/$SAMPLE_DYNAMIC_BACKEND_PATH
    cp $TOPDIR/armnn/build_armnn_for_arm64 arm64_armnn_package
    cp $arm64_PB/lib/libprotobuf.so.15.0.1 arm64_armnn_package
    cd arm64_armnn_package && ln -sf libprotobuf.so.15.0.1 libprotobuf.so.15 ; cd ..
    tar -jcvf arm64_armnn_package.tbz2 arm64_armnn_package

    [[ $? -ne 0 ]] && echo "packaging armnn failed!" | tee -a $logfile && exit 1
    echo "armnn packaging finish!" | tee -a $logfile
}

function pre_build_dep()
{
    build_protobuf
    build_caffe
    build_boost
    build_computelib
    build_tensoflow
    build_flatbuffer
    build_onnx
    build_tflite
}

function first_run()
{
    echo "First run..." | tee -a $logfile
    cd $ARMNN

    install_tools
    if [ -d resources ] && [ "`ls -A resources`" != "" ]; then
        use_local_dep_src
    else
        download_dep_src
    fi
    pre_build_dep
    build_armnn $@
}

function usage_info()
{
    echo "Usage: $0 [first] [reset] [-a [SAMPLE_DYNAMIC_BACKEND_PATH]] [-D] [-pcbCtfoT] [-rRh]"
    echo
    echo "first first run, do all of things"
    echo "reset delete all depend files"
    echo "-a    build armnn"
    echo "-P    packaging armnn"
    echo "-r    clean armnn's compile temporary files"
    echo "-R    remove armnn's include cmake configure"
    echo "-I    install all the required tools"
    echo "-D    build all of armnn's depends"
    echo
    echo "-E    extracting all of depends source"
    echo "-p    build protobuf"
    echo "-c    build caffe (need depend protobuf)"
    echo "-b    build boost"
    echo "-C    build ComputeLibrary"
    echo "-t    build tensorflow (need depend protobuf)"
    echo "-f    build Flatbuffers"
    echo "-o    build onnx (need depend protobuf)"
    echo "-T    build tflite (need depend flatbuffers)"
    echo "-h    show help info"
    echo "Note: ComputeLibrary and ARMNN versions should correspond to each other!"
}

function main()
{
    [[ $# -lt 1 ]] && usage_info && exit 1

    echo "Starting build armnn..." | tee $logfile
    if [ "$1" = "first" ]; then
        shift && first_run $@
        exit $?
    elif [ "$1" = "reset" ]; then
        echo "Deleting dependent files..." | tee -a $logfile
        rm -rf $ARMNN/{protobuf,caffe,boost_1_64_0,ComputeLibrary,tensorflow,flatbuffers-1.10.0,onnx}
        rm -rf $ARMNN/{google,tflite,arm64_boost_install}
        exit $?
    fi

    local flag=0

    while getopts ":aPDIEpcbCtfoTrRh" opt > /dev/null; do
        case "$opt" in
            a) flag=1 ; break ;;
            r) cd $TOPDIR/armnn/build_armnn_for_arm64 && make clean ; exit 0 ;;
            R) cd $TOPDIR/armnn && rm -rf build_armnn_for_arm64 ; exit 0 ;;
            P) packaging_armnn ; exit 0 ;;
            D) pre_build_dep ; flag=2 ; break ;;
            I) install_tools ;;
            E) use_local_dep_src ;;
            p) build_protobuf ;;
            c) build_caffe ;;
            b) build_boost ;;
            C) build_computelib ;;
            t) build_tensoflow ;;
            f) build_flatbuffer ;;
            o) build_onnx ;;
            T) build_tflite ;;
            h|?) usage_info ; exit 1 ;;
        esac
    done

    [[ $flag -eq 0 ]] && exit 0
    shift $((OPTIND-1))
    [[ $flag -eq 2 && "$1" = "-a" ]] && shift

    build_armnn $@
}

main $@

###############################################################################
# Author        Date        Version    Abstract
#------------------------------------------------------------------------------
# Kevin Zhou   2020-06-10   v0.1       Initial version create
###############################################################################
