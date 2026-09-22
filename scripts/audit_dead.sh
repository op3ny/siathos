#!/bin/bash
# Auditoria: conta ocorrencias de funcoes/simbolos para achar codigo morto.
cd "$(cd "$(dirname "$0")/.." && pwd)"
for fn in synallagma_start_all synallagma_load_defaults pit_ack load_idt \
          synallagma_guards_folder consent_accepted context_switch_exit \
          kinesis_register slow_down outw_port fs_has_children \
          ipc_send_blocking paging_load paging_map_user; do
    n=$(grep -rn "$fn" src/kernel 2>/dev/null | wc -l)
    echo "$fn: $n"
done
