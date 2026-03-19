#include <efi.h>
#include <efilib.h>
#include <string.h>
#include <stdint.h>

/* NTP packet (48 bytes) */
typedef struct {
    uint8_t  li_vn_mode;      /* 0x1B = LI=0, VN=3, Mode=3 (client) */
    uint8_t  stratum;
    uint8_t  poll;
    uint8_t  precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint64_t ref_ts;
    uint64_t orig_ts;
    uint64_t recv_ts;
    uint64_t trans_ts;
} NTP_PACKET;

/* Public DNS server for DNS4 configuration */
static const EFI_IPv4_ADDRESS DnsServer = {{192, 168, 1, 1}};

/* NTP hostname to resolve via DNS4 */
static CHAR16 *NtpHostname = L"asia.pool.ntp.org";

/* EFI GUIDs (already defined in <efi.h> from gnu-efi) */
extern EFI_GUID gEfiIp4Config2ProtocolGuid;
extern EFI_GUID gEfiDns4ServiceBindingProtocolGuid;
extern EFI_GUID gEfiUdp4ServiceBindingProtocolGuid;
extern EFI_GUID gEfiIp4ProtocolGuid;  /* Used to get local IP after DHCP */

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    EFI_IP4_CONFIG2_PROTOCOL *Ip4Config2 = NULL;
    EFI_DNS4_SERVICE_BINDING_PROTOCOL *Dns4Sb = NULL;
    EFI_DNS4_PROTOCOL *Dns4 = NULL;
    EFI_UDP4_SERVICE_BINDING_PROTOCOL *Udp4Sb = NULL;
    EFI_UDP4_PROTOCOL *Udp4 = NULL;
    EFI_HANDLE DnsChild = NULL;
    EFI_HANDLE UdpChild = NULL;

    InitializeLib(ImageHandle, SystemTable);
    Print(L"UEFI Standalone Networking Demo (DHCP + DNS + NTP)\n");
    Print(L"Intel-compatible NIC supported via UEFI firmware protocols\n\n");

    /* ==================== 1. DHCP via EFI_IP4_CONFIG2_PROTOCOL ==================== */
    Status = SystemTable->BootServices->LocateProtocol(&gEfiIp4Config2ProtocolGuid, NULL, (VOID**)&Ip4Config2);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to locate IP4Config2: %r\n", Status);
        goto Done;
    }

    /* Set policy to DHCP */
    EFI_IP4_CONFIG2_POLICY Policy = Ip4Config2PolicyDhcp;
    Status = Ip4Config2->SetData(Ip4Config2, Ip4Config2DataTypePolicy, sizeof(Policy), &Policy);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to set DHCP policy: %r\n", Status);
        goto Done;
    }

    /* Wait up to 10 seconds for DHCP to complete (poll InterfaceInfo) */
    Print(L"Requesting DHCP IP address...\n");
    for (UINTN i = 0; i < 200; i++) {  /* ~10s with 50ms stalls */
        EFI_IP4_CONFIG2_INTERFACE_INFO *Info = NULL;
        UINTN InfoSize = 0;
        Status = Ip4Config2->GetData(Ip4Config2, Ip4Config2DataTypeInterfaceInfo, &InfoSize, NULL);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            Status = SystemTable->BootServices->AllocatePool(EfiBootServicesData, InfoSize, (VOID**)&Info);
            if (!EFI_ERROR(Status)) {
                Status = Ip4Config2->GetData(Ip4Config2, Ip4Config2DataTypeInterfaceInfo, &InfoSize, Info);
                if (!EFI_ERROR(Status) && Info->Ip4Address.Addr[0] != 0) {
                    Print(L"DHCP Success! IP: %d.%d.%d.%d\n",
                          Info->Ip4Address.Addr[0], Info->Ip4Address.Addr[1],
                          Info->Ip4Address.Addr[2], Info->Ip4Address.Addr[3]);
                    SystemTable->BootServices->FreePool(Info);
                    break;
                }
                SystemTable->BootServices->FreePool(Info);
            }
        }
        SystemTable->BootServices->Stall(50000);  /* 50ms */
    }

    /* ==================== 2. DNS via EFI_DNS4_PROTOCOL (resolve pool.ntp.org) ==================== */
    Status = SystemTable->BootServices->LocateProtocol(&gEfiDns4ServiceBindingProtocolGuid, NULL, (VOID**)&Dns4Sb);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to locate DNS4 Service Binding: %r\n", Status);
        goto Done;
    }

    Status = Dns4Sb->CreateChild(Dns4Sb, &DnsChild);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to create DNS4 child: %r\n", Status);
        goto Done;
    }

    Status = SystemTable->BootServices->OpenProtocol(DnsChild, &gEfiDns4ProtocolGuid, (VOID**)&Dns4,
                                                     ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to open DNS4 protocol: %r\n", Status);
        goto Done;
    }

    /* Configure DNS4 with public server */
    EFI_DNS4_CONFIG_DATA DnsConfig = {0};
    DnsConfig.DnsServerCount = 1;
    DnsConfig.DnsServerList = (EFI_IPv4_ADDRESS*)&DnsServer;
    DnsConfig.EnableDnsCache = TRUE;
    DnsConfig.Protocol = EFI_DNS_PROTOCOL_IPV4;
    Status = Dns4->Configure(Dns4, &DnsConfig);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to configure DNS4: %r\n", Status);
        goto Done;
    }

    Print(L"Resolving %s via DNS4...\n", NtpHostname);
    EFI_DNS4_HOSTNAME_TO_IP_TOKEN Token = {0};
    Token.HostName = NtpHostname;
    Status = Dns4->HostNameToIp(Dns4, &Token);
    if (EFI_ERROR(Status)) {
        Print(L"DNS resolution failed: %r\n", Status);
        goto Done;
    }

    /* Wait for DNS response (simple polling) */
    while (Token.Status == EFI_NOT_READY) {
        SystemTable->BootServices->Stall(100000);
    }
    if (EFI_ERROR(Token.Status)) {
        Print(L"DNS query failed: %r\n", Token.Status);
        goto Done;
    }

    EFI_IPv4_ADDRESS NtpIp = Token.IpList->IpAddress.v4;
    Print(L"NTP server resolved to %d.%d.%d.%d\n",
          NtpIp.Addr[0], NtpIp.Addr[1], NtpIp.Addr[2], NtpIp.Addr[3]);

    /* ==================== 3. NTP via EFI_UDP4_PROTOCOL ==================== */
    Status = SystemTable->BootServices->LocateProtocol(&gEfiUdp4ServiceBindingProtocolGuid, NULL, (VOID**)&Udp4Sb);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to locate UDP4 Service Binding: %r\n", Status);
        goto Done;
    }

    Status = Udp4Sb->CreateChild(Udp4Sb, &UdpChild);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to create UDP4 child: %r\n", Status);
        goto Done;
    }

    Status = SystemTable->BootServices->OpenProtocol(UdpChild, &gEfiUdp4ProtocolGuid, (VOID**)&Udp4,
                                                     ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to open UDP4 protocol: %r\n", Status);
        goto Done;
    }

    /* Configure UDP4 (local IP from DHCP, remote = NTP server port 123) */
    EFI_UDP4_CONFIG_DATA UdpConfig = {0};
    UdpConfig.AcceptAnySource = TRUE;
    UdpConfig.AllowDuplicatePort = FALSE;
    UdpConfig.StationAddress.Addr[0] = 0;  /* Use DHCP-assigned IP */
    UdpConfig.StationPort = 0;             /* Any local port */
    UdpConfig.RemoteAddress = NtpIp;
    UdpConfig.RemotePort = 123;
    Status = Udp4->Configure(Udp4, &UdpConfig);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to configure UDP4: %r\n", Status);
        goto Done;
    }

    /* Prepare NTP request packet */
    NTP_PACKET Request = {0};
    Request.li_vn_mode = 0x1B;  /* Client mode */
    Request.stratum = 0;
    Request.poll = 6;
    Request.precision = 0xEC;

    EFI_UDP4_TRANSMIT_DATA TxData = {0};
    TxData.RemoteAddress = NtpIp;
    TxData.RemotePort = 123;
    TxData.DataLength = sizeof(NTP_PACKET);
    TxData.FragmentCount = 1;
    EFI_UDP4_FRAGMENT_DATA Fragment = {sizeof(NTP_PACKET), &Request};
    TxData.FragmentTable = &Fragment;

    EFI_UDP4_COMPLETION_TOKEN TxToken = {0};
    TxToken.Packet.TxData = &TxData;

    Status = Udp4->Transmit(Udp4, &TxToken);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to transmit NTP request: %r\n", Status);
        goto Done;
    }

    /* Wait for transmit complete */
    while (TxToken.Status == EFI_NOT_READY) {
        Udp4->Poll(Udp4);
        SystemTable->BootServices->Stall(10000);
    }

    /* Receive NTP response */
    EFI_UDP4_RECEIVE_DATA RxData = {0};
    EFI_UDP4_COMPLETION_TOKEN RxToken = {0};
    RxToken.Packet.RxData = &RxData;

    Status = Udp4->Receive(Udp4, &RxToken);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to queue NTP receive: %r\n", Status);
        goto Done;
    }

    /* Poll for response */
    Print(L"Waiting for NTP response...\n");
    for (UINTN i = 0; i < 100; i++) {  /* 1 second timeout */
        Udp4->Poll(Udp4);
        if (RxToken.Status != EFI_NOT_READY) break;
        SystemTable->BootServices->Stall(10000);
    }

    if (RxToken.Status == EFI_SUCCESS && RxData.DataLength >= sizeof(NTP_PACKET)) {
        NTP_PACKET *Response = (NTP_PACKET*)RxData.FragmentTable[0].FragmentBuffer;
        /* Convert NTP timestamp (seconds since 1900) to Unix time */
        uint64_t NtpSeconds = (Response->trans_ts >> 32) - 2208988800ULL;
        Print(L"NTP Success! UTC time: %d seconds since 1970 (Unix epoch)\n", (UINT32)NtpSeconds);
    } else {
        Print(L"NTP receive failed or timeout: %r\n", RxToken.Status);
    }

    Print(L"\nNetworking demo complete. Halting...\n");

Done:
    /* Cleanup (best effort) */
    if (Udp4) Udp4->Configure(Udp4, NULL);
    if (Dns4) Dns4->Configure(Dns4, NULL);
    if (DnsChild) Dns4Sb->DestroyChild(Dns4Sb, DnsChild);
    if (UdpChild) Udp4Sb->DestroyChild(Udp4Sb, UdpChild);

    SystemTable->BootServices->Stall(10000000);  /* 10-second pause */
    return EFI_SUCCESS;
}
