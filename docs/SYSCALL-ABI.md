# Siaht OS — Syscall ABI 1.6

Interface oficial e estável entre userspace e kernel. **Não mude números retroativamente.**
ABI 1.6 = superfície de serviço do `synd` (contratos em ring 3) — **sem syscalls
novos** (SYS_COUNT continua 38); a mudança é o protocolo IPC do serviço (`IPC_TYPE_SYN_*`).

## Transição ring3 → ring0

Entrada via **`int 0x80`** (interrupt gate, `DPL=3`, `IF=0` durante toda a chamada):

```asm
    mov rax, <nr>       ; número do syscall
    mov rdi, <a1>       ; arg 1
    mov rsi, <a2>       ; arg 2
    mov rdx, <a3>       ; arg 3
    mov rcx, <a4>       ; arg 4
    mov r8,  <a5>       ; arg 5
    int 0x80
    ; retorno em rax
```

- **a5 em `r8`** (não `r10`): espelha convenção de syscall do kernel, evita clobber de `rcx`/`r11`.
- O gate salva **todos os 15 GPRs**, restaura ao retorno — só `rax` é sobrescrito com o resultado (ABI 1.0, MARCO 1).
- Vetor `0x80` é interrupt gate (`0xEE`, `idt.c`) → `IF=0` no ring0 durante todo o dispatch → **sem preempção dentro do gate**.
- Retorno ao ring3 via `iretq`. O frame original do `int 0x80` (SS, RSP, RFLAGS, CS, RIP) é preservado.

## Tabela de syscalls (ABI 1.6 — SYS_COUNT=38)

| Nº | Constante | Args | Retorno | Reqs |
|----|-----------|------|---------|------|
| 0 | `SYS_EXIT` | `a1 = código` | — (não retorna) | — |
| 1 | `SYS_WRITE` | `a1=fd, a2=ptr, a3=tam` | bytes escritos (máx 4096) | — |
| 2 | `SYS_READ` | `a1=buf, a2=max` | 0=sem input, 1=char lido em `*a1` | — |
| 3 | `SYS_EXEC` | `a1=path, a2=argc, a3=argv[]` | pid ou `0xFFFFFFFFFFFFFFFF` | CAP_EXEC |
| 4 | `SYS_IPC_SEND` | `a1=to, a2=type, a3=data, a4=size` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_IPC |
| 5 | `SYS_IPC_RECV` | `a1=buf, a2=max, a3=&from, a4=&type` | bytes lidos ou `0xFFFFFFFFFFFFFFFF` | CAP_IPC |
| 6 | `SYS_YIELD` | — | 0 | — |
| 7 | `SYS_FS_READ` | `a1=path, a2=buf, a3=max` | bytes lidos ou `0xFFFFFFFFFFFFFFFF` | CAP_FS_READ |
| 8 | `SYS_FS_WRITE` | `a1=path, a2=data, a3=size` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_FS_WRITE |
| 9 | `SYS_FS_LIST` | `a1=dir, a2=buf, a3=bufsize(0=4096)` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_FS_READ |
| 10 | `SYS_GETPID` | — | pid atual | — |
| 11 | `SYS_IPC_REPLY` | `a1=to, a2=type, a3=data, a4=size` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_IPC |
| 12 | `SYS_SYSINFO` | `a1=ptr_system_info_t` | `FS_OK` ou `0xFFFFFFFFFFFFFFFF` | — |
| 13 | `SYS_UPTIME` | — | uptime em ms (ticks×10) | — |
| 14 | `SYS_GETPPID` | — | pid do pai | — |
| 15 | `SYS_PROC_LIST` | `a1=proc_info_t[], a2=max(0=64)` | número de processos preenchidos | — |
| 16 | `SYS_FS_CREATE` | `a1=path, a2=type(0=arquivo,1=dir)` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_FS_WRITE |
| 17 | `SYS_AUTH_COUNT` | — | nº de usuários registrados | — |
| 18 | `SYS_AUTH_CREATE` | `a1=nome, a2=senha` | 1=ok, 0=falhou (só se count==0) | — |
| 19 | `SYS_AUTH_VERIFY` | `a1=nome, a2=senha` | 1=ok, 0=falhou | — |
| 20 | `SYS_CONTRACT_LIST` | `a1=buf, a2=bufsize` | 0 (preenche buf com texto) | — |
| 21 | `SYS_SESSION_LOGIN` | `a1=nome, a2=aceitar(1=S)` | 1=sessão criada (copia caps do usuário), 0=negada | — |
| 22 | `SYS_CONSOLE` | `a1=0:clear, 1:reboot, 2:poweroff` | 0 | — |
| 23 | `SYS_FS_REMOVE` | `a1=path` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_FS_WRITE |
| 24 | `SYS_WAITPID` | `a1=pid` | código de saída ou `0xFFFFFFFFFFFFFFFF` | — |
| 25 | `SYS_SVC_REGISTER` | `a1=nome` | 0=ok, `-1` erro | CAP_IPC |
| 26 | `SYS_SVC_QUERY` | `a1=nome, a2=&pid` | pid do serviço ou `0xFFFFFFFFFFFFFFFF` | — |
| 27 | `SYS_AUTH_LIST` | `a1=buf, a2=bufsize` | 0 (texto), 1=ok, `-1` erro | — |
| 28 | `SYS_AUTH_DELETE` | `a1=nome` | 1=removido, 0=protegido, `-1` sem permissão | CAP_USER_ADMIN |
| 29 | `SYS_AUTH_CHANGEPW` | `a1=nome, a2=senha_atual, a3=nova` | 1=ok, 0=falhou, `-1` sem permissão | — |
| 30 | `SYS_AUTH_CREATE_USER` | `a1=nome, a2=senha` | 1=criado, 0=falhou, `-1` sem permissão | CAP_USER_ADMIN |
| 31 | `SYS_FB_DRAW` | `a1=fb_cmd_t*` | 0 ou `0xFFFFFFFFFFFFFFFF` | CAP_FB_DRAW |
| 32 | `SYS_IO_PORT` | `a1=op, a2=port, a3=val` | leitura (in8/16/32) ou 0 | CAP_DEV_IO |
| 33 | `SYS_MMAP` | `a1=paginas` | base VA do heap ou `0xFFFFFFFFFFFFFFFF` | — |
| 34 | `SYS_DEV_MAP` | `a1=phys, a2=size` | VA do BAR mapeado ou `0xFFFFFFFFFFFFFFFF` | CAP_DEV_IO |
| 35 | `SYS_V2P` | `a1=va` | PA da página ou `0xFFFFFFFFFFFFFFFF` | — |
| 36 | `SYS_AUTH_TICKET_ISSUE` | `a1=nome, a2=caps, a3=&token` | 0=ok, `0xFFFFFFFFFFFFFFFF` | CAP_USER_ADMIN |
| 37 | `SYS_AUTH_HASH` | `a1=pw, a2=salt-hex(≤32), a3=&out-hex(64)` | 0=ok, `0xFFFFFFFFFFFFFFFF` | CAP_USER_ADMIN |

