# Ceedling QEMU STM32xx

Would it not be lovely if
[Ceedling](http://www.throwtheswitch.org/ceedling) would run tests in a
[QMU](https://www.qemu.org/) kernel with debugging support using VS Code
from where the developer can single-step the C or the assembly. The
embedded developer could then build tests and debug them quickly.
Debugging is important because testing usefully requires it when tests
fail. Yes, it would be very handy.

> “Wouldn’t it be love(r)ly.”—Audrey Hepburn, 1964

# The Problem

Set up a Ceedling project. Run and debug tests for STM32 on Windows[^1].

For a more specific focus, this article chooses
[STM32F405xx](https://www.st.com/resource/en/datasheet/stm32f405og.pdf)
as the target or even more specifically an F405RGT6. This target
corresponds to QEMU’s `netduinoplus2` machine for the [Netduino Plus
2](https://www.seeedstudio.com/Netduino-Plus-2-p-1348.html) board. The
solution will develop a project to run its tests as a QEMU kernel, bare
metal. No need for an operating system image. The development process
did not possess any of these devices or boards. Their choice only
derives from a close match between the F4 and QEMU’s Netduino emulation.

# The Solution

All the files conveniently exist in a [Git
repo](https://github.com/royratcliffe/ceedling-qemu-stm32xx); use it as
a starting point for tailoring. For simplicity, the test sources overlay
the STM headers just as a developer might enhance an existing STM source
tree with Ceedling tests—precisely the approach taken here.

## Kit Bag

Using 0.31.1 of Ceedling on Windows with Ruby 2.7.7.1. Install Ruby
using [Chocolatey](https://chocolatey.org/) and take care not to pull
Ruby 3 in its place; you will need to ensure that Ruby 2 sits on the
search path ahead of Ruby 3 when installing both in parallel.

Listing Chocolatey packages using `choco list --local` gives, amongst
many other things:

    gcc-arm-embedded 10.2.1
    Qemu 2023.4.24

Ruby is v2.7.7. Ceedling is 0.31.1; listed using `gem list` as:

    ceedling (0.31.1)

Ceedling at this version requires Ruby 2, as previously mentioned.

## Project YAML

Ceedling uses a YAML file to configure the testing build. Sources live
in `src` by default, tests in `test` and test-support sources in
`test/support`. This is the *minimal* `package.yml`:

``` yaml
---
:project:
  :build_root: build
  :use_exceptions: FALSE

:test_build:
  :use_assembly: TRUE

:paths:
  :test:
    - +:test/**
    - -:test/support
  :source:
    - src/**
    - Core/Src/**
  :include:
    - inc/**
    - Core/Inc/**
    - Drivers/**
  :support:
    - test/support
    - Core/Startup

:extension:
  :executable: .elf

:files:
  :support:
    - +:test/support/startup.c
    - +:Core/Startup/startup_stm32f405rgtx.s
    - +:Core/Src/system_stm32f4xx.c
    - +:Core/Src/syscalls.c

:defines:
  :common: &common_defines
    - STM32F405xx
  :test:
    - *common_defines
    - TEST

:unity:
  :defines:
    - UNITY_EXCLUDE_SETJMP_H

:tools:
  :test_compiler:
    :executable: arm-none-eabi-gcc
    :arguments:
      - -I$: COLLECTION_PATHS_TEST_SUPPORT_SOURCE_INCLUDE_VENDOR
      - -D$: COLLECTION_DEFINES_TEST_AND_VENDOR
      - -g3
      - -mcpu=cortex-m4
      - -mthumb
      - -std=gnu11
      - -c ${1}
      - -o ${2}
  :test_assembler:
    :executable: arm-none-eabi-as
    :arguments:
      - ${1}
      - -o ${2}
  :test_linker:
    :executable: arm-none-eabi-gcc
    :arguments:
      - -g3
      - -T STM32F405RGTX_FLASH.ld
      - ${1}
      - -o ${2}
  :test_fixture:
    :executable: qemu-system-arm
    :arguments:
      - -machine netduinoplus2
      - -nographic # disable graphical output and redirect serial I/Os to console
      - -no-reboot # exit instead of rebooting
      - -kernel ${1}

:plugins:
  :enabled:
    - xml_tests_report
    - stdout_pretty_tests_report
    - module_generator
...
```

Some things to note:

- No exceptions enabled. Unity also needs to see the
  `UNITY_EXCLUDE_SETJMP_H` manifest constant. Exceptions require a
  working `setjmp` implementation; leave that for another day.

- Use assembly for the test build.

- VS Code’s [Ceedling Test
  Explorer](https://marketplace.visualstudio.com/items?itemName=numaru.vscode-ceedling-test-adapter)
  needs `xml_tests_report`.

- Excludes FPU (floating-point unit) options, and uses the defaults. The
  compiler step needs `__LDREXW` so requires the `-mcpu=cortex-m4`
  option.

- The project does *not* need access to the driver sources, only the
  driver headers and the
  [CMSIS](https://developer.arm.com/tools-and-software/embedded/cmsis)
  headers. Use the tree to map any other alternative drivers for
  differing targets.

## Foo Test

Build an as-yet-unimplemented test. Ceedling provides a handy little
Rake-style shortcut.

    ceedling module:create[foo]

Ceedling generates stubs including a [test
stub](https://github.com/royratcliffe/ceedling-qemu-stm32xx/blob/main/test/test_foo.c).

## Flash Load

The [STM32CubeMX
tool](https://www.st.com/en/development-tools/stm32cubemx.html)
generates two loader scripts, one for RAM-based execution, the other for
flash. Choose the
[flash](https://github.com/royratcliffe/ceedling-qemu-stm32xx/blob/main/STM32F405RGTX_FLASH.ld).
It locates the compiled code at 08000000<sub>16</sub> with data
including blank-static-storage and stack at 20000000<sub>16</sub>.

## Put Character

Unity invokes `putchar()` to output the test results character by
character. Unity implements its own formatters so no standard IO
required.

The code is very simple. It uses the low-level driver to transmit one
8-bit character to USART1. No need to initialise the hardware because
QEMU redirects its output to standard output.

``` c
#include "stm32f4xx_ll_usart.h"

int putchar(int ch) {
  LL_USART_TransmitData8(USART1, ch);
  return 0;
}
```

## Unity End, System Reset

When the tests complete, Unity runs `UnityEnd()` which reports the test
results. Finally, the firmware kernel needs to run `__NVIC_SystemReset`
in order to shut down the emulation. Unity supplies the “main” function.

``` c
#include "cmsis_compiler.h"

void main(void);

void Reset_Handler() {
  main();
  __NVIC_SystemReset();
}
```

## Clobber Test

Run Ceedling. It builds and tests.

    ceedling clobber test

The output:

    Test 'test_foo.c'
    -----------------
    Generating runner for test_foo.c...
    Compiling test_foo_runner.c...
    Compiling test_foo.c...
    Compiling unity.c...
    Compiling foo.c...
    Compiling startup_stm32f405rgtx.s...
    Compiling syscalls.c...
    Compiling system_stm32f4xx.c...
    Compiling sysmem.c...
    Compiling startup.c...
    Compiling cmock.c...
    Linking test_foo.elf...
    Running test_foo.elf...

    --------------------
    IGNORED TEST SUMMARY
    --------------------
    [test_foo.c]
      Test: test_foo_NeedToImplement
      At line (17): "Need to Implement foo"

    --------------------
    OVERALL TEST SUMMARY
    --------------------
    TESTED:  1
    PASSED:  0
    FAILED:  0
    IGNORED: 1

Success!

## VS Code Debugging

Create a launch configuration and a task. The task will launch an
emulation using QEMU.

### `launch.json`

``` json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "(gdb) Launch",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/test/out/${command:ceedlingExplorer.debugTestExecutable}",
            "cwd": "${workspaceFolder}",
            "MIMode": "gdb",
            "miDebuggerPath": "arm-none-eabi-gdb",
            "miDebuggerServerAddress": "localhost:1234",
            "preLaunchTask": "(qemu) Ceedling",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Set Disassembly Flavor to Intel",
                    "text": "-gdb-set disassembly-flavor intel",
                    "ignoreFailures": true
                }
            ]
        }
    ]
}
```

Note the `program`. Also note the `preLaunchTask`; see next.

### `tasks.json`

``` json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "(qemu) Ceedling",
            "type": "shell",
            "command": "qemu-system-arm -M netduino2 -no-reboot -nographic -kernel '${workspaceFolder}/build/test/out/${command:ceedlingExplorer.debugTestExecutable}' -S -s",
            "isBackground": true,
            "group": "test"
        }
    ]
}
```

Note the same executable path appearing in the `command` field albeit
single-quoted to escape paths with spaces. The sub-key
`command:ceedlingExplorer.debugTestExecutable` selects the Ceedling Test
Explorer’s executable—the one in focus when you tap *Debug*.

Set up a breakpoint at the test’s entry point. The debug session will
otherwise run to completion.

You might also want to enable `task.problemMatchers.neverPrompt` because
VS Code nags about missing matchers for the “(qemu) Ceedling” task. VS
Code wants to know when the task begins and ends but QMU runs quietly by
default when launched.

Happy debugging?

``` c
// Happy debugging!
#define true (rand() >= (RAND_MAX >> 1))
```

# Future Work

Enabling exceptions might be a useful upgrade. The project template
disables them because `setjmp` triggers by default.

The startup source does *not* initialise BSS (blank-static storage) or
data sections; the reset handler normally performs this basic function.
Provided the tests do not rely on static features however, no issue. The
normal reset handler appears in `Core/Startup/startup_stm32f405rgtx.s`
and can easily port to C using `__asm` directives.

[^1]: The solution proves to be operating-system agnostic. MacOS and
    Linux will work just as well.
