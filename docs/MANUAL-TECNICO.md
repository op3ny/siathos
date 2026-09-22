# Siath OS — Manual Técnico

> Documento técnico de referência do Siath OS (kernel `thais-kernel` + bootloader `ThaisBoot`).
> Release: **Siath OS v1.0.0 alpha — "Independent System"** · ABI 1.6 (SYS_COUNT=38) · 2026-09-22.
> Para o "estado honesto" por área (real vs. objetivo) ver [`STATUS.md`](STATUS.md).

## 1. Visão geral e decisões de projeto

- **Plataforma-alvo:** x86_64, UEFI apenas (sem caminho BIOS/CSM).
- **Linguagem:** C freestanding + NASM Assembly; toolchain `gcc`/`ld`/`nasm`, `-mcmodel=kernel`, higher-half.
- **Modelo:** microkernel em consolidação com *capability-based security* (sem superusuário). Serviços migram do kernel para processos ring 3 sob demanda; o kernel permanentemente detém: memória, processos, escalonador, IPC, syscalls, FS (primitivo) e gates de capability.
- **Zero dependências de terceiros:** bootloader, kernel, libc mínima de usuário (`spud.h`) e tooling (`genapp.py`, `mkiso.sh`, `run_auto_test.py`) são originais do repositório.
- **Números da release:** kernel ≈ 6.774 linhas (C+ASM), bootloader ≈ 254 linhas, 18 apps userspace, 5 serviços ring 3, 38 syscalls, 13 markers de regressão.

## 2. Boot — ThaisBoot (UEFI)

`src/boot/thaisboot/main.c` — bootloader PE32+ compilado com GNU-EFI.

1. Init do UEFI, protocolo GOP obtém o **framebuffer** (passado ao kernel para o splash/console).
2. Lê `kernel.elf` da ESP (abre o device path onde o loader foi carregado).
3. Coleta o **memory map** e chama `ExitBootServices`.
4. Salta para o kernel em 64 bits, passando um bloco `bootinfo` (magia `THAIS_BOOT_MAGIC`, kernel_phys, framebuffer, mapa) + page table mínima higher-half (verificação `KTHAIS_BOOT_OK`).
5. O kernel entra em `entry` (`arch/x86_64/boot.asm`), monta GDT/TSS/paging próprios e segue a inicialização.

Detalhe do linker: `thaisboot.lds`/`efi.ld` definem as seções PE; debug tags (`post pml4_1FF`, `post pdhh0`, `post code0`) confirmam no serial o estado do salto.

## 3. Memória

| Subsistema | Implementação |
|---|---|
| **PMM** (`pmm`) | Bitmap de páginas físicas (4KB); memória acima de 2GB é ignorada por limitação do bitmap fixo (log: `PMM: 524288 paginas (2048 MB) gerenciadas`). |
| **Heap** (`kmalloc`/`kfree`) | Split/coalesce de blocos, detecção de corrupção; 4MB prontos no boot (`[oikos] heap: 4MB prontos`). |
| **Paging** | 4 níveis (PML4/PDPT/PD/PT), identidade + higher-half. Espaço por processo via **cópia rasa do PML4** + troca de **CR3** no switch de contexto. |
| **Páginas de usuário** | `paging_map_user` mapeia 4KB por processo com invalidação TLB — **W^X**: código `0x5` (P/US/X), stack `0x7|NX`; **EFER.NXE** habilitado. |
| **Teardown/GC** | `proc_gc_run` libera kernel stack, PML4, páginas de usuário e regiões ELF de processos mortos (nunca o espaço ativo). |

Iso, **sem COW** e **sem demand paging** (base sólida, ainda sem as otimizações).

## 4. Processos e escalonador

- **PCB** `process_t`: PID, nome, `rights` (capabilities), estado, stack, contexto, parent.
- **Estados:** CREATED → READY → RUNNING → BLOCKED → TERMINATED.
- **Context switch** em assembly (`ctxswitch.asm`): salva/restaura callee-saved, RSP, RIP, CR3.
- **Escalonador:** round-robin **preemptivo** (tick de 100Hz do PIT) + yield cooperativo; `sched_yield`/`sched_block`/`sched_wakeup`/`sched_tick`.
- **Ring 3:** `proc_create_user` cria processo de usuário — espaço próprio, kernel stack + user stack, frame `iretq`, `enter_user`/`user_trampoline`, `tss.rsp0` por processo no `sched_prep`. Resume sob preempção/syscall pelo modelo **full-frame/iretq** (stub do ISR parka `hw_rsp`; bug fix histórico fechado — ver §11).
- **Passagem de args:** `proc_create_user_args` entrega `argc/argv` em `rdi/rsi` na 1ª execução (flag `ran`).

## 5. Interrupções e syscalls (ABI 1.6)

