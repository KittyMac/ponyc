# Build llvm sources (mac os x version)

I have had the best results using llvm v7.0 installed by MacPorts. When attempting to
switch to the "vendored llvm" Pony submodules, it results in errors in the ponyc which
is generated. I believe the source of the discrepency two-fold:

1. I don't know that the specific commit the "vendored llvm" Pony is pointing to is the
  exact same as is used by MacPorts
2. The MacPorts version includes a number of source patches.
3. The "vendored llvm" version does not include cross-compilation targets

In addition, neither the MacPorts version of the "vendored llvm" include a libclang.a which
I can statically link to (and dynamically linking to is resules in rpath errors).

To get a version of llvm which meets my needs, I created this build.sh which will download,
patch, and compile llvm in (hopefully) the same way MacPorts does.  In addition, I turn on
the option to generate a statically linkable libclang.a

You shouldn't have to do any of this. If you build using the command in the Makefile-ios
then it should be taken care of automatically.

You can confirm the available cross-compilation targets by running ``./llc --version``