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
