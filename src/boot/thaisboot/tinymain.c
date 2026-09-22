#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    Print(L"THAISTEST OK\n");
    Print(L"BS=%p\n", BS);
    UINTN n = 0;
    EFI_HANDLE *h = 0;
    EFI_STATUS s = BS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, 0, &n, &h);
    Print(L"lhb=%r n=%u\n", s, n);
    EFI_HANDLE *h2 = 0;
    UINTN n2 = 0;
    EFI_STATUS s2 = BS->LocateHandleBuffer(AllHandles, 0, 0, &n2, &h2);
    Print(L"allh=%r n2=%u\n", s2, n2);
    return EFI_SUCCESS;
}
