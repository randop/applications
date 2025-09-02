# x64bin

### Overview
- **Goal**: Create a standalone x64 UEFI application that outputs "Hello, World!" to the UEFI console.
- **Tools**:
  - **LLVM**: To generate and compile LLVM IR into an x64 EFI executable.
  - **picolibc**: A lightweight C library suitable for bare-metal environments, providing minimal standard C functions.
  - **UEFI Firmware**: The binary will be an EFI application (`.efi`) loaded by a UEFI-compliant system.
- **Constraints**:
  - No operating system dependencies (no Windows, Linux, macOS).
  - Target a bare-metal x64 UEFI environment.
  - Store the binary on a FAT32 EFI System Partition (ESP).
- **Output**: A file named `BOOTX64.EFI` that prints "Hello, World!" to the UEFI console.

### Step 1: C Code for UEFI Application
We’ll write a minimal C program using UEFI interfaces to print "Hello, World!" and leverage picolibc for basic C library support (e.g., `strlen`). The UEFI entry point is `efi_main`, which replaces the standard C `main`.

**`hello.c`**:
```c
#include <efi.h>
#include <efilib.h>
#include <string.h>

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    // Initialize UEFI library
    InitializeLib(ImageHandle, SystemTable);

    // Print "Hello, World!" to the UEFI console
    Print(L"Hello, World!\n");

    // Halt the system to prevent immediate reboot
    SystemTable->BootServices->Stall(5000000); // 5-second delay

    return EFI_SUCCESS;
}
```

- **Explanation**:
  - `<efi.h>` and `<efilib.h>` provide UEFI-specific types and helper functions (e.g., `Print` for console output).
  - `InitializeLib` sets up the UEFI environment for library functions.
  - `Print` outputs a wide-character string (`L"Hello, World!\n"`) to the UEFI console.
  - `SystemTable->BootServices->Stall` pauses execution to make the output visible.
  - `strlen` from picolibc is included via `<string.h>`, though not used here, to demonstrate picolibc integration.
  - The function returns `EFI_SUCCESS` to indicate successful execution.

### Step 2: Generating LLVM IR
We’ll use `clang` (part of LLVM) to compile the C code into LLVM Intermediate Representation (IR). The IR will then be optimized and assembled into an x64 EFI binary.

Run the following command to generate LLVM IR:
```bash
clang -target x86_64-pc-none-elf -S -emit-llvm -fno-stack-protect -nostdinc -I/path/to/picolibc/include -I/path/to/uefi-headers -o hello.ll hello.c
```

- **Flags**:
  - `-target x86_64-pc-none-elf`: Targets a bare-metal x64 ELF environment (no OS).
  - `-S -emit-llvm`: Outputs LLVM IR to `hello.ll`.
  - `-fno-stack-protect`: Disables stack-smashing protection, unnecessary for bare-metal.
  - `-nostdinc`: Excludes standard C library headers, as we’re using picolibc.
  - `-I/path/to/picolibc/include`: Points to picolibc’s include directory.
  - `-I/path/to/uefi-headers`: Points to UEFI headers (e.g., from EDK II or GNU-EFI).

**Sample `hello.ll`** (simplified, actual IR will be longer):
```llvm
; ModuleID = 'hello.c'
source_filename = "hello.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-none-elf"

%struct.EFI_SYSTEM_TABLE = type opaque
%struct._EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL = type { i8*, i8* }

@.str = private unnamed_addr constant [14 x i16] [i16 72, i16 101, i16 108, i16 108, i16 111, i16 44, i16 32, i16 87, i16 111, i16 114, i16 108, i16 100, i16 33, i16 10], align 2

declare void @InitializeLib(i8*, %struct.EFI_SYSTEM_TABLE*) #0
declare i32 @Print(%struct._EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, i16*) #0

define dso_local i64 @efi_main(i8* %ImageHandle, %struct.EFI_SYSTEM_TABLE* %SystemTable) #1 {
  call void @InitializeLib(i8* %ImageHandle, %struct.EFI_SYSTEM_TABLE* %SystemTable)
  %1 = getelementptr inbounds %struct.EFI_SYSTEM_TABLE, %struct.EFI_SYSTEM_TABLE* %SystemTable, i32 0, i32 0
  %2 = load %struct._EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, %struct._EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL** %1, align 8
  %3 = call i32 @Print(%struct._EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* %2, i16* getelementptr inbounds ([14 x i16], [14 x i16]* @.str, i32 0, i32 0))
  %4 = getelementptr inbounds %struct.EFI_SYSTEM_TABLE, %struct.EFI_SYSTEM_TABLE* %SystemTable, i32 0, i32 1
  %5 = load i8*, i8** %4, align 8
  %6 = bitcast i8* %5 to i8* (i64)*
  %7 = call i8* %6(i64 5000000)
  ret i64 0
}

attributes #0 = { "no-trapping-math"="true" }
attributes #1 = { nounwind "no-trapping-math"="true" }
```

- **Key Elements**:
  - The IR defines the `efi_main` function, which calls `InitializeLib` and `Print`.
  - The string `"Hello, World!\n"` is stored as an array of 16-bit integers (UEFI uses UTF-16).
  - External declarations (`InitializeLib`, `Print`) link to UEFI library functions.
  - The target triple `x86_64-pc-none-elf` ensures compatibility with bare-metal x64.

