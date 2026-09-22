# Siaht OS — BLUEPRINT TÉCNICO v0.1

## 1. Visão
Micro OS original, não simulação. Kernel, FS, comandos, init, pacotes inéditos. Ancap implícito na mecânica, não no discurso.

## 2. Arquitetura Geral
```
ThaisBoot (futuro) / Limine (temp) → thais-kernel (micro) → thais-init (contratos) → thais-sh + runtimes
```

### 2.1 Microkernel (src/kernel/)
- Linguagem: C17 + ASM x86_64, freestanding, -ffreestanding -nostdlib
- Memória: pmm (bitmap) + vmm (paging 4-level, HHMD Limine)
- Capabilities: `cap_t` = token opaco (u64 id + direitos bitmask). Sem uid 0. Processo só acessa `aisthesis/*` se tem cap.
- Scheduler: round-robin preemptivo via PIT/APIC (fase 1 cooperativo)
- IPC: message passing por contrato (synallagma)
- Drivers: framebuffer GOP (Limine), serial, PIC, teclado PS/2 (depois USB/virtio)
- Syscalls: `synallagma_create`, `cap_grant`, `praxis_exec`, `aisthesis_read/write`, `kinesis_spawn`

### 2.2 Filesystem
- Boot: FAT32 (ESP) — obrigatório UEFI
- Root: ThaFS v0 (inicialmente ramfs + FAT32, depois FS próprio com inodes gregos)
- Layout lógico já exposto ao userspace mesmo sobre FAT32 (via mounts virtuais)

### 2.3 Init por Contratos Voluntários
`/synallagma/*.pacto`:
```toml
[contrato]
nome = "aisthesis.teclado"
acao = "/techne/teclado"
voluntario = true
revogavel = true
caps = ["aisthesis.input"]
on_start = "praxia /techne/teclado"
```
Kernel não força start; `thais-init` propõe, usuário aceita. Sem dependência coercitiva.

### 2.4 Emporion Federado (pacotes)
- Formato `.thais` (tar + manifest `emporion.toml`)
- Repo é um `agora` (URL git/http). `agora sync` federa. `emporion install <pacote>` resolve via mercado (escolha do usuário, sem lock central)
- Baseado em `musl` para portabilidade

### 2.5 Runtimes Híbridos
Fase 1 (atual): musl + libthais (wrappers capability) → compilar CPython 3.12 e Node 20 nativamente para Siaht OS.
Fase 2: camada `linux-compat` traduz syscalls Linux (open/read/write) para caps, permitindo rodar binários Linux estáticos.

## 3. Boot Splash Específico
Framebuffer 1024x768, fonte PSF 8x16, caixa 60x9 chars centralizada, borda dupla.
Lógica:
- 0-49% spinner |/-\\ a 60ms
- 50-100% barra [====>   ] + porcentagem
Frases: array 4 strings, `rand() % 4` no boot.

## 4. Comandos thais-sh (src/userspace/thais-sh/)
Shell próprio, não bash. Parser simples.
Comandos implementados fase 1:
- horasis [caminho] (-l, -a)
- metabasis <caminho>
- ktisis <arquivo|dir> [-d]
- anairesis (rm)
- praxia <bin> (exec)
- graphe <arquivo> (cat)
- nomos <arquivo> (edit/view config)
- synallagma list/start/stop <contrato>
- emporion search/install/remove
- agora sync
- aisthesis list
- kinesis (ps)
- thais, op3n, op3ny (easter eggs com arte ASCII + frases)

## 5. Estrutura de Pastas Repo
```
Thais-OS/
 ├─ docs/BLUEPRINT.md
 ├─ src/
 │   ├─ boot/thaisboot/ (stub futuro bootloader)
 │   ├─ kernel/ (microkernel)
 │   └─ userspace/thais-sh/, thais-init/, thais-login/
 ├─ limine/ (git submodule temp)
 ├─ gnu-efi/ (se precisar)
 ├─ build/ (out)
 └─ Makefile, linker.ld, limine.conf
```

## 6. Build & QEMU
Toolchain: x86_64-elf-gcc (WSL2) ou clang+lld. ISO híbrida UEFI+BIOS via limine.

## 7. Segurança Ancap
- Sem root. Primeiro processo (init) tem caps iniciais e delega via `cap_grant` voluntário.
- Cada driver é processo isolado com caps mínimas.
- Revogação explícita: `synallagma revoke <cap>`.

## 8. Próximos Passos Imediatos
Implementar kernel mínimo que já mostra splash + login + shell, mesmo sem FS completo.
