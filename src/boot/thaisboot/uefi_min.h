#ifndef THAIS_UEFI_MIN_H
#define THAIS_UEFI_MIN_H
typedef unsigned long long u64;
typedef unsigned short u16;
typedef u64 efi_status_t;
typedef unsigned long long efi_handle_t;
typedef u64 efi_physical_addr;

typedef struct {
    u64 Reset;
    u64 OutputString;
} efi_simple_text_output_protocol_t;

typedef struct {
    char pad[0x60];
    efi_simple_text_output_protocol_t *ConOut;
} efi_system_table_t;

typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_output_string)(efi_simple_text_output_protocol_t*, u16*);

// EFI boot services signatures (ms_abi)
typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_alloc_pages)(int, int, u64, efi_physical_addr*);
typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_free_pages)(efi_physical_addr, u64);
typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_get_mem_map)(u64*, void*, u64*, u64*, u32*);
typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_exit_bs)(efi_handle_t, u64);
typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_locate_proto)(void*, void*, void**);
typedef efi_status_t (__attribute__((ms_abi)) *efi_fn_handle_proto)(efi_handle_t, void*, void**);

typedef struct {
    char pad[0x38]; // ate 0x38 tem tabelas; BootServices em 0x60? nao, SystemTable tem BootServices e RuntimeServices
} efi_boot_services_t;
#endif
