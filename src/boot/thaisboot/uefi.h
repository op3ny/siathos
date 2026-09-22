#ifndef THAISBOOT_UEFI_H
#define THAISBOOT_UEFI_H
#include <stdint.h>

typedef uint8_t  efi_bool_t;
typedef uint16_t efi_char16_t;
typedef void    *efi_handle_t;
typedef uint64_t efi_status_t;
typedef uint64_t efi_physical_addr;
typedef uint64_t efi_virtual_addr;

#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define EFI_NOT_FOUND 0x8000000000000002ULL
#define EFI_NO_MEDIA 0x8000000000000003ULL
#define EFI_DEVICE_ERROR 0x8000000000000007ULL
#define EFI_VOLUME_CORRUPTED 0x8000000000000008ULL
#define EFI_OUT_OF_RESOURCES 0x8000000000000009ULL
#define EFI_NOT_READY 0x8000000000000010ULL
#define EFI_INVALID_PARAMETER 0x8000000000000002ULL

typedef struct {
    uint64_t sig; uint32_t revision; uint32_t header_size; uint32_t crc32; uint32_t reserved;
} efi_table_header_t;

typedef struct {
    uint32_t type; uint32_t pad; uint64_t phys_start; uint64_t virt_start; uint64_t num_pages; uint64_t attr;
} efi_memory_descriptor_t;

typedef struct { uint32_t data1; uint16_t data2; uint16_t data3; uint8_t data4[8]; } efi_guid_t;

// --- protocol/device structs ---
typedef struct efi_file_protocol efi_file_protocol_t;
typedef struct efi_simple_fs efi_simple_fs_t;
typedef struct efi_gop efi_gop_t;
typedef struct efi_system_table efi_system_table_t;

typedef struct efi_loaded_image {
    uint32_t revision; efi_handle_t parent; efi_handle_t self; efi_system_table_t *system;
    efi_handle_t device; void *file_path; uint64_t reserved; uint32_t load_opts_size; void *load_opts;
} efi_loaded_image_t;

struct efi_file_protocol {
    efi_status_t (*open)(efi_file_protocol_t *self, efi_file_protocol_t **new, uint16_t *name, uint64_t mode, uint64_t attr);
    efi_status_t (*close)(efi_file_protocol_t *self);
    efi_status_t (*delete)(efi_file_protocol_t *self);
    efi_status_t (*read)(efi_file_protocol_t *self, uint64_t *size, void *buf);
    efi_status_t (*write)(efi_file_protocol_t *self, uint64_t *size, void *buf);
    efi_status_t (*get_pos)(efi_file_protocol_t *self, uint64_t *pos);
    efi_status_t (*set_pos)(efi_file_protocol_t *self, uint64_t pos);
    efi_status_t (*get_info)(efi_file_protocol_t *self, efi_guid_t *type, uint64_t *isize, void *buf);
    efi_status_t (*set_info)(efi_file_protocol_t *self, efi_guid_t *type, uint64_t isize, void *buf);
    efi_status_t (*flush)(efi_file_protocol_t *self);
};

typedef struct efi_file_info {
    uint64_t size; uint64_t file_size; uint64_t phys_size; uint64_t attr;
    uint16_t filename[1];
} efi_file_info_t;

struct efi_simple_fs {
    uint64_t revision;
    efi_status_t (*open_volume)(efi_simple_fs_t *self, efi_file_protocol_t **root);
};

typedef struct efi_gop_mode_info {
    uint32_t max_mode; uint32_t mode; uint32_t fmt; uint32_t bpp;
    uint32_t res1; uint32_t hres, vres; uint32_t res2;
} efi_gop_mode_info_t;
typedef struct efi_gop_mode {
    uint32_t max_mode; uint32_t mode; efi_gop_mode_info_t *info; uint64_t size_of_info; void *fb_base; uint64_t fb_size;
} efi_gop_mode_t;
struct efi_gop {
    efi_status_t (*query_mode)(efi_gop_t *self, uint32_t mode, uint64_t *sz, efi_gop_mode_info_t **info);
    efi_status_t (*set_mode)(efi_gop_t *self, uint32_t mode);
    efi_status_t (*blt)(void);
    efi_gop_mode_t *mode;
};