### Step 3: Building the EFI Binary
To create the final `BOOTX64.EFI` binary, we need to:
1. Compile the LLVM IR to an object file.
2. Link it with picolibc and UEFI library functions.
3. Convert the result to a PE/COFF executable (UEFI’s binary format).

#### Prerequisites
- **LLVM**: Install `clang` and `lld` (LLVM’s linker).
- **picolibc**: Build and install picolibc for the `x86_64-pc-none-elf` target.
- **UEFI Headers and Libraries**: Use headers and libraries from EDK II or GNU-EFI (ensure no OS-specific dependencies).
- **FAT32 Partition**: A UEFI-compliant FAT32 EFI System Partition.

#### Build Commands
1. **Compile LLVM IR to Object File**:
   ```bash
   llc -filetype=obj -o hello.o hello.ll
   ```
   - `llc` converts LLVM IR to x64 machine code as an ELF object file.

2. **Link with picolibc and UEFI Libraries**:
   ```bash
   ld.lld -nostdlib -entry efi_main -subsystem=efi_application -o BOOTX64.EFI hello.o \
   -L/path/to/picolibc/lib -lc \
   -L/path/to/uefi-libs -lefi
   ```
   - `-nostdlib`: Avoids standard OS libraries.
   - `-entry efi_main`: Sets the UEFI entry point.
   - `-subsystem=efi_application`: Specifies the PE/COFF format for UEFI.
   - `-lc`: Links picolibc’s C library.
   - `-lefi`: Links UEFI library functions (e.g., from GNU-EFI).

   Ensure `/path/to/picolibc/lib` and `/path/to/uefi-libs` point to the directories containing `libc.a` and UEFI libraries, respectively.

3. **Verify the Output**:
   The result is `BOOTX64.EFI`, a PE/COFF executable compatible with UEFI.

### Step 4: Deploying to UEFI System
1. **Prepare FAT32 EFI Partition**:
   - Create a FAT32-formatted USB drive or disk partition.
   - Ensure it’s marked as an EFI System Partition (ESP).
   - Create the directory structure: `/EFI/BOOT/`.
   - Copy `BOOTX64.EFI` to `/EFI/BOOT/BOOTX64.EFI` (UEFI’s default boot path for x64 systems).

2. **Boot the UEFI System**:
   - Configure the UEFI firmware to boot from the USB drive or partition.
   - Disable Secure Boot (if enabled), as the binary is unsigned.
   - Boot the system, and the UEFI firmware will load `BOOTX64.EFI`.

3. **Expected Output**:
   - The UEFI console should display: `Hello, World!`.
   - The system will pause for 5 seconds before returning to the UEFI firmware.

### Step 5: Notes and Considerations
- **picolibc Integration**:
  - Picolibc provides minimal C library functions (`strlen`, etc.) suitable for bare-metal.
  - Ensure picolibc is built for `x86_64-pc-none-elf` with no OS dependencies.
  - Only a subset of picolibc is needed (e.g., string functions), keeping the binary small.

- **UEFI Libraries**:
  - Use GNU-EFI or EDK II’s `lib` for `InitializeLib` and `Print`.
  - These libraries are bare-metal and OS-agnostic, meeting the requirement to avoid Windows, Linux, or macOS dependencies.

- **LLVM IR Customization**:
  - The generated IR can be hand-optimized if needed (e.g., inline small functions).
  - Ensure all external symbols (`InitializeLib`, `Print`) are resolved during linking.

- **Size Optimization**:
  - The binary is minimal, as picolibc and UEFI libraries are lightweight.
  - Strip debug symbols to reduce size:
    ```bash
    objcopy --strip-debug BOOTX64.EFI
    ```

- **Testing**:
  - Use QEMU with OVMF (UEFI firmware) for testing:
    ```bash
    qemu-system-x86_64 -bios OVMF.fd -hda fat:rw:/path/to/efi/partition
    ```
    - `OVMF.fd` is a UEFI firmware image.
    - `/path/to/efi/partition` contains `/EFI/BOOT/BOOTX64.EFI`.

- **No OS Dependencies**:
  - The build process uses only LLVM tools, picolibc, and UEFI libraries, ensuring no reliance on Windows, Linux, or macOS.
  - All tools (clang, llc, ld.lld) are cross-platform and OS-agnostic.

### Step 6: Troubleshooting
- **Linker Errors**: Ensure picolibc and UEFI libraries are built for `x86_64-pc-none-elf` and paths are correct.
- **UEFI Boot Failure**: Verify the FAT32 partition is correctly formatted and marked as ESP. Check UEFI boot order.
- **No Output**: Ensure the UEFI console is enabled and the firmware supports text output.

### Final Deliverable
The result is `BOOTX64.EFI`, a bare-metal x64 UEFI application that:
- Runs directly on UEFI firmware.
- Prints "Hello, World!" to the console.
- Uses LLVM IR for compilation and picolibc for C library support.
- Resides on a FAT32 EFI partition.
- Has no dependencies on Windows, Linux, or macOS.

If you need specific assistance with setting up picolibc, UEFI libraries, or testing in QEMU, let me know, and I can provide detailed steps for those components!