- **IDT** com 48 vetores (32 exceções + 16 IRQ), moderador do teclado no kernel + driver ring 3.
- **PIC** remapeado; **PIT** 100Hz.
- **Syscall:** gate `int 0x80` (DPL=3) que salva os 15 GPRs no frame e só sobrescreve `rax` no retorno. Tabela completa e gates de capability em [`SYSCALL-ABI.md`](SYSCALL-ABI.md) — **SYS_COUNT = 38**.

Exemplos de syscalls (0..37): `SYS_WRITE`, `SYS_FS_READ/LIST/CREATE/WRITE/REMOVE/AUDIT`, `SYS_IPC_*`, `SYS_MMAP`, `SYS_DEV_MAP`, `SYS_AUTH_*` (fallback + `SYS_AUTH_HASH`/`TICKET_ISSUE`), `SYS_SESSION_LOGIN`, `SYS_CONTRACT_LIST`, `SYS_SYSINFO/UPTIME/GETPPID/PROC_LIST/COUNT`.

## 6. IPC

- **Mailboxes** por processo: `ipc_send`/`ipc_receive`/`ipc_reply`, bloqueio, `CAP_IPC`.
- Protocolos por serviço (ex.: `IPC_TYPE_AUTH_*` 20-26, `IPC_TYPE_FS_*` 11-14, `IPC_TYPE_SYN_*` 30-34).
- Regra **svc-first + fallback syscall**: o kernel nunca bloqueia esperando um serviço (one-flow-per-boot; kernel = enforcement point).

## 7. Sistema de arquivos (RAMFS/VFS)

- **RAMFS** com ownership, permissões (`world_read/write`) e **caps por nó**; **VFS** sobre ele (normalização `.`/`..`, validação, API `vfs_*`).
- Árvore de diretórios grega (§ do README) montada no boot (`[arkhe] arvore montada`).
- **Exec desacoplado do RAMFS:** apps do sistema (`/bin`, `/kormi`, `/praxis`) vivem embutidos em `s_mods[]` e executam via `exec_embedded_args` — **embedded-first**, FS usado apenas para arquivos/scripts de usuário. O boot não depende do `fs_read`.
- **Serviço `fsd` (ring 3):** dono do **store userspace** de `/paradosis` e `/nomos` (cópia no heap do serviço, semeada do kernel no boot, marker `[fsd] store ring3`), superfície IPC completa (READ/LIST/CREATE/WRITE/REMOVE/AUDIT) + trilho de auditoria próprio. Kernel (`SYS_FS_*`) = primitivo/fallback.

## 8. Autenticação

- **Hash:** **PBKDF2-HMAC-SHA256**, `AUTH_HASH_ITER=100000`, salt aleatório 16B (32 hex) por conta. `crypto.c` validado contra vetores de referência; custo real ~4.4–5.6ms/hash em QEMU (TCG).
- **Serviço `authd` (ring 3, ABI 1.5):** dono da store de credenciais e da política. 1º usuário = soberano (CAP_ALL) e nunca é removido. Login de admin autenticado (`admin_authed`) é exigido para criar/remover/alterar senhas de outros usuários.
- **Sessão por ticket:** `SYS_AUTH_TICKET_ISSUE` (36, exige `CAP_USER_ADMIN`) emite `{nome, caps, token}`; `SYS_SESSION_LOGIN(name, a2=1, a3=tok)` consome o ticket e monta a sessão — os caps vêm do serviço, não da store do kernel.
- **Hash via syscall (`SYS_AUTH_HASH`=37):** o PBKDF2 roda no kernel com **IRQs desligadas** (`disable_interrupts()`), eliminando corrupção por preempção durante o custo cripto de ~5ms (padrão validado; histórico do bug em `STATUS.md §3`).
- Kernel (`SYS_AUTH_*`/`auth.c`) = **fallback** quando o serviço não está disponível (um fluxo por boot).

## 9. Contratos de serviço — Synallagma

- Arquivos `/synallagma/*.pacto` (TOML-like) com campos `caminho`, `nome`, `leitura`, `escrita`, `execucao`, `voluntario`, `revogavel`.
- Consentimento por sessão no login; enforcement no `vfs_open`/sessão (`synallagma_*` no kernel).
- **Serviço `synd` (ring 3, ABI 1.6):** dono do **store userspace de contratos** — semeado via `SYS_CONTRACT_LIST` no boot (marker `[synd] store ring3`), superfície IPC `IPC_TYPE_SYN_LIST/READ/CONSENT/REVOKE/AUDIT` (30-34), trilho de auditoria no heap do serviço. Kernel = primitivo/fallback + enforcement (mesmo modelo authd/fsd).

## 10. Execução de programas (ELF64)

