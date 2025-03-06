# X2645 Plugin for NVI

## 概要

`X2645Plugin`是一个为NVI SDK编写的视频编码插件，支持H.264/H.265编码。

## 编译

1. 安装CMake，最低支持版本3.20(推荐使用最新版本的CMake)；
2. 执行`cmake`构建工程，使用IDE或者执行`cmake --build .`编译。
   - **需要指定NVI头文件目录**，`-DNVI_PATH=<NVI repository or include path.>`
   - 使用vcpkg配置依赖库：`cmake ../ -DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>\scripts\buildsystems\vcpkg.cmake -DNVI_PATH=<NVI>`
   - 也可以使用[vcpkg/releases](https://github.com/NetworkVideoInterface/vcpkg/releases)中已编译的二进制静态库直接编译，将包下载后直接解压到工程根目录，然后cmake配置工程编译。