`0xFFFFFFFFFFFFFFFF` (-1 em complemento de dois) é o indicador de erro universal.

### ABI 1.5 — sessão por ticket (authd, serviço ring 3)

- `SYS_AUTH_TICKET_ISSUE` (36): grava no kernel `{nome, caps, token}` consumível
  uma única vez (`CAP_USER_ADMIN`; só o `authd` via grant do `kormi_start`).
  `token` é 64-bit, mistura ticks + seq + contexto do chamador.
- `SYS_SESSION_LOGIN` (21) ganhou branch `a3`: com `a3!=0`, trata `a3` como
  token de ticket — consome o ticket, sintetiza a sessão com os caps vindos do
  serviço e **não toca no store do kernel** (fallback clássico com `a3=0` mantém
  o caminho `auth_find`). A2 segue exigindo o consentimento (`1=S`).
- `SYS_AUTH_HASH` (37): PBKDF2-HMAC-SHA256 com `disable_interrupts()` (primitiva
  computacional). Em ring 3, computo longo preemptado por IRQ é
  **não-determinístico** (hash corrompia intermitentemente) — o `authd` executa
  o hash por aqui e mantém store+política+ticket no serviço.
- Dois tickets de sessão por conta: o novo invalida o anterior não usado.

### SYS_SESSION_LOGIN (21) — ABI 1.5

## Detalhes das syscalls

### SYS_EXEC (3) — executar ELF externo

