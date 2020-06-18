# 在Ubuntu16.04_x64下编译适用于RK3399的armnn框架库
2020-06-10

- 一. 准备工作
- 二. 编译安装Google的Protobuf library(当前适配版本为v3.5.2)
- 三. 编译x86_64的Caffe(前方坑多,请小心驾驶!)
- 四. 编译目标平台(arm64)的Boost库
- 五. 编译ARM Compute Library
- 六. 编译Tensorflow(当时适配的版本为v1.15, commit id: 590d6eef7e)
- 七. 编译Flatbuffer(当前适配的版本为v1.10.0)
- 八. 编译Onnx(当前适配的commit id: f612532843)
- 九. 编译TfLite
- 十. 为目标平台(arm64)编译armnn(做了这么多铺垫,总算到了正题了!!!)
- 参考链接

---

## 一. 准备工作
1. 安装相关编译工具

```
sudo apt install crossbuild-essential-arm64 cmake
```

2. 配置全局变量(后续将会用到)

```
export ARMNN=$HOME/armnn-devenv

cd $ARMNN
```

3. 下载armnn的源码, 并checkout到1905版本

```
git clone https://github.com/ARM-software/armnn.git
cd armnn
git checkout branches/armnn_19_05
```

## 二. 编译安装Google的Protobuf library(当前适配版本为v3.5.2)
1. 从官网(https://github.com/protocolbuffers/protobuf)下载protobuf

```
git clone -b v3.5.2 https://github.com/google/protobuf.git protobuf // 必须如此,不能先clone再check,否则后续编译caffe会出现N多莫名奇妙的错误
cd protobuf
git submodule update --init --recursive

sudo apt install curl autoconf libtool build-essential g++
./autogen.sh // 用于生成configure
```

2. 编译一个本地版本(x86_64)的protobuf库及编译器(protoc)

```
mkdir build_x86_64
cd build_x86_64
# 已执行过配置,想更换安装路径时,需要关闭当前终端,重新在新终端中执行如下两个步骤
../configure --prefix=$ARMNN/google/x86_64_pb_install
make install -j16
cd ..
```

3. 编译目标平台版本(arm64)的protobuf库

```
mkdir build_arm64
cd build_arm64
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
../configure --host=aarch64-linux --prefix=$ARMNN/google/arm64_pb_install --with-protoc=$ARMNN/google/x86_64_pb_install/bin/protoc
make install -j16

cd $ARMNN
```

## 三. 编译x86_64的Caffe(前方坑多,请小心驾驶!)
1. 安装相关依赖项,详细可参考(http://caffe.berkeleyvision.org/install_apt.html)

```
sudo apt-get install libleveldb-dev libsnappy-dev libopencv-dev libhdf5-serial-dev
sudo apt-get install --no-install-recommends libboost-all-dev
sudo apt-get install libgflags-dev libgoogle-glog-dev liblmdb-dev
sudo apt-get install libopenblas-dev
sudo apt-get install libatlas-base-dev
```


**注: 笔者在安装libopencv-dev时出现过一系列依赖错误**
> 解决方法: 修改镜像源(/etc/apt/sources.list),笔者建议使用官方默认源或中科大源均可,中科大源如下:

```
deb http://mirrors.ustc.edu.cn/ubuntu/ xenial main restricted universe multiverse
deb http://mirrors.ustc.edu.cn/ubuntu/ xenial-security main restricted universe multiverse
deb http://mirrors.ustc.edu.cn/ubuntu/ xenial-updates main restricted universe multiverse
deb http://mirrors.ustc.edu.cn/ubuntu/ xenial-proposed main restricted universe multiverse
deb http://mirrors.ustc.edu.cn/ubuntu/ xenial-backports main restricted universe multiverse
deb-src http://mirrors.ustc.edu.cn/ubuntu/ xenial main restricted universe multiverse
deb-src http://mirrors.ustc.edu.cn/ubuntu/ xenial-security main restricted universe multiverse
deb-src http://mirrors.ustc.edu.cn/ubuntu/ xenial-updates main restricted universe multiverse
deb-src http://mirrors.ustc.edu.cn/ubuntu/ xenial-proposed main restricted universe multiverse
deb-src http://mirrors.ustc.edu.cn/ubuntu/ xenial-backports main restricted universe multiverse
```

2. 从官网(https://github.com/BVLC/caffe)下载Caffe-Master

```
git clone https://github.com/BVLC/caffe.git
cd caffe
cp Makefile.config.example Makefile.config
```

3. 根据需要适配Makefile.config(此处坑最多)

```
# CPU only version:
CPU_ONLY := 1

USE_OPENCV := 1

# Add hdf5 and protobuf include and library directories
INCLUDE_DIRS := $(PYTHON_INCLUDE) /usr/local/include /usr/include/hdf5/serial $(ARMNN)/google/x86_64_pb_install/include/
LIBRARY_DIRS := $(PYTHON_LIB) /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu/hdf5/serial $(ARMNN)/google/x86_64_pb_install/lib/
```

附笔者完整的配置: 

4. 设置环境变量(为了使用上面编译出的protoc)

```
export PATH=$ARMNN/google/x86_64_pb_install/bin/:$PATH
export LD_LIBRARY_PATH=$ARMNN/google/x86_64_pb_install/lib/:$LD_LIBRARY_PATH
```

5. 开始正常编译测试(These should all run without errors)

```
make all -j16
make test -j16
make runtest -j16

cd $ARMNN
```

==**PS: caffe.pb.h and caffe.pb.cc will be needed when building ArmNN's Caffe Parser**==

## 四. 编译目标平台(arm64)的Boost库
从官网(http://www.boost.org/doc/libs/1_64_0/more/getting_started/unix-variants.html)下载Boost
(当前适配的版本为v1.64, 高于该版本将会因依赖问题导致ArmNN编译失败).之后解压,稍作配置编译安装即可,如下:

Platform| File                 | SHA256 Hash
---     |---                   |---
unix    | boost_1_64_0.tar.bz2 | 7bcc5caace97baa948931d712ea5f37038dbb1c5d89b43ad4def4ed7cb683332
unix    | boost_1_64_0.tar.gz  | 0445c22a5ef3bd69f5dfb48354978421a85ab395254a26b1ffb0aa1bfd63a108
windows | boost_1_64_0.7z      | 49c6abfeb5b480f6a86119c0d57235966b4690ee6ff9e6401ee868244808d155
windows | boost_1_64_0.zip     | b99973c805f38b549dbeaf88701c0abeff8b0e8eaa4066df47cac10a32097523

```
wget https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.bz2
tar -zxvf boost_1_64_0.tar.gz
cd boost_1_64_0
echo "using gcc : arm : aarch64-linux-gnu-g++ ;" > user_config.jam
./bootstrap.sh --prefix=$ARMNN/arm64_boost_install
./b2 install toolset=gcc-arm link=static cxxflags=-fPIC --with-filesystem --with-test --with-log --with-program_options -j32 --user-config=user_config.jam

cd $ARMNN
```

## 五. 编译ARM Compute Library
从官网(https://github.com/ARM-software/ComputeLibrary)下载ComputeLibrary,之后按如下配置编辑即可.
> **特别提醒:**
> ComputeLibrary与ArmNN的版本应当对应!
> 例如ArmNN使用的branches/armnn_20_02, 则ComputeLibray也应当对应使用branches/arm_compute_20_02
> PS: 当前robox的depth使用的是armnn_19_05

```
git clone https://github.com/ARM-software/ComputeLibrary.git
cd ComputeLibrary/
git checkout -b v19.05
git pull
sudo apt install scons
scons arch=arm64-v8a neon=1 opencl=1 embed_kernels=1 extra_cxx_flags="-fPIC" -j8 internal_only=0

cd $ARMNN
```

## 六. 编译Tensorflow(当时适配的版本为v1.15, commit id: 590d6eef7e)

```
git clone https://github.com/tensorflow/tensorflow.git
cd tensorflow
git checkout 590d6eef7e91a6a7392c8ffffb7b58f2e0c8bc6b
../armnn/scripts/generate_tensorflow_protobuf.sh $ARMNN/google/tensorflow-protobuf $ARMNN/google/x86_64_pb_install

cd $ARMNN
```

## 七. 编译Flatbuffer(当前适配的版本为v1.10.0)
1. 从官网(https://github.com/google/flatbuffers/archive/v1.10.0.tar.gz)下载Flatbuffer

```
wget -O flatbuffers-1.10.0.tar.gz https://github.com/google/flatbuffers/archive/v1.10.0.tar.gz
tar xvf flatbuffers-1.10.0.tar.gz
cd flatbuffers-1.10.0
rm -f CMakeCache.txt
```

2. 编译一个本地版本(x86_64)的faltbuffer库

```
mkdir build_x86_64
cd build_x86_64
cmake .. -DFLATBUFFERS_BUILD_FLATC=1 -DFLATBUFFERS_BUILD_TESTS=0 -DCMAKE_INSTALL_PREFIX:PATH=$ARMNN/google/x86_64_fb_install
make all -j16
make install

cd ..
```

3. 编译目标平台版本(arm64)的faltbuffer库

```
mkdir build_arm64
cd build_arm64
cmake .. -DCMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++ -DFLATBUFFERS_BUILD_FLATC=1 -DFLATBUFFERS_BUILD_TESTS=0 -DCMAKE_INSTALL_PREFIX:PATH=$ARMNN/google/arm64_fb_install
make all -j16
make install

cd $ARMNN
```

## 八. 编译Onnx(当前适配的commit id: f612532843)

```
git clone https://github.com/onnx/onnx.git
cd onnx
git fetch https://github.com/onnx/onnx.git f612532843bd8e24efeab2815e45b436479cc9ab && git checkout FETCH_HEAD
export LD_LIBRARY_PATH=$<DIRECTORY_PATH>/protobuf-host/lib:$LD_LIBRARY_PATH
../google/x86_64_pb_install/bin/protoc onnx/onnx.proto --proto_path=. --proto_path=../google/x86_64_pb_install/include --cpp_out ../onnx

cd $ARMNN
```

## 九. 编译TfLite

```
mkdir tflite
cd tflite
cp ../tensorflow/tensorflow/lite/schema/schema.fbs .
../flatbuffers-1.10.0/build_x86_64/flatc -c --gen-object-api --reflect-types --reflect-names schema.fbs

cd $ARMNN
```

## 十. 为目标平台(arm64)编译armnn(做了这么多铺垫,总算到了正题了!!!)
1. 从官网(https://github.com/ARM-software/armnn)下载armnn, 并切换到指定分支(armnn_19_05), 上文已完成,不再赘述
2. 通过CMake适配编译环境,相应的配置脚本(armnn_config.sh)如下:

```
#!/bin/bash

export CXX=aarch64-linux-gnu-g++
export CC=aarch64-linux-gnu-gcc

cmake .. \
-DARMCOMPUTE_ROOT=$ARMNN/ComputeLibrary \
-DARMCOMPUTE_BUILD_DIR=$ARMNN/ComputeLibrary/build/ \
-DBOOST_ROOT=$ARMNN/google/arm64_boost_install/ \
-DARMCOMPUTENEON=1 -DARMCOMPUTECL=1 -DARMNNREF=1 \
-DCAFFE_GENERATED_SOURCES=$ARMNN/caffe/build/src \
-DBUILD_CAFFE_PARSER=1 \
-DONNX_GENERATED_SOURCES=$ARMNN/onnx \
-DBUILD_ONNX_PARSER=1 \
-DTF_GENERATED_SOURCES=$ARMNN/google/tensorflow-protobuf \
-DBUILD_TF_PARSER=1 \
-DTF_LITE_GENERATED_PATH=$ARMNN/tflite \
-DBUILD_TF_LITE_PARSER=1 \
-DFLATBUFFERS_ROOT=$ARMNN/google/arm64_fb_install \
-DFLATC_DIR=$ARMNN/flatbuffers-1.10.0/build \
-DPROTOBUF_ROOT=$ARMNN/google/x86_64_pb_install \
-DPROTOBUF_LIBRARY_DEBUG=$ARMNN/google/arm64_pb_install/lib/libprotobuf.so.15.0.1 \
-DPROTOBUF_LIBRARY_RELEASE=$ARMNN/google/arm64_pb_install/lib/libprotobuf.so.15.0.1

# (Version ≥ armnn_20_02) If you want to include standalone sample dynamic backend tests,
# add the argument to enable the tests and the dynamic backend path to the CMake command.
# -DSAMPLE_DYNAMIC_BACKEND=1 \
# -DDYNAMIC_BACKEND_PATHS=$SAMPLE_DYNAMIC_BACKEND_PATH
```

3. 开始执行编译

```
cd armnn
mkdir build_armnn_for_arm64
cd build_armnn_for_arm64
cp armnn_config.sh .
source ./armnn_config.sh

make -j16

cd ..
```

4. 编译独立的动态后端样例(要求版本≥armnn_20_02)

CMake的配置脚本(dynamic_backend_config.sh)如下:

```
#!/bin/bash

export CXX=aarch64-linux-gnu-g++
export CC=aarch64-linux-gnu-gcc

cmake .. \
-DCMAKE_CXX_FLAGS=--std=c++14 \
-DBOOST_ROOT=$ARMNN/arm64_boost_install/ \
-DBoost_SYSTEM_LIBRARY=$ARMNN/arm64_boost_install/lib/libboost_system.a \
-DBoost_FILESYSTEM_LIBRARY=$ARMNN/arm64_boost_install/lib/libboost_filesystem.a \
-DARMNN_PATH=$ARMNN/armnn/build_arm64/libarmnn.so
```


```
cd $ARMNN/armnn/src/dynamic/sample
mkdir build_sample
cd build_sample
cp dynamic_backend_config.sh .
source dynamic_backend_config.sh

make -j16

cd ..
```

5. 执行单元测试
> 1. 拷贝 $ARMNN/armnn/build_armnn_for_arm64 到一个arm64 linux的机器
> 2. 拷贝 $ARMNN/google/arm64_pb_install/lib/libprotobuf.so.15.0.1 到上面的 build_armnn_for_arm64 目录
> 3. 如果已使能 standalone sample dynamic tests 则需要拷贝 libArm_SampleDynamic_backend.so 到 $SAMPLE_DYNAMIC_BACKEND_PATH 所指定的路径下
> 4. 在 arm64 linux 的机器上进入build_armnn_for_arm64目录,设置LD_LIBRARY_PATH为当前路径
```cd  build_armnn_for_arm64```
```; export LD_LIBRARY_PATH=`pwd` ```

> 5. 在 build_armnn_for_arm64 目录下创建指向 libprotobuf.so.15.0.1 的符号链接
```ln -s libprotobuf.so.15.0.1 ./libprotobuf.so.15```
> 6. 开始执行单元测试
```
./UnitTests
Running 567 test cases...

*** No errors detected
```


以上脚本可以一键完成上述所有操作, 脚本要求的目录结构如下:

```
.
├── build.sh -> scripts/build.sh
├── resources
│   ├── armnn.git.7z
│   ├── boost_1_64_0.tar.bz2
│   ├── caffe.git.7z
│   ├── ComputeLibrary.git.7z
│   ├── flatbuffers-1.10.0.tar.gz
│   ├── onnx.git.7z
│   ├── protobuf.git.7z
│   └── tensorflow.git.7z
└── scripts
    └── build.sh

2 directories, 10 files
```

## 参考链接
1. https://github.com/ARM-software/armnn/blob/branches/armnn_20_05/BuildGuideCrossCompilation.md
1. 被低估的ArmNN（一）如何编译
1. caffe编译出现connot find -lopencv_imgcodecs的解决方式
1. caffe编译:fatal error: pyconfig.h: No such file or directory #include "pyconfig.h"
1. Ubuntu16.04下caffe安装编译全过程（CPU）
1. Ubuntu16.04 安装配置Caffe


