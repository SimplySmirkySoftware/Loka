# #1828: macOS 10.4 clip box runtime probe

`ClipProbe-tiger.mm` lays out a clipped `Box` (`CHILD_CLIP_OWN_RECT`) and its unclipped twin with the
production `MacScenePlatformController`, adds a red view across the box's right and bottom edges, and checks
pixels (view cache and window backing store) and hit tests. Exit 0 = every check passed.

Build (Mavericks build host, main 0aef7326 + #1849's test-only commits):

```sh
cmake -G Ninja -S . -B build/g40 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_DEPLOYMENT_TARGET=10.4 \
  -DCMAKE_C_COMPILER=/Developer/usr/bin/gcc-4.0 -DCMAKE_CXX_COMPILER=/Developer/usr/bin/g++-4.0 \
  -DCMAKE_OSX_ARCHITECTURES=ppc -DCMAKE_OSX_SYSROOT=/Developer/SDKs/MacOSX10.4u.sdk \
  -DLOKA_MAC_OS_10_4=ON -DLOKA_MAC_OS_10_4_SYSROOT=/Developer/SDKs/MacOSX10.4u.sdk
ninja -C build/g40 LokaMacOSCore
S=/Developer/SDKs/MacOSX10.4u.sdk
g++-4.0 -x objective-c++ -arch ppc -isysroot $S -mmacosx-version-min=10.4 -g -Icommon -Iapple/macos/src \
  -Iapple/macos/src/context -Iapple/macos -Iapple/common/cocoa -c ClipProbe-tiger.mm -o ClipProbe.o
g++-4.0 -arch ppc -isysroot $S -mmacosx-version-min=10.4 -Wl,-syslibroot,$S ClipProbe.o \
  build/g40/apple/macos/libLokaMacOSCore.a -framework Cocoa -o ClipProbe
```

At 0aef7326 `LokaMacOSCore` does not compile with GCC 4.0 (`TextDocument.cpp:378`, a nested class of
`SceneTextDocuments` reads private members of `TextDocumentRegistration`; GCC 4.0 does not extend the
enclosing class's friendship to it). The probe build made those members public in a local copy only.

Run: `ClipProbe <result-file>` from ssh on the logged-in guest (10.4.11 8S165 PPC, QEMU `ppcosx`, AppKit
824.44). Result: 3/3 PASS; `clipprobe-tiger-10.4.11.txt` is run 1.
