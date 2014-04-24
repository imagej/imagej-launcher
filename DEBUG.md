This document describes how to debug the ImageJ launcher.

# Build the launcher

1.  Build using the debug profile, which passes `-g` to the compiler.

1.  Copy the executable from:
    ```
    target/nar/imagej-launcher-<version>-<AOL>-executable/bin/<AOL>/imagej-launcher
    ```
    Where:
    * `<version>` is the current version in the POM; and
    * `<AOL>` is your NAR Architecture-OS-Linker identifier.
    And rename it to `debug`.

The following shell script will do the job:
```shell
#!/bin/sh
mvn -Pdebug clean package &&
cp target/nar/*/bin/*/imagej-launcher debug
```

# Run the launcher using a debugger

You can debug the launcher using `gdb` (on Linux) or `lldb` (on OS X).

Here is a sample shell script for OS X:
```shell
#!/bin/sh
lldb -a x86_64 -f ./debug -- --ij-dir=/Applications/Fiji.app
```

You can also add on any other flags you wish to use; e.g.:
```shell
#!/bin/sh
JAVA_DIR="/Library/Java/JavaVirtualMachines/jdk1.7.0_21.jdk/Contents/Home"
lldb -a x86_64 -f ./debug -- --ij-dir=/Applications/Fiji.app --java-home "$JAVA_DIR"
```

See also this [GDB and LLDB cheat sheet](http://lldb.llvm.org/lldb-gdb.html).
