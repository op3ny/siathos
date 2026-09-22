# Siaht OS — Made with love by Thaís (op3n/op3ny)

> Micro sistema operacional original, ancap na alma, grego na forma.
> Made by [Thaís (op3n/op3ny)](https://github.com/op3ny)
> Release: **0.0.x — "Independent System" (em consolidação)** · 2026-09-22 · ABI 1.6 (SYS_COUNT=38)

## Filosofia Implícita (Escola Austríaca)
Não há panfleto no boot. Há arquitetura:
- **Praxeologia** → tudo é `praxis` (ação humana). Binários ficam em `/praxis`, não `/bin`.
- **Ordem espontânea (Hayek)** → sem daemon central planejador. Cada serviço é um **contrato voluntário** em `/synallagma`.
- **Propriedade privada** → capability-based, não `root` onipotente. Você só acessa o que contratou.
- **Catalaxia** → rede é `/catallaxy`, troca voluntária. Pacotes são mercado federado `/emporion` + `/agora`.
- **Individualismo metodológico** → `/idios` (o indivíduo), a menor minoria.

## Rota Escolhida: C — Zero com Base
Kernel 100% inédito em C/ASM, microkernel. Boot via **ThaisBoot** próprio (UEFI x86_64). 100% compatível QEMU x86_64.

## Estrutura de Diretórios (original, grego + função + easter eggs)
```
/arkhe          → raiz / princípio (αρχή)
/praxis         → executáveis (πρᾶξις) [ex-/bin]
/techne         → ferramentas de sistema (τέχνη) [ex-/sbin]
/nomos          → leis/configs (νόμος) [ex-/etc]
/emporion       → mercado de pacotes (ἐμπόριον) [ex-/usr + /var/lib]
/agora          → cache federado de repos (ἀγορά)
/idios          → lar do indivíduo (ἴδιος) [ex-/home]
/idios/thais    → easter egg home da criadora
/oikos          → sistema interno privado (οἶκος)
/kinesis        → processos em movimento (κίνησις) [ex-/proc + /run]
/paradosis      → temporários/entregas (παράδοσις) [ex-/tmp]
/aisthesis      → dispositivos/sentidos (αἴσθησις) [ex-/dev]
/catallaxy      → rede/trocas (catallaxy hayekiana) [ex-/net]
/synallagma     → contratos voluntários (συνάλλαγμα) [ex-/etc/init.d + systemd]
/op3n           → easter egg discord
/op3ny          → easter egg github
/menger, /hayek, /rothbard, /mises → easter eggs austríacos (symlinks p/ docs)
```

## Comandos (shell `praxia`)
Nada de `ls/cd/mkdir` genérico. Shell real em ring 3 (`praxia`). Comandos em grego transliterado + easter eggs austríacos (aliases também no shell de console do kernel):
| Original | Trad. | Função |
|---|---|---|
| `horasis` | ὅρασις | listar (ls) |
| `metabasis` | μετάβασις | ir/mudar dir (cd) |
| `ktisis` | κτίσις | criar arquivo/dir |
| `hairesis` | αἵρεσις | escolher/selecionar |
| `praxia` | πρᾶξις | executar ação |
| `synallagma` | συνάλλαγμα | gerenciar contratos/processos |
| `emporion` | ἐμπόριον | mercado de pacotes |
| `aisthesis` | αἴσθησις | ver dispositivos |
| `nomos` | νόμος | editar leis/configs |
| `agora` | ἀγορά | sincronizar repos |
| `thais` | — | easter egg |
| `op3n` / `op3ny` | — | easter eggs |

Compatibilidade: aliases POSIX ainda funcionam mas são desencorajados.

## Boot Splash
Caixa centralizada framebuffer GOP (proporcional à resolução do modo de vídeo):
```
┌─────────────────────────────────────┐
│  O Siaht OS está iniciando... Aguarde [loading] │
│  Made with love by Thaís (op3n/op3ny)           │
│  "frase aleatória"                              │
└─────────────────────────────────────┘
```
- `<50%` → spinner `|/-\`
- `>50%` → barra `[====>   ] 73%`
Frases rotativas:
1. "Eles correm sem distância..." — Baleia (Atlas)
2. "A liberdade exige oposição a toda forma de coerção..." — Thaís
3. "Uma ode à Menger, Hayek, Rothbard e Mises."
4. "A menor minoria na Terra é o indivíduo..." — Ayn Rand

Depois → login framebuffer (bootstrap via `spoudazo`) → shell `praxia` (ring 3).

## Stack Técnica
- **Boot:** **ThaisBoot** (UEFI x86_64, PE32+, 100% original em `src/boot/thaisboot/`) — abre kernel.elf da ESP, framebuffer GOP, memory map, ExitBootServices, salto higher-half
- **Kernel:** kernel `thais-kernel` C/ASM, capability-based, sem root, scheduler cooperativo → preemptivo (resume full-frame/iretq para ring 3)
- **Serviços ring 3** (ABI 1.5/1.6): `authd` (auth), `fsd` (fs), `synd` (Synallagma), `devd` (dev), `odigos` (teclado) — consumidores via IPC com fallback no kernel
- **FS:** RAMFS em memória (VFS) → **ThaFS** próprio (persistente) em desenvolvimento
- **Init:** contratos voluntários em `/synallagma/*.pacto` (TOML-like), não systemd
- **Runtimes híbridos (planejado, pós 0.1):** `musl` + camada POSIX compat (capabilities por baixo) → CPython, Node, C++ nativos; binários Linux via tradução de syscalls (fase 2)
- **Build:** gcc + ld + nasm + xorriso + mtools (Linux nativo — elementary OS/Debian) → ISO híbrida UEFI
- **Teste:** QEMU `qemu-system-x86_64 -bios OVMF.fd -cdrom build/siaht.iso`; regressão `scripts/_runtest.sh N` (13 markers)

## Build Rápido
```bash
# Debian/Ubuntu (Linux nativo)
sudo apt install build-essential nasm xorriso qemu-system-x86 ovmf mtools
make iso   # gera build/siaht.iso
make run   # QEMU UEFI (janela)
make run-headless   # QEMU UEFI headless (serial)
```
> **Nota:** Siaht OS é **UEFI x86_64** hoje. Não há caminho BIOS/CSM (decisão
> explícita; rever apenas com a fase de compatibilidade BIOS).

## Dados Reais da Release (medidos em 2026-09-22)

| Artefato | Tamanho |
|---|---|
| ISO híbrida UEFI `build/siaht.iso` | 34 MB |
| Kernel `build/kernel.elf` | 576 KB |
| Bootloader `build/BOOTX64.EFI` (ThaisBoot, PE32+) | 52 KB |

| Métrica | Valor |
|---|---|
| Kernel C/ASM (linhas) | ~6.774 |
| Bootloader ThaisBoot (linhas) | ~254 |
| Apps userspace (``/bin`` + ``/kormi``) | 18 |
| Serviços ring 3 (ABI 1.5/1.6) | 5 (`authd`, `fsd`, `devd`, `odigos`, `synd`) |
| Syscalls (`int 0x80`) | 38 (ABI 1.6) |
| Regressão serial `scripts/_runtest.sh` | 13 markers, verde no QEMU |

> Contratos do Synallagma: `idios.pacto` protege `/idios`; store userspace de
> contratos vive no serviço `synd` (ring 3, ABI 1.6), espelho do modelo authd/fsd.

## Roadmap

> Status atual: **Siaht OS 0.0.x — microkernel em consolidação** (ABI 1.6).
> Boot de produção validado no QEMU: `KTHAIS_BOOT_OK` → serviços ring 3
> (`authd`/`fsd`/`devd`/`odigos`/`synd`) → `spoudazo` → prompt `Novo admin`.
> Esta seção separa o que **já está implementado e funcionando** do que é
> **planejado** (arquitetura-alvo). Nada aqui é promessa de marketing: cada
> item "implementado" roda no QEMU hoje.
>
> Para a **versão honesta** do que é maturidade real vs. objetivo (isolamento
> de memória, auth, Synallagma como serviço, ring-3), veja
> [`docs/STATUS.md`](docs/STATUS.md).

### ✅ Implementado (roda agora)

**Núcleo / boot**
- [x] Bootloader UEFI original `ThaisBoot` (PE32+, `/src/boot/thaisboot`)
- [x] Framebuffer GOP + splash de boot centralizado com barra/spinner
- [x] Serial (COM1) + console framebuffer

**Memória**
- [x] PMM bitmap (alocação/liberação de páginas físicas)
- [x] Heap `kmalloc`/`kfree` com split/coalesce e detecção de corrupção
- [x] Paging de nível 4 com mapeamento identidade + higher-half
- [x] Espaço de endereços por processo (PML4 clone raso) + troca de CR3 no switch
- [x] `paging_map_user` real (Fase P): mapeia página de usuário 4KB por processo,
      com invalidação TLB — **W^X** (código `0x5` P|US|X, stack `0x7|NX`) e
      **EFER.NXE** habilitado (auditoria de segurança)
- [x] Teardown/GC de processo morto: `proc_gc_run` libera kernel stack, PML4,
      páginas de usuário e regiões ELF reservadas (nunca o espaço ativo)

**Processos / Scheduler**
- [x] PCB real (`process_t`): PID, nome, rights, estado, stack, contexto, parent
- [x] Estados: CREATED / READY / RUNNING / BLOCKED / TERMINATED
- [x] Context switch real em assembly (salva/restaura callee-saved, RSP, RIP, CR3)
- [x] Scheduler round-robin preemptivo (cooperativo + tick do PIT)
- [x] `sched_yield`, `sched_block`, `sched_wakeup`, `sched_tick`
- [x] Demonstrativos A/B/C + produtor/consumidor IPC (`make TEST=1`)

**Interrupções**
- [x] IDT com 48 vectors (32 exceções + 16 IRQ) + gate de syscall `int 0x80` (DPL=3)
- [x] Handlers de exceção com PANIC + contexto do processo
- [x] PIC remap, PIT 100Hz, teclado PS/2 — driver ring 3 `odigos` (`/kormi`,
      scancodes → IPC) + console PS/2 do kernel
- [x] Exceções de ring 3 → PANIC com dump completo do contexto/processo

**Filesystem / VFS**
- [x] RAMFS com ownership, permissions (`world_read/write`), caps por nó
- [x] VFS genérico sobre o RAMFS (normalização de `.`/`..`, validação, `vfs_*`)
- [x] Árvore de diretórios grega (`/arkhe`, `/praxis`, `/idios`, ...)

**Segurança / Serviços lógicos**
- [x] Capabilities reais (`process_t.rights`, `cap_check/grant/revoke`)
- [x] Aplicação de capabilities no VFS (`vfs_check_cap`) baseada em contrato
- [x] Contratos voluntários `Synallagma` (`/synallagma/*.pacto`) + consentimento no login
- [x] Auth com hash + salt (**PBKDF2-HMAC-SHA256**, `AUTH_HASH_ITER=100000`,
      salt 16B por conta, ~4.4ms/hash em QEMU) + usuário soberano/voluntário,
      troca de senha — **serviço ring 3 `authd`** (ABI 1.5, kernel = fallback)
- [x] Login framebuffer com bootstrap do admin (via `authd`/`spoudazo`)

**IPC**
- [x] Mailboxes por processo, `ipc_send/receive/reply`, bloqueio, CAP_IPC
- [x] Syscalls `int 0x80` (ABI 1.6, **SYS_COUNT=38**, gates de capability — tabela
      completa em `docs/SYSCALL-ABI.md`)

**Execução**
- [x] ELF64 loader (valida header, carrega PT_LOAD, cria processo, entry point)
- [x] Passagem de **argc/argv na 1ª execução** (`cpu_context_t.arg1/arg2` +
      flag `ran`, carregados em rdi/rsi por `ctxswitch.asm`) — demo `/bin/hello`
      imprime `args: /bin/hello mundo bar` e sai com código 0 (`make TEST=1`)
- [x] `proc_create_user` (Fase K, base): cria processo ring 3 — esp. de endereços
      próprio, stack kernel + stack usuário, frame iretq, `enter_user`/`user_trampoline`,
      `tss.rsp0` por processo no scheduler (`sched_prep`)
  - [x] Preempção e retorno via syscall/IRQ em ring 3 no modelo **full-frame/iretq**
    (park `style=1`/`hw_rsp` no stub do ISR) — **bugfix 2/3 fechado** (causa raiz:
    clobber de `r10` no `.diag_iret` do stub; 16/16 PASS no boot TEST)
- [x] Apps userspace `/bin` + `/praxis`: `ls`, `cat`, `echo`, `ps`, `mem`, `heap`,
      `wtest`, `phrourio`, `svctest` (`make TEST=1` — testes do gate)
- [x] Serviços `/kormi`: `authd`, `fsd`, `synd`, `devd`, `odigos`
- [x] Shell ring 3 `praxia` com comandos gregos + aliases POSIX (bootstrap/login
      via `spoudazo`); `thais-sh`/`thais-init`/`thais-login` = esqueleto

### ✅ Em consolidação (Fase A/B/C/J/O/P)

Auditoria e limpeza já realizadas e verificadas no QEMU (boot limpo):

- [x] Revisão de buffers: `SYS_WRITE` (até 4096B), `fs_list`, `elf.c` (valida
      vaddr/limites), `exec_file`, `FS_NAME_MAX` único, `pmm_free` sem underflow
- [x] Remoção de código morto / duplicações / `outb`/`inb` unificados
- [x] VFS/RAMFS consolidado (normalização, `.`/`..`, ownership, caps, erros)
- [x] Capabilities como gate real em `vfs_open` e nos syscalls (`syscall_cap`)
- [x] ELF loader com heap (`kmalloc`/`kfree`) em vez de buffer estático
- [x] `docs/SYSCALL-ABI.md` — ABI completa + gates de capability
- [x] Auditoria de segurança: **validação de ponteiros de usuário em syscalls**
  (`user_ptr_ok`/`user_copy`/`user_str` + regiões `umap_*` do processo;
  ring-0 não passa pelo gate)
- [x] Auditoria de segurança: **W^X + EFER.NXE** em páginas de usuário +
  **teardown/GC** de processos mortos (`proc_gc_run`)
- [x] Auditoria de segurança: auth hash **FNV-1a → PBKDF2-HMAC-SHA256**
  (crypto kernel validado contra vetores; custo real ~4.4ms/hash em QEMU)
- [x] Login de bootstrap: **#PF no PBKDF2 sob preempção corrigido** (IRQs off
  no hash) — **causa raiz do ISR encontrada e fechada** (bugfix 2/3, ver abaixo)
- [x] Auditoria da segurança da preempção: **bugfix 2/3 fechado** — o `isr_common_stub`
  corrompia `r10` no retorno de IRQ (bloco `.diag_iret` escrevia no registro depois
  dos pops e antes do `iretq`); todo returno de IRQ devolvia o processo com `r10`
  = bytes do frame. Fix: `iretq` puro; validado 16/16 PASS. (histórico em `skill.md` §7)

### 🧭 Planejado (arquitetura-alvo — próximo grande objetivo)

O objetivo é sair do modelo **monolítico** atual:

```
KERNEL: shell + auth + fs + login + exec (tudo dentro)
```

para o modelo **serviços independentes**:

```
BOOT → KERNEL (mem, sched, proc, IPC, caps, syscalls)
         ↓
       init (PID 1)
         ↓
  ┌───────────┬──────────────┬─────────────┐
 Shell     Auth Service   FS Service     Apps
```

Fases planejadas (na ordem de implementação que recomendamos):

| Fase | Objetivo |
|------|----------|
| **B** | ✅ Consolidar VFS/RAMFS (limites, erros, permissões) e a interface `vfs_*` |
| **C** | ✅ Capabilities como gate real em VFS/syscalls (`syscall_cap`) |
| **P** | ✅ `paging_map_user` real com **W^X** (0x5/0x7|NX) + **NXE** + teardown/GC de processo morto |
| **K** | ✅ Ring 3 completo (`proc_create_user`, `enter_user`, `user_trampoline`, tss.rsp0 por proc) + **preempção full-frame/iretq validada** (bugfix 2/3 fechado) |
| **N** | `init` real (PID 1) que inicia serviços em vez de chamar o ambiente direto (hoje: `kormi_start`/`session_entry` cumprem o papel) |
| **O** | ✅ Passagem de argc/argv ao executar um ELF (`exec_elf_args`, argumentos no stack do processo, `ran`/1ª execução) — útil para `praxia <bin> <args...>` |
| **L** | ✅ Shell em processo independente ring 3 (`praxia`, usa syscalls+IPC, não funções internas) |
| **M** | ✅ Auth (`authd`), FS (`fsd`) e Synallagma (`synd`) como serviços ring 3 separados (ABI 1.5/1.6, SYS_COUNT=38 inalterado; kernel = fallback/enforcement) — login/exec ainda residem no kernel |

> Auditar como **hoje** esses serviços estão acoplados (monolítico real) e o
> plano honesto de separação (o que falta, dependências, critérios de aceite):
> [`docs/SERVICES.md`](docs/SERVICES.md).
> **H/I + Q — IPC consolidado + Synallagma integrado + modelo de serviços: ✅ fechados**
> com o `synd` (serviço de contratos, ABI 1.6) — IPC types 30-34, SYS_COUNT=38.

**Marco da versão:**
> **Siaht OS 0.1 — "Independent System"** só está consolidado quando: boot → kernel
> → scheduler → init → FS service → Auth service → Shell process → login → usuário
> executa comandos, cria/edita arquivos (persistidos em memória), executa um ELF,
> um novo processo é criado, o scheduler alterna, o programa termina e retorna à shell.

### Rota técnica futura (pós 0.1)
1. ThaFS + emporion federado (persistência real)
2. musl + Python/Node/C++ nativos
3. Catallaxy (rede) + drivers virtio

Licença: MIT + cláusula de amor.
