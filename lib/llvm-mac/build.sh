#!/bin/sh

# We need to be able to build our own version of llvm v7.1.0 from scratch. We want it to be exactly the same as
# the version available from MacPorts except we want it to build the static version of libclang.a
#
# This script attempts to accomplish these goals in X easy steps
#
# 1. Download the exact same tar.xz files the MacPorts version does
# 2. Apply the exact same patchs the MacPorts patch does
# 3. Compile the sources as per the "Getting Started" guihe, with out options enabled
# 4. Profit?

# 0. When this is run, it is assumed we want to do the full monty so we clean up any left over cruft

rm -rf src/
rm -rf build/

mkdir -p src/
mkdir -p build/

# 1. Download the exact same tar.xz files the MacPorts version does
cd src 
curl -L -o llvm-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/llvm-7.1.0.src.tar.xz
gunzip llvm-7.1.0.src.tar.xz
tar xf llvm-7.1.0.src.tar
rm -f llvm-7.1.0.src.tar
mv llvm-7.1.0.src llvm

curl -L -o cfe-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/cfe-7.1.0.src.tar.xz
gunzip cfe-7.1.0.src.tar.xz
tar xf cfe-7.1.0.src.tar
rm -f cfe-7.1.0.src.tar
mv cfe-7.1.0.src clang

curl -L -o compiler-rt-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/compiler-rt-7.1.0.src.tar.xz
gunzip compiler-rt-7.1.0.src.tar.xz
tar xf compiler-rt-7.1.0.src.tar
rm -f compiler-rt-7.1.0.src.tar
mv compiler-rt-7.1.0.src compiler-rt

curl -L -o libcxx-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/libcxx-7.1.0.src.tar.xz
gunzip libcxx-7.1.0.src.tar.xz
tar xf libcxx-7.1.0.src.tar
rm -f libcxx-7.1.0.src.tar
mv libcxx-7.1.0.src libcxx

curl -L -o clang-tools-extra-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/clang-tools-extra-7.1.0.src.tar.xz
gunzip clang-tools-extra-7.1.0.src.tar.xz
tar xf clang-tools-extra-7.1.0.src.tar
rm -f clang-tools-extra-7.1.0.src.tar
mv clang-tools-extra-7.1.0.src clang-tools-extra

curl -L -o lldb-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/lldb-7.1.0.src.tar.xz
gunzip lldb-7.1.0.src.tar.xz
tar xf lldb-7.1.0.src.tar
rm -f lldb-7.1.0.src.tar
mv lldb-7.1.0.src lldb

curl -L -o polly-7.1.0.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-7.1.0/polly-7.1.0.src.tar.xz
gunzip polly-7.1.0.src.tar.xz
tar xf polly-7.1.0.src.tar
rm -f polly-7.1.0.src.tar
mv polly-7.1.0.src polly

cd ../

# 2. Apply the exact same patchs the MacPorts patch does
cd src/llvm
patch -p1 < ../../patches/0001-Set-the-Mach-O-CPU-Subtype-to-ppc7400-when-targeting.patch
patch -p1 < ../../patches/0002-Define-EXC_MASK_CRASH-and-MACH_EXCEPTION_CODES-if-th.patch
patch -p1 < ../../patches/0003-MacPorts-Only-Don-t-embed-the-deployment-target-in-t.patch
patch -p1 < ../../patches/0004-Fix-build-issues-pre-Lion-due-to-missing-a-strnlen-d.patch
patch -p1 < ../../patches/0005-Threading-Only-call-pthread_setname_np-on-SnowLeopar.patch

cd ../clang
patch -p3 < ../../patches/1001-MacPorts-Only-Prepare-clang-format-for-replacement-w.patch
patch -p3 < ../../patches/1002-MacPorts-Only-Fix-name-of-scan-view-executable-insid.patch
patch -p3 < ../../patches/1003-Default-to-ppc7400-for-OSX-10.5.patch
patch -p3 < ../../patches/1004-Only-call-setpriority-PRIO_DARWIN_THREAD-0-PRIO_DARW.patch
patch -p3 < ../../patches/1005-Default-to-fragile-ObjC-runtime-when-targeting-darwi.patch
patch -p3 < ../../patches/1006-Fixup-libstdc-header-search-paths-for-older-versions.patch
patch -p3 < ../../patches/1007-Fix-build-issues-pre-Lion-due-to-missing-a-strnlen-d.patch
patch -p3 < ../../patches/openmp-locations.patch


# 3. Compile the sources as per the "Getting Started" guihe, with out options enabled
cd ../../build
cmake -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_WARNINGS=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLIBCLANG_BUILD_STATIC=ON \
  -DDARWIN_PREFER_PUBLIC_SDK=ON \
  -DLLVM_ENABLE_RTTI=ON \
  -DLLVM_ENABLE_FFI=ON \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_BUILD_RUNTIME=ON \
  -DCLANG_ENABLE_ARCMT=OFF \
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
  -DLLVM_BINDINGS_LIST=none \
  ../src/llvm
make -j28

# 4. clang-c headers are not being generated for some reason; until i figure out why, copy them over manually
cp -R ./patches/clang-c ./build/include