// --- BootServices (ABI exata UEFI) ---
typedef struct efi_boot_services {
    efi_table_header_t hdr;
    efi_status_t (*raise_tpl)(uint64_t new_tpl);
    void (*restore_tpl)(uint64_t old_tpl);
    efi_status_t (*allocate_pages)(uint32_t type, uint32_t mem_type, uint64_t pages, efi_physical_addr *mem);
    efi_status_t (*free_pages)(efi_physical_addr mem, uint64_t pages);
    efi_status_t (*get_memory_map)(uint64_t *memmap_size, efi_memory_descriptor_t *memmap, uint64_t *map_key, uint64_t *desc_size, uint32_t *desc_ver);
    efi_status_t (*allocate_pool)(uint32_t pool_type, uint64_t size, void **buf);
    efi_status_t (*free_pool)(void *buf);
    efi_status_t (*create_event)(uint32_t type, uint64_t notify_tpl, void *notify_fn, void *ctx, void *event);
    efi_status_t (*set_timer)(void *event, uint32_t type, uint64_t trigger);
    efi_status_t (*wait_for_event)(uint64_t num, void **events, uint64_t *idx);
    efi_status_t (*signal_event)(void *event);
    efi_status_t (*close_event)(void *event);
    efi_status_t (*check_event)(void *event);
    efi_status_t (*install_protocol_interface)(void *h, efi_guid_t *g, uint32_t it, void *iface);
    efi_status_t (*reinstall_protocol_interface)(void *h, efi_guid_t *g, void *old, void *new);
    efi_status_t (*uninstall_protocol_interface)(void *h, efi_guid_t *g, void *iface);
    efi_status_t (*handle_protocol)(efi_handle_t h, efi_guid_t *g, void **iface);
    void *reserved1;
    efi_status_t (*register_protocol_notify)(efi_guid_t *g, void *event, void **reg);
    efi_status_t (*locate_handle)(uint32_t search, efi_guid_t *g, void *key, uint64_t *bsz, efi_handle_t *buf);
    efi_status_t (*locate_device_path)(efi_guid_t *g, void *dp, efi_handle_t *h);
    efi_status_t (*install_configuration_table)(efi_guid_t *g, void *tbl);
    efi_status_t (*load_image)(uint8_t boot, efi_handle_t parent, void *dp, void *src, uint64_t sz, efi_handle_t *img);
    efi_status_t (*start_image)(efi_handle_t img, uint64_t *exit_idx, void *exit_data);
    efi_status_t (*exit)(efi_handle_t img, efi_status_t code, uint64_t sz, uint16_t *data);
    efi_status_t (*unload_image)(efi_handle_t img);
    efi_status_t (*exit_boot_services)(efi_handle_t img, uint64_t map_key);
    efi_status_t (*get_next_monotonic_count)(uint64_t *cnt);
    efi_status_t (*stall)(uint64_t microsec);
    efi_status_t (*set_watchdog_timer)(uint64_t timeout, uint64_t code, uint64_t data, uint16_t *watch);
    efi_status_t (*connect_controller)(efi_handle_t ctrl, efi_handle_t *drv, void *dp, uint8_t recursive);
    efi_status_t (*disconnect_controller)(efi_handle_t ctrl, efi_handle_t drv, efi_handle_t child);
    efi_status_t (*open_protocol)(efi_handle_t h, efi_guid_t *g, void **iface, efi_handle_t agent, efi_handle_t ctrl, uint32_t attr);
    efi_status_t (*close_protocol)(efi_handle_t h, efi_guid_t *g, efi_handle_t agent, efi_handle_t ctrl);
    efi_status_t (*open_protocol_information)(efi_handle_t h, efi_guid_t *g, void *buf);
    efi_status_t (*protocols_per_handle)(efi_handle_t h, efi_guid_t ***guids, uint64_t *count);
    efi_status_t (*locate_handle_buffer)(uint32_t search, efi_guid_t *g, void *key, uint64_t *count, efi_handle_t **buf);
    efi_status_t (*locate_protocol)(efi_guid_t *g, void *reg, void **iface);
    efi_status_t (*install_multiple_protocol_interfaces)(void *h, ...);
    efi_status_t (*uninstall_multiple_protocol_interfaces)(void *h, ...);
    efi_status_t (*calculate_crc32)(void *data, uint64_t sz, uint32_t *crc);
    efi_status_t (*copy_mem)(void *dst, void *src, uint64_t sz);
    efi_status_t (*set_mem)(void *buf, uint64_t sz, uint8_t val);
} efi_boot_services_t;

typedef struct efi_system_table {
    efi_table_header_t hdr;
    uint16_t *console_in; uint16_t *console_out; uint16_t *console_err;
    void *input; void *output; void *stderr;
    efi_handle_t *stdin; efi_handle_t *stdout; efi_handle_t *stderr2;
    efi_boot_services_t *boot;
    void *runtime;
} efi_system_table_t;

static const efi_guid_t EFI_LOADED_IMAGE_PROTOCOL_GUID = {0x5B1B31A1,0x9562,0x11D2,{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static const efi_guid_t EFI_SIMPLE_FILE_SYSTEM_GUID = {0x964E5B22,0x6459,0x11D2,{0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static const efi_guid_t EFI_GRAPHICS_OUTPUT_GUID = {0x9042A9DE,0x23DC,0x4A38,{0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A}};
static const efi_guid_t EFI_FILE_INFO_GUID = {0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

#endif
