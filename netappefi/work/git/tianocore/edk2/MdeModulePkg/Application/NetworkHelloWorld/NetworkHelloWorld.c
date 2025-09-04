#include <Uefi.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/Dhcp4.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  UINTN                    NoHandles;
  EFI_HANDLE               *Handles;
  EFI_HANDLE               ControllerHandle;
  EFI_SERVICE_BINDING_PROTOCOL *Dhcp4Sb;
  EFI_HANDLE               ChildHandle;
  EFI_DHCP4_PROTOCOL       *Dhcp4;
  EFI_DHCP4_CONFIG_DATA    ConfigData;
  EFI_DHCP4_MODE_DATA      ModeData;
  EFI_IPv4_ADDRESS         Ip;
  CHAR16                   IpStr[32];
  UINTN                    Index;

  // Print "Hello World"
  SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Hello World\r\n");

  // Find NIC handles with Simple Network Protocol
  NoHandles = 0;
  Handles = NULL;
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleNetworkProtocolGuid, NULL, &NoHandles, &Handles);
  if (EFI_ERROR(Status) || NoHandles == 0) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"No network interfaces found.\r\n");
    return Status;
  }

  // Try the first NIC for simplicity
  ControllerHandle = Handles[0];

  // Connect the network stack if not already connected
  Status = gBS->ConnectController(ControllerHandle, NULL, NULL, TRUE);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to connect network controller.\r\n");
    goto Cleanup;
  }

  // Open DHCP4 Service Binding on the controller
  Status = gBS->OpenProtocol(
                 ControllerHandle,
                 &gEfiDhcp4ServiceBindingProtocolGuid,
                 (VOID **)&Dhcp4Sb,
                 ImageHandle,
                 ControllerHandle,
                 EFI_OPEN_PROTOCOL_GET_PROTOCOL
                 );
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to locate DHCP4 Service Binding.\r\n");
    goto Cleanup;
  }

  // Create a DHCP4 child instance
  ChildHandle = NULL;
  Status = Dhcp4Sb->CreateChild(Dhcp4Sb, &ChildHandle);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to create DHCP4 child.\r\n");
    goto Cleanup;
  }

  // Open the DHCP4 protocol on the child
  Status = gBS->OpenProtocol(
                 ChildHandle,
                 &gEfiDhcp4ProtocolGuid,
                 (VOID **)&Dhcp4,
                 ImageHandle,
                 ChildHandle,
                 EFI_OPEN_PROTOCOL_BY_DRIVER
                 );
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to open DHCP4 protocol.\r\n");
    goto DestroyChild;
  }

  // Configure DHCP4 (request new IP, synchronous mode)
  ZeroMem(&ConfigData, sizeof(ConfigData));
  ConfigData.DiscoverTryCount = 2;  // Retry attempts
  ConfigData.DiscoverTimeout  = AllocateZeroPool(sizeof(UINT32) * ConfigData.DiscoverTryCount);
  if (ConfigData.DiscoverTimeout == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto CloseProtocol;
  }
  for (Index = 0; Index < ConfigData.DiscoverTryCount; Index++) {
    ConfigData.DiscoverTimeout[Index] = 5;  // 5 seconds per try
  }
  ConfigData.RequestTryCount  = 2;
  ConfigData.RequestTimeout   = AllocateZeroPool(sizeof(UINT32) * ConfigData.RequestTryCount);
  if (ConfigData.RequestTimeout == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto CloseProtocol;
  }
  for (Index = 0; Index < ConfigData.RequestTryCount; Index++) {
    ConfigData.RequestTimeout[Index] = 5;
  }
  ConfigData.OptionCount      = 0;
  ConfigData.OptionList       = NULL;
  ConfigData.ClientAddress    = (EFI_IPv4_ADDRESS){0, 0, 0, 0};  // Request new IP

  Status = Dhcp4->Configure(Dhcp4, &ConfigData);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to configure DHCP4.\r\n");
    goto CloseProtocol;
  }

  // Start DHCP process (blocking until complete)
  Status = Dhcp4->Start(Dhcp4, NULL);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"DHCP failed to start.\r\n");
    goto CloseProtocol;
  }

  // Get mode data to retrieve IP
  Status = Dhcp4->GetModeData(Dhcp4, &ModeData);
  if (EFI_ERROR(Status) || ModeData.State != Dhcp4Bound) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to get DHCP mode data or not bound.\r\n");
    goto CloseProtocol;
  }

  // Print the obtained IP
  CopyMem(&Ip, &ModeData.ConfigData.ClientAddress, sizeof(EFI_IPv4_ADDRESS));
  UnicodeSPrint(IpStr, sizeof(IpStr), L"Obtained IPv4: %d.%d.%d.%d\r\n", Ip.Addr[0], Ip.Addr[1], Ip.Addr[2], Ip.Addr[3]);
  SystemTable->ConOut->OutputString(SystemTable->ConOut, IpStr);

CloseProtocol:
  gBS->CloseProtocol(ChildHandle, &gEfiDhcp4ProtocolGuid, ImageHandle, ChildHandle);
  FreePool(ConfigData.DiscoverTimeout);
  FreePool(ConfigData.RequestTimeout);

DestroyChild:
  Dhcp4Sb->DestroyChild(Dhcp4Sb, ChildHandle);

Cleanup:
  if (Handles != NULL) {
    FreePool(Handles);
  }
  return EFI_SUCCESS;
}