- **Loader ELF64:** valida header, carrega `PT_LOAD` dentro das restrições, cria PCB, prepara entry point; usa `kmalloc`/`kfree` (sem buffer estático).
- **Apps userspace:** `src/userspace/<app>/` → ELF PIC linkado fixo em `0x200000000000` → embutido em `src/kernel/<app>_elf.h` via `scripts/genapp.py` → executado em ring 3 por `exec_elf`/`apps_try_run`. Manifesto (`manifest.cfg`) define nome, alias e se é serviço (`kormi=1`).
- **18 apps** embutidos (incl. `ls`, `cat`, `echo`, `ps`, `mem`, `heap`, `wtest`, `phrourio`, `svctest`, `fetch`, `hello` + serviços `authd`, `fsd`, `devd`, `odigos`, `synd`, `spoudazo`, `praxia`).
- **Shell `praxia`** (ring 3) + console de login/shell → app via `apps_try_run_args` com `argc/argv`.

## 11. Segurança

- **Capabilities:** gates reais em `vfs_open` e em syscalls (`syscall_cap`), baseadas no contrato e nos `rights` do processo (`cap_check`/`grant`/`revoke`).
- **Validação de ponteiros de usuário:** `user_ptr_ok`/`user_copy`/`user_str` conferem `[ptr, ptr+n)` contra as regiões `umap_*` do processo — fecha "apontar para kernel e vazar". Processos ring 0 (umap_count=0) não passam pelo gate.
- **W^X + NXE** e **GC de processo morto** (§3).
- **Controles cripto sob preempção** (`cli/sti` no hash) e **fix do ISR** (`isr_common_stub` não clobbera `r10` no iretq — causa raiz histórica de corrupção de retorno de IRQ; regressão 16/16).
- Sem senha em texto plano; sem `root` onipotente; login exige consentimento dos contratos.

## 12. Drivers

- **Framebuffer** GOP (splash + console), **serial** COM1 (log de boot).
- **PS/2** (teclado, polling, US intl) e **USB xHCI** (teclado; `devd` mapeia BAR MMIO via `SYS_DEV_MAP`; `odigos_pliktrologiou` expõe scancodes → serviço `kbd`).
- **PIT** (timer 100Hz), **PCI** (enumeração; xHCI BAR0).

## 13. Build, execução e regressão

```bash
make                      # build de produção (sem apps de teste)
make TEST=1               # inclui apps de demonstração + self-tests
make TEST=1 RING3=1       # + demo ring 3
make iso                  # gera build/siath.iso
make run                  # QEMU UEFI com display
make run-headless         # QEMU headless (serial file:build/boot.log) — PASS = chega ao "Novo admin"
```

- **Trocar `TEST`/`RING3` exige** `rm -f src/kernel/*.o src/kernel/arch/x86_64/*.o` (o Makefile não rastreia mudança de CFLAGS).
- **Regressão serial** `scripts/_runtest.sh N` (QEMU headless, `fat:rw`, sem ISO): valida os **13 markers** por boot (`[appctl]`, `[fsd] heap-probe/coalesce/store ring3`, `[svctest] MARCO 9 + auth IPC + fsd IPC ops + synd IPC`, `[wtest]`, `[phrourio]`, `[authd] PBKDF2 self-test + servico 'auth'`, `[synd] store ring3`).
- **Automação E2E:** `scripts/run_auto_test.py` automatiza bootstrap+login+consentimento via monitor QEMU (`sendkey`).

Artefatos da release (medidos): `build/siath.iso` 34MB · `build/kernel.elf` 576KB · `build/BOOTX64.EFI` 52KB.

## 14. Estado por subsistema (resumo)

| Área | Maturidade |
|---|---|
| Boot UEFI (ThaisBoot) | consolidado |
| Scheduler/context switch (preemptivo, full-frame/iretq) | consolidado |
| PMM · heap · paging (W^X/NXE) · GC | consolidado |
| VFS/RAMFS · capabilities | consolidado |
| Auth (PBKDF2) + serviço `authd` ring 3 (ABI 1.5) | consolidado |
| Contratos Synallagma + serviço `synd` ring 3 (ABI 1.6) | consolidado |
| Serviço `fsd` ring 3 (store userspace `/paradosis`,`/nomos` + auditoria) | consolidado |
| IPC + syscalls (ABI 1.6, 38) | consolidado |
| ELF64 + ring 3 + argc/argv | consolidado |
| Login/shell/init como serviços | **em progresso** (auth/fs/synd já saíram; shell/login/init ainda no kernel) |
| Persistência (ThaFS), rede, runtimes | planejado |

## 15. Roadmap técnico

1. **Init real (PID 1)** que inicia serviços (hoje `kormi_start`/`session_entry` cumprem o papel).
2. **Migrar** login/exec/FS para serviços ring 3 completos (classes boot/exec já desacopladas do RAMFS).
3. **ThaFS** persistente em disco (ESP/HDD) + `emporion` federado.
4. **musl + runtimes** (CPython/Node/C++) sobre capabilities; camada POSIX.
5. **Catallaxy** (rede) + drivers virtio.

Veja [`STATUS.md`](STATUS.md) (estado honesto por área) e [`SERVICES.md`](SERVICES.md) (plano de separação de serviços) para o detalhe.