This document describes how to debug the ImageJ launcher.

# Build the launcher

1.  Build with `mvn -Pdebug`, which passes `-g` to the native compiler.

2.  Copy the executable from the `target` folder, and rename it to `debug`.

# Run the launcher using a debugger

You can debug the launcher using `gdb` (on Linux) or `lldb` (on OS X).

Here is a sample shell script for OS X:
```shell
#!/bin/sh
lldb -a x86_64 -f ./debug -- --ij-dir=/Applications/ImageJ.app
```

You can also add on any other flags you wish to use; e.g.:
```shell
#!/bin/sh
JAVA_DIR="/Library/Java/JavaVirtualMachines/jdk1.8.0_172.jdk/Contents/Home"
lldb -a x86_64 -f ./debug -- --ij-dir=/Applications/ImageJ.app --java-home "$JAVA_DIR"
```

See also this [GDB and LLDB cheat sheet](http://lldb.llvm.org/lldb-gdb.html).
