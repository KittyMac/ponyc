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

# Note: pre-downloaded, pre-patched, and lzip compressed versions of these now exist in the 
# compressed folder. This reduces our reliance on being able to download the source outside
# of our control.

# 0. When this is run, it is assumed we want to do the full monty so we clean up any left over cruft

rm -rf src/
rm -rf build/

mkdir -p src/
mkdir -p build/

# 1. Download the exact same tar.xz files the MacPorts version does
# the commented code below will download the release tars directly from llvm. However, the full lzip compressed src
# is ~70MB so we were able to just include it in the git repo.

echo "Generating llvm project folder from compressed sources"

plzip -c -d ./compressed/clang.tar.lz > ./src/clang.tar
plzip -c -d ./compressed/compiler-rt.tar.lz > ./src/compiler-rt.tar
plzip -c -d ./compressed/libcxx.tar.lz > ./src/libcxx.tar
plzip -c -d ./compressed/lldb.tar.lz > ./src/lldb.tar
plzip -c -d ./compressed/llvm.tar.lz > ./src/llvm.tar
plzip -c -d ./compressed/polly.tar.lz > ./src/polly.tar

cd src

tar xf clang.tar
tar xf compiler-rt.tar
tar xf libcxx.tar
tar xf lldb.tar
tar xf llvm.tar
tar xf polly.tar

rm -f clang.tar
rm -f compiler-rt.tar
rm -f libcxx.tar
rm -f lldb.tar
rm -f llvm.tar
rm -f polly.tar

cd ../


# 3. Compile the sources as per the "Getting Started" guihe, with out options enabled
LLVMMACROOT="${PWD}"
cd ./build/
cmake -G "Unix Makefiles" \
  -DCMAKE_INSTALL_PREFIX="$1" \
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
  "${LLVMMACROOT}/src/llvm"
make -j28

# remove the old installation
rm -rf "$1"

make -j28 install

# 4. make install doesn't move over libclang.a, so we'll need to do that manually
cp "${LLVMMACROOT}/build/lib/libclang.a" "$1/lib/libclang.a"

# 5. Clean up.  Remove any unnecessary files which we no longer need since we have built a functioning llvm install.
rm -rf "${LLVMMACROOT}/src"
rm -rf "${LLVMMACROOT}/build"