`a1=path` (caminho absoluto, validado com `user_str`), `a2=argc` (máx 16), `a3=ponteiro para array user de `uint64_t` (ponteiros para strings).

- Cada string de argv é validada com `user_str` (máx 128 bytes).
- Retorna `pid` (uint32) em `rax` se sucesso; `0xFFFFFFFFFFFFFFFF` se erro.
- **Herança de capabilities:** o novo processo recebe `rights = base | (parent->rights)` quando o pai está logado (login criou sessão que copiou `u->caps`). Init/processos ring-0 (CAP_ALL) propagam tudo aos filhos — inclusive aos apps `/kormi` (drivers). Em ring 3 puro sem sessão, herdam `CAP_SYSCALL|CAP_FS_READ|CAP_FS_WRITE|CAP_EXEC|CAP_IPC`.

### SYS_WAITPID (24) — aguardar término de filho

`a1=pid` do filho esperado. Bloqueia (`sched_block`) até o filho terminar (ou já terminado). Retorna o código de saída (uint32). `0xFFFFFFFFFFFFFFFF` = erro (pid 0, filho não existe, não é filho do chamador).

### SYS_SVC_REGISTER (25) / SYS_SVC_QUERY (26) — registry de serviços IPC

`SYS_SVC_REGISTER`: `a1=nome` (string até 31 chars) registra `current_proc->pid` como provedor (SVC_MAX=16); limpa registros órfãos; requer CAP_IPC.
`SYS_SVC_QUERY`: `a1=nome`, `a2=&pid` (opcional). Retorna pid do serviço ou erro.

### Protocolos de serviço via IPC (payload NUL- ou `\n`-separado)

Valores de `type` no envio para cada serviço registrado (definidos em `spud.h`):

| type | nome | serviço | request → response |
|---|---|---|---|
| 1 | `IPC_TYPE_PING` | qualquer | (vazio) → `"pong"` |
| 2 | `IPC_TYPE_READ` | `fsd` | `path` → conteúdo do arquivo |
| 3 | `IPC_TYPE_LIST` | `fsd` | `dir` → listagem |
| 11 | `IPC_TYPE_FS_CREATE` | `fsd` | `path` → `'1'`/`'0'` |
| 12 | `IPC_TYPE_FS_WRITE` | `fsd` | `path\n<dados>` (sem NUL interno) → `'1'`/`'0'` |
| 13 | `IPC_TYPE_FS_REMOVE` | `fsd` | `path` → `'1'`/`'0'` |
| 14 | `IPC_TYPE_FS_AUDIT` | `fsd` | (vazio) → trilho de auditoria do serviço (texto) |
| 20-26 | `IPC_TYPE_AUTH_*` | `authd` | ver §ABI 1.5 acima |
| 30 | `IPC_TYPE_SYN_LIST` | `synd` | `filtro` (opcional) → texto dos contratos vigentes (`  nome r w v g a c\n`) |
| 31 | `IPC_TYPE_SYN_READ` | `synd` | `caminho` → contrato detalhado ou `"0"` |
| 32 | `IPC_TYPE_SYN_CONSENT` | `synd` | `caminho` → `'1'`/`'0'` (consentimento voluntário; kernel aplica a regra) |
| 33 | `IPC_TYPE_SYN_REVOKE` | `synd` | `caminho` → `'1'`/`'0'` (revoga consentimento) |
| 34 | `IPC_TYPE_SYN_AUDIT` | `synd` | (vazio) → trilho de auditoria do serviço (texto) |

> **SYS_CONTRACT_LIST (20) — GOTCHA de retorno:** o syscall retorna **0** em sucesso
> (o conteúdo é preenchido no buffer; o retorno NÃO é tamanho — mesmo comportamento
> do `SYS_FS_LIST`). Consumidores devem varrer o buffer até o NUL. Formato por linha:
> `"  - <nome> em <caminho> (r=%d w=%d x=%d)\n"` — **o nome pode conter espaços**.

Cliente via `spud_fsd_*` (`spud.h`): não-bloqueante (retry+SYS_YIELD) do
`SYS_IPC_RECV`; **fallback honesto** nos syscalls `SYS_FS_*` quando `fsd` não
estiver registrado. `FS_AUDIT` não tem fallback (é valor só do serviço).
Cliente synd via `spud_synd_*`: **svc-first**; só `spud_synd_list` tem fallback
syscall (`SYS_CONTRACT_LIST`); os demais devolvem -1 sem serviço (`SYN_AUDIT` é
valor só do serviço).

### SYS_IPC_SEND (4) / SYS_IPC_RECV (5) / SYS_IPC_REPLY (11)

Mailbox-based. IPC, máx `IPC_MSG_SIZE=64B`. `sys_ipc_free_space()` aguarda slot livre (se caixa cheia). `ipc_receive` bloqueia até mensagem disponível.

### SYS_CONSOLE (22)

`a1=0` → limpa tela. `a1=1` → reboot. `a1=2` → poweroff. Retorna 0.

### SYS_FS_CREATE (16) / SYS_FS_REMOVE (23)

`SYS_FS_CREATE`: `a1=path` absoluto, `a2=0` arquivo ou `1` diretório. Delega `vfs_create("/", path, type)` (valida caps e bloqueia pseudo-FS/`/oikos`). Requer CAP_FS_WRITE.
`SYS_FS_REMOVE`: `a1=path`. Delega `vfs_remove("/", path)`. Requer CAP_FS_WRITE.

### SYS_AUTH_* (27-30) — gestão de usuários em ring 3

- `SYS_AUTH_LIST` (27): `a1=buf, a2=bufsize`. Preenche com texto listando usuários (nome, admin, caps, ativo). Requer CAP_USER_ADMIN.
- `SYS_AUTH_DELETE` (28): `a1=nome`. Soft-delete (`active=false`). Recusa o admin e o próprio usuário atual. Requer CAP_USER_ADMIN.
- `SYS_AUTH_CHANGEPW` (29): `a1=nome, a2=senha_atual, a3=nova`. Para si mesmo, valida senha atual; admin pode trocar de terceiros (senha atual ignorada). 1=ok, 0=falhou, -1=sem permissão.
- `SYS_AUTH_CREATE_USER` (30): `a1=nome, a2=senha`. Cria usuário voluntário com caps `CAP_FB_DRAW|CAP_AISTHESIS|CAP_FS_READ|CAP_IDIOS_WRITE`. Requer CAP_USER_ADMIN.
- A sessão (`SYS_SESSION_LOGIN`) copia `u->caps` para `current_proc->rights` — admin logado vira CAP_ALL e propaga aos filhos via `SYS_EXEC`.

### SYS_FB_DRAW (31) — framebuffer (base para GUI/Doom)

`a1=fb_cmd_t*`:

```c
typedef struct {
    int32_t op, x, y, w, h;
    uint32_t color; uint32_t color2;
    const char *text;     /* (FB_CMD_TEXT) */
} fb_cmd_t;
```

Ops: `FB_CMD_CLEAR=0` (color), `FB_CMD_RECT=1`, `FB_CMD_PIXEL=2` (x,y,color), `FB_CMD_TEXT=3` (x,y,text,color,color2 — buffer limitado a `FB_CMD_TEXT_MAX=128`), `FB_CMD_CHAR=4` (x,y,char em `w`,color,color2). Requer CAP_FB_DRAW. `fb_get_dim()` retorna 0 com framebuffer ativo (preenche `w/h`), -1 se ausente.

### SYS_MMAP (33) — malloc/memória de processo (ABI 1.3)

`a1=paginas` (>=1, máx 8192 = 32MB por chamada). Mapeia N páginas 4KB **contíguas** no user space do processo chamador, flags `P|US|W|NX` (grava, não executa — W^X), registrando o bloco no umap p/ validação de ponteiro nas syscalls. Retorna a **base VA** (page-aligned) ou `0xFFFFFFFFFFFFFFFF` em falha (sem memória / sem slot de umap).

- A região do heap por processo é **uma**: o 1º `SYS_MMAP` cria o slot (`process_t.umap_heap`) logo após o topo do stack (+0x10000 de guarda); os demais chamados estendem o mesmo bloco (`umap_size[slot]` cresce). Teardown: `paging_free_address_space` libera as folhas automaticamente (fazem parte do PML4 do processo).
- Rollback total em falha: desmapeia as páginas já mapeadas (`paging_unmap_user`) e devolve os frames ao PMM — nenhuma PTE pendurada.
- Sem cap: é o mecanismo fundamental de heap de usuário (limite prático = memória do PMM). Qualquer ring-3 com CAP_SYSCALL pode mapear no próprio espaço.
- **spud.h** fornece `spud_malloc`/`spud_free` (free-list first-fit, chunks de 64KB por `SYS_MMAP`; coalescing ainda não implementado — TODO).

### SYS_IO_PORT (32) — acesso a portas (drivers)

`a1=op`: `0=inb, 1=inw, 2=inl, 3=outb, 4=outw, 5=outl`. `a2=port (16-bit)`, `a3=valor (saída)`. Entradas retornam o valor lido; saídas retornam 0. Requer **CAP_DEV_IO**.

### SYS_DEV_MAP (34) — mapear BAR MMIO de dispositivo (ABI 1.4)

`a1=phys` (base do BAR, alinhada a 4KB), `a2=size` (1B a 1MB, arredondado p/ cima). Mapeia o range físico (típico: BAR de PCI) no espaço do processo chamador, na **região device-IO** (`USER_DEVIO_BASE = 0x7ff000000000`, topo do user space; cresce p/ cima, `process_t.umap_devio_base/_used`). Retorna a **VA base** do mapeamento, ou `0xFFFFFFFFFFFFFFFF` (sem cap, phys desalinhado, size inválido, falta de PT pages). Flags `P|US|W|NX|PTE_AVAIL_MMIO` — RW, não executável. Requer **CAP_DEV_IO** (drivers `/kormi`).

- **`PTE_AVAIL_MMIO` (bit 11, avail)**: marca a PTE como frame de hardware, **não pertencente ao PMM**. `paging_free_address_space`/`paging_free_subtree` **não** fazem `pmm_free` dessas folhas no teardown — sem isso, o GC corromperia o bitmap do PMM liberando o BAR do device.
- Mapeamento WB (cacheável). Drivers deveriam usar UC/WC (PAT) no futuro; em QEMU funciona.
- A região devio **não** é registrada em `umap_*`: o driver dereferencia diretamente o endereço (acesso a MMIO), sem validação de ponteiro de syscall.

### SYS_V2P (35) — endereço físico de uma VA de usuário (para DMA)

`a1=va`. Retorna o endereço **físico** (PTE frame + offset de página) da VA no espaço do processo chamador, ou `0xFFFFFFFFFFFFFFFF` se não mapeada. Requisito para DMA: o driver XHCI aloca anéis/contextos via `spud_malloc` (heap userspace) e programa os físicos nos registradores/TRBs — `spud_v2p` resolve a tradução. Sem cap adicional: o processo só pode consultar o próprio mapa (`paging_phys_of_user(p->ctx.cr3, ...)`), que só aceita user space (< 0x800000000000) e rejeita páginas grandes (PS).

## Validação de capabilities

Dispatcher exige **`CAP_SYSCALL`** para todas as syscalls. Acima disso:
- `SYS_EXEC` → **CAP_EXEC**
- `SYS_IPC_SEND/RECV/REPLY` → **CAP_IPC**
- `SYS_FS_READ/FS_LIST` → **CAP_FS_READ**
- `SYS_FS_WRITE/FS_CREATE/FS_REMOVE` → **CAP_FS_WRITE**
- `SYS_SVC_REGISTER` → **CAP_IPC**
- `SYS_AUTH_DELETE/CREATE_USER` → **CAP_USER_ADMIN**
- `SYS_FB_DRAW` → **CAP_FB_DRAW**
- `SYS_FB_DRAW` → **CAP_FB_DRAW**
- `SYS_IO_PORT` / `SYS_DEV_MAP` → **CAP_DEV_IO**

Processos ring-0 (`umap_count==0`) confiam no kernel (sem validação de ponteiro).

## Validação de ponteiros

Todo ponteiro ring-3 é validado antes de dereferenciar:
- `user_ptr_ok(ptr, len)` — confere que `[ptr, ptr+len)` pertence a uma região registrada (umap).
- `user_copy` / `user_str` — usam o gate acima; retornam erro se ponteiro inválido.
- Impede processos ring-3 de ler/escrever memória do kernel.

## W^X no paging (MARCO 5, bug-fix 8)

O loader (`proc_create_user_segments`) mapeia segmentos ELF com W^X baseado em `p_flags`:
- `PF_X` (bit 0 = 0x1) → RX (0x5): `.text`, `.rodata`
- `PF_W` (bit 1 = 0x2) → RW+NX (0x7|NX): `.data`, `.bss`
- Leitura pura (sem X nem W) → RO+NX (0x1|NX)

> Cuidado: `p_flags` ELF é bit0=X, bit1=W, bit2=R (não confundir com 0x1/0x2/0x4 de page tables x86).

## Adicionar uma nova syscall

1. Defina `#define SYS_* <n>` em `src/kernel/include/thais.h` **e** no espelho `src/userspace/spud.h`.
2. Implemente `case SYS_*` em `src/kernel/syscall.c`.
3. Atualize `SYS_COUNT` e esta tabela.
4. Se precisar de nova capability, adicione em `thais.h` e no dispatcher.

## Boas práticas

- Valide ponteiros antes de dereferenciar (nunca confie em ring-3).
- Retorne `0xFFFFFFFFFFFFFFFF` em erro, nunca `0` (0 pode ser válido).
- Valide no serviço de baixo nível (VFS/IPC), não só no dispatcher.
- Nunca chame `proc_exit` sem garantir que o pai foi notificado (MARCO 3: `wait_pid`/`wait_code`).