# Siaht OS — Status Técnico Honesto

> Este documento registra o que o sistema **realmente** é hoje (e o que **não** é),
> sem promoção. Atualize sempre que a implementação mudar.
> Data base: 2026-09-21 (ABI 1.5 + `authd`/`fsd` como serviços ring 3;
> **ABI 1.6** — `synd` como serviço ring 3, dono do store userspace de
> contratos do synallagma (espelho exato do modelo authd/fsd); SYS_COUNT
> permanece 38 — a 1.6 é superfície de serviço, **sem syscalls novos**.
> Histórico 2026-09-11: store de auth saiu do kernel para `/kormi/authd`;
> boot/exec desacoplado do ramfs e o serviço `fsd` ganhou superfície IPC
> completa (READ/LIST/CREATE/WRITE/REMOVE/AUDIT).

## 1. Modelo de execução atual

Para quem leu o README e quer saber a verdade nua sobre a fundação:

- **Não há userspace real ainda.** `init`, `login` e `shell` são código
  residente no kernel (kernelspace), executado como PID 1 e processos kernel
  que chamam `vfs_*`, `auth_*`, `fs_*`, `exec_*`, `kinesis_*` e `synallagma_*`
  **diretamente (chamada de função), não via syscall/IPC**.
- **`auth` é o 1º serviço migrado para ring 3 (2026-09-11, ABI 1.5):** o app
  `/kormi/authd` (PID 8, anel 3, CAP_USER_ADMIN via grant) é dono da store de
  credenciais e da política (1º usuário = soberano; validação de senha custosa;
  criação/remoção/troca de senha). Spawned por `kormi_start()` em produção e
  TEST. Login real ("Novo admin", leitura de conta) usa IPC a ele; os syscalls
  `SYS_AUTH_*` do kernel ficam como **fallback** quando `svc_query("auth")`
  falha (um fluxo por boot). `praxia` (users/userdel/passwd/useradd), o
  bootstrap do `spoudazo` e o `svctest` já consomem o serviço via IPC.
- **Preparação para `fs` como serviço (2026-09-11, mesmo dia):** (a) nasceu a
  tabela `s_mods[]` (file-scope em `fs.c`) com accessors `fs_mod_*` e um
  `exec_embedded_args` no loader — boot (`kormi_start`) e sessão (`spoudazo`)
  e execuções de `/bin`, `/kormi`, `/praxis` passaram a usar os bytes embutidos
  **sem passar pelo ramfs** (`exec_user_path` é embedded-first para dirs do
  sistema, com fallback em FS p/ arquivos do usuário) — o boot deixa de ser
  refém do `fs_read`; (b) o serviço `fsd` completou a superfície IPC ao estilo
  authd: `IPC_TYPE_FS_CREATE/WRITE/REMOVE/AUDIT` (11-14) além de READ/LIST e
  agora é o **dono do store userspace**: `/paradosis` e `/nomos` vivem em copia
  no HEAP do fsd (semeado do kernel no boot, marker `[fsd] store ring3`), com
  **trilho de auditoria próprio** e helpers `spud_fsd_*` (svc-first + fallback
  syscall; AUDIT sem fallback). O kernel (SYS_FS_*) é o **primitivo/formback**
  nos demais paths — mesmo contrato do authd (store no serviço, kernel como
  fallback e ponto de enforcement); NADA de bloqueio de kernel aguardando
  serviço (regra IPCA). Divergência "split-brain" boot-a-boot entre o store do
  kernel e o do fsd é o modelo assumido (um fluxo por boot), idêntico ao do auth.
- Existe a **base de ring 3** (`proc_create_user`, `enter_user`, `user_trampoline`,
  `tss.rsp0` por processo) e um demo, mas o ciclo de vida completo de processos
  ring 3 (preempção + retorno por syscall/IRQ via full-frame/iretq) **não é
  consolidado**. O escalonador é `ret`-baseado (cooperativo).
- Um ELF (ex.: `/bin/hello`) pode ser `exec`ado e rodar em ring 0 com espaço
  de endereços próprio, recebendo `argc/argv` em `rdi/rsi` na primeira troca
  de contexto — ver Fase O.

Consequência: o modelo ainda é **predominantemente monolítico**
(`shell + login + fs + exec` dentro do kernel), mas **não é mais 100%** —
`auth` já vive fora do kernel como serviço. O modelo "serviços independentes"
do roadmap é um **objetivo em progresso**, não o estado final.

## 2. Memória virtual (isolamento)

Estado honesto da tabela de páginas:

| Item | Status |
|---|---|
| PMM bitmap (alloc/free de páginas) | ✅ real |
| Heap `kmalloc`/`kfree` (split/coalesce) | ✅ real |
| Paging 4 níveis: identidade + higher-half | ✅ real |
| Espaço de endereços por processo (PML4 clone raso) + troca CR3 no switch | ✅ real |
| `paging_map_user` (página 4KB R/W/U por processo, invalida TLB) | ✅ real |
| **W^X** por página de usuário (código `0x5`, stack `0x7|NX`, EFER.NXE on) | ✅ real |
| **Teardown/GC de processo morto** (PML4, páginas, kernel stack, regiões ELF) | ✅ real |
| Unmap/teardown de espaço ao matar processo | ✅ via GC (`proc_gc_run`) |
| Copy-on-write | ❌ não implementado |
| Demand paging (paginação sob demanda) | ❌ não implementado |
| Páginas de usuário **RWX** | ❌ (eliminado — W^X aplicado) |
| Lifecycle de páginas de usuário | ✅ contagem/alloc + free no GC |

> ⚠️ **Troca de CR3 + W^X + GC dão base de isolamento real** (PML4 clone raso,
> teardown só do que o processo criou, blocos compartilhados com o kernel não
> são liberados). Ainda sem COW e demand paging — é base sólida, enjaulamento
> parcial.

## 3. Auth / senhas

- `auth_hash_password(pass, salt)` usa **PBKDF2-HMAC-SHA256**, custo
  `AUTH_HASH_ITER=100000`, salt aleatório de 16 bytes (32 hex) por conta.
- Implementação kernel própria (`crypto.c`) **validada contra vetores de
  teste** (SHA-256 e PBKDF2/Vectors RFC 7914-style: c=1/2/4096, bloco único
  e multi-bloco) — confirmado em host com `hashlib`.
- Custo medido em QEMU (TCG): ~4.4–5.6 ms/hash — login interativo viável.
- Ainda atende só ao modelo "senha em RAM no boot" (sem persistência);
  upgrade para Argon2id/scrypt previsto quando houver «lib» externa/ring 3.

### ✅ authd: auth como serviço ring 3 (2026-09-11, ABI 1.5)

- O app `/kormi/authd` (anel 3) registra o serviço `"auth"` (`spud_svc_serve`)
  e vira **dono da store** (nome/salt/hash/caps) e da **política**: 1º usuário
  = soberano (CAP_ALL); validação de senha com custo; DELETE/CREATE_USER/
  CHANGEPW-admin exigem `admin_authed` (login de admin válido no boot);
  soberano nunca é removido. O kernel mantém `SYS_AUTH_*`/`auth.c` como
  **fallback**; cada boot usa um único fluxo (authd OU syscalls).
- **Sessão via ticket (ABI 1.5):** `SYS_AUTH_TICKET_ISSUE` (36) exige
  `CAP_USER_ADMIN` (grant kormi) — authd emite `{nome, caps, token-cok}`;
  `SYS_SESSION_LOGIN(name, a2=1, a3=tok)` consome o ticket e monta a sessão
  com os caps decididos pelo serviço, sem ficar dependente do store do kernel.
- **PBKDF2 = primitiva computacional do kernel (`SYS_AUTH_HASH`=37):** em ring 3,
  computo longo preemptado por IRQ corrompia o hash de forma **não-determinística**
  (FALHOU intermitente no self-test; mesma classe do bug do #PF). O authd só
  executa o hash via syscall que roda o `crypto.c` com `disable_interrupts()`
  (padrão já comprovado do login clássico). Store + política + ticket seguem no
  serviço; o kernel aplica a regra recebida (enforcement-point, ver SERVICES.md).
- **Fix latente no kernel (`auth.c`):** `auth_hash_password()` passava
  `saltlen=16` fixo — com salt < 32 hex, bytes residuais de stack entravam no
  PBKDF2. Agora o tamanho real é calculado; contas reais (sempre 32 hex) não
  mudaram de hash.
- **Self-test determinístico:** `[authd] PBKDF2 self-test ok` no boot (vetor
  codificado de `hashlib`, c=100000); `svctest` valida o serviço via IPC
  (ping/count/verify-inexistente) e imprime `[svctest] auth IPC ok`.
- Bootstrap/login em produção via IPC (`spoudazo` → authd); build TEST valida
  o mesmo caminho headless.

### ⚠️ Login interativo: Page Fault no PBKDF2 (corrigido em 2026-09-02)

- **Sintoma**: ao criar o `admin` (e rodar o primeiro PBKDF2) na ISO limpa, o
  processo `init` (PID 1) dava `#PF` dentro de `sha256_update`/`sha256_compress`
  (`mov edx,[rsi+rax]`), `PANIC - sistema parado`. Reproduzível por injeção de
  teclas PS/2 no monitor QEMU, **intermitente**.
- **Causa**: a preempção (IRQ0 do PIT, que roda ISR + `sched_yield` sobre o
  kernel stack do processo) durante o hash de ~5 ms corrompia o estado e o
  `#PF` aparecia ao retomar. O mesmo PBKDF2 passava quando executado com IRQs
  desligadas (bench de boot).
- **Fix aplicado**: `auth_hash_password()` roda o PBKDF2 com
  `disable_interrupts()`/`enable_interrupts()` em volta — o conseque do 1º
  bootstrap/login abrange `auth_create_user`, `auth_verify` e
  `auth_change_password`. Custo: ~5 ms de latência de IRQ no login, aceitável
  e comum para operações cripto de curta duração.
- **Fluxo validado de ponta a ponta (QEMU, ISO limpa)**:
  `Novo admin: thais` → senha + confirmação → login `thais/1234` →
  consentimento dos 2 contratos (`S`) → `Bem-vindo, thais` → prompt do shell
  `thais@T!/arkhe$` (roda e responde a comandos).
- **Ressalva honesta**: o `cli/sti` **contorna** a falha; a causa raiz exata do
  `#PF` sob preempção no caminho de hash **não foi isolada**. Não há troca de
  processo (só `init` existe na ISO limpa; `sched_yield` retorna sem `switch`),
  então a suspeita recai sobre o ISR do PIT sobre o kernel stack do processo.
  Reabrir se outro código cripto-costoso rodar como processo ring 0.

## 4. Synallagma ("contratos")

- Motor: `synallagma_*` no kernel permanece como **primitivo/fallback** (lê
  `/synallagma/*.pacto` — campos `caminho`, `nome`, `leitura`, `escrita`,
  `execucao`, `voluntario`, `revogavel` —, escolhe o contrato mais específico
  e registra consentimento por sessão de login).
- **É serviço desde a ABI 1.6 (2026-09-21):** o app `/kormi/synd` (ring 3) é o
  **dono do store userspace de contratos** — semente via `SYS_CONTRACT_LIST`
  no boot (marker `[synd] store ring3: N contratos userspace`), registra o
  serviço `"synd"` via IPC (`IPC_TYPE_SYN_LIST/READ/CONSENT/REVOKE/AUDIT` =
  types 30-34) e mantém trilho de auditoria no heap do serviço — espelho exato
  do modelo authd/fsd. O kernel (SYS_CONTRACT_LIST + `synallagma_*`) fica como
  primitivo/fallback e é o **ponto de enforcement** em `vfs_open`/sessão
  (autoridade de regras separada da autoridade de execução).
- Reconciliação honesta: a autoridade de *decisão* (store + política) já vive
  fora do kernel; a *execução* (gate) permanece no kernel — como no auth/fs.
  Não roda mais "só" como função interna.
- Plano honesto de separação (o que falta, dependências, aceite):
  [`docs/SERVICES.md`](SERVICES.md).

## 5. Superfície de segurança conhecida (syscalls)

Ponteiros de usuário **são validados contra o espaço de endereçamento do
processo chamador** antes de copiar (`user_ptr_ok`/`user_copy`/`user_str`
+ regiões `umap_*` registradas em `proc_create_user`):

- `SYS_WRITE` (`a1=fd`, `a2=ptr`, `a3=tamanho`) copia de `a2` **só se
  `[a2, a2+a3)` estiver dentro das regiões de usuário** do processo.
- `SYS_FS_*`/`SYS_READ`/`SYS_IPC_*` idem: todo ponteiro é capado por região
  mapeada, nunca por cast confiante.
- Processos ring 0 (kernel/ELF) têm `umap_count=0` e **não** passam por esse
  gate (confiam no kernel).

> A validação fecha a classe de bug "apontar para memória do kernel e
> ler/vazar". Revisar a lista abaixo mantém-se obrigatório a cada novo syscall.

## 6. Veredito por área (marco Fase O + auditoria)

| Área | Maturidade | Nota |
|---|---|---|
| Boot (UEFI ThaisBoot) | 🟢 | boots limpo QEMU UEFI + direto via ESP (`fat:rw`) |
| Scheduler/context switch | 🟢 | cooperativo + preemptivo (PIT), `ran` p/ 1ª execução |
| PMM / heap | 🟢 | funcional |
| Paging (uso kernel) | 🟢 | teardown + W^X + NXE + GC |
| VFS / RAMFS | 🟢 | ownership, caps, normalização |
| Caps | 🟢 | gate real em syscalls/VFS |
| Auth | 🟢 | PBKDF2-HMAC-SHA256 (validado, ~4.4ms/hash) |
| authd (serviço ring 3, ABI 1.5) | 🟢 | `/kormi/authd` dono da store+política, sessão por ticket SYS_AUTH_TICKET_ISSUE/SESSION_LOGIN(a3), hash via SYS_AUTH_HASH (IRQs off); fallback nos syscalls |
| Synallagma | 🟢 | serviço ring 3 `synd` (ABI 1.6): dono do store userspace de contratos — markers `[synd] store ring3` (12º) + `[svctest] synd IPC ok` (13º do runtest), kernel=primitivo/fallback + enforcement (espelho authd) |
| IPC + syscalls | 🟢 | mailbox + `int 0x80` 0..37 (ABI 1.6 — superfície de serviço do `synd`, sem syscalls novos; SYS_COUNT 38) |
| ELF loader | 🟢 | valida header, PT_LOAD, argc/argv ✅ |
| Ring 3 usuário | 🟢/🟡 | cycle completo em demo (`RING3-OK`); services ainda não |
| Serviço `fsd` (superfície IPC via service) | 🟢 | `[svctest] fsd IPC ops ok` — create/write/read/remove/audit expostos + trilho de auditoria do serviço (spud_fsd_*, fallback syscall) |
| Store userspace no `fsd` | 🟢 | `/paradosis` e `/nomos` em copia no heap do serviço (seed do kernel, marker `[fsd] store ring3: ... (N arquivos)`); kernel = primitivo/fallback (modelo authd) |
| `exec_embedded_args` (boot/exec embutido) | 🟢 | boot/sessão e `/bin`,`/kormi`,`/praxis` executam direto de `s_mods[]`; FS só p/ scripts/arquivos de usuário |
| Syscalls ABI 1.0 (SYS_SYSINFO/UPTIME/GETPPID/PROC_LIST/COUNT) | 🟢 | validados via app `fetch` ring 3 |
| Syscalls FS (SYS_FS_READ=7, SYS_FS_LIST=9) acessíveis a ring 3 | 🟢 | validados via apps `ls` e `cat` |
| Apps ring 3 embutidos (`apps_try_run`, `/praxis/*`) | 🟢 | `fetch`, `mem`, `echo`, `ps`, `ls`, `cat` rodam em ring 3 e saem (código 0) |
| argc/argv para apps ring 3 flat | 🟢 | `proc_create_user_args`: rdi=argc, rsi=argv_va (2026-09-03) |
| Integração shell → app ring 3 | 🟡 | código validado via `apps_try_run`; interação PS/2 na automação não confiável |
| Services (init/shell/fs #s serviços) | 🟡/🔴 | `auth` saiu do kernel (authd) e `synd` (ABI 1.6, store de contratos); `fs`/`shell`/`init` ainda residentes — pré-requisito pronto: exec desacoplado do ramfs (`s_mods[]`/`exec_embedded_args`), `fsd` com superfície IPC completa (+auditoria) e **store do serviço ativo: `/paradosis` e `/nomos` em copia no heap do fsd** (marker `[fsd] store ring3: ... (N arquivos)`), kernel = primitivo/fallback (modelo authd). Próximo passo (ADiADO por decisão — ver skill.md §7 "fora de escopo"): migrar os ~93 usos internos de fs/vfs no kernel p/ o serviço (classes boot/exec já desacopladas) |

## 7. Build/teste atuais (como rodar)

```bash
# boot DIRETO sem gerar ISO (sem rebuild de mídia):
mkdir -p build/esp/EFI/BOOT
cp build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
cp build/kernel.elf build/esp/kernel.elf
qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
  -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/X.log \
  -display none -no-reboot

# TEST=1 (apps de demonstração + hello argv + bench PBKDF2):
rm -f src/kernel/*.o src/kernel/arch/x86_64/*.o && make TEST=1

# ISO limpa (SEM testes) — use 'make' puro, sem variáveis:
rm -f src/kernel/*.o src/kernel/arch/x86_64/*.o && make && make iso

# TEST=1 + demo ring 3 (proc_create_user + instrução ring 3 real):
rm -f src/kernel/*.o src/kernel/arch/x86_64/*.o && make TEST=1 RING3=1
# boot log esperado (X.log): "[auth] PBKDF2 1x ... us/hash",
# "ABC[hello] args:...", "RING3-OK"; GC imprime "[gc] coletando ...".
```

> **Apps ring 3 (ABI 1.0):** com `TEST=1`, `init_spawn_test_apps` (testapp.c)
> também spawna o app `fetch` via `apps_try_run("/praxis/fetch")` — o mesmo
> caminho que o shell usa em `praxia /praxis/<app>`. Log esperado:
> `[apps] 'fetch' rodando em ring 3 (pid N)` seguido da saída
> (`Memoria total/usada/livre`, `Processos`, `Uptime`) e
> `[kinesis] processo fetch (pid N) encerrou (codigo 0)`.

> Makefile: obrigatório `.DEFAULT_GOAL := all` antes do `-include *.d`, senão
> o make toma `auth.o` como alvo-padrão e não compila os testes.
> Trocar `TEST`/`RING3` exige `rm -f src/kernel/*.o src/kernel/arch/x86_64/*.o`
> (o make não rastreia mudança de CFLAGS).

> **argc/argv de apps flat ring 3 (2026-09-03):** `proc_create_user_args`
> monta as strings e o array de ponteiros `char* argv[]` no TOPO da stack de
> usuário e entrega `rdi=argc, rsi=argv_va` ao `_start` via `enter_user`
> (lê `[rsp-24]/[rsp-16]`). **Bug corrigido:** as strings e o array
> sobrepunham-se (o array colado abaixo das strings crescia para cima e
> sobrescrevia os bytes das strings, que viravam lixo `0x02`). Layout novo:
> **array no topo, strings logo abaixo (sem sobreposição)**. Apps que usam
> buffer gravável (ex.: putu64) devem usar a **stack (RW/NX)**, nunca `.data`
> — a página de código é `0x5` (W^X: executa, não grava). Validado: `echo`
> imprime `Ola mundo from ring3`, `ps` e `fetch` sem args.
>
> **Novos apps `ls` e `cat` (2026-09-03):** apps ring 3 que acessam o FS via
> syscalls `SYS_FS_LIST` (9) e `SYS_FS_READ` (7). Exigem `CAP_FS_READ`, agora
> concedido a todos os apps embutidos. Validados no build `TEST=1`:
> `ls /synallagma` lista `emporion.pacto`/`idios.pacto`; `cat /nomos/manifesto.txt`
> imprime o manifesto. Ajuda a demonstrar ring 3 acessando um serviço do
> kernel (VFS/RAMFS) só via ABIs de syscall.
>
> **Shell → app ring 3 COM args (2026-09-03):** `cmd_praxia` agora repassa os
> tokens `argc/argv` ao app (`apps_try_run_args(full, argc, argv, ...)`).
> **Bug corrigido no tokenizador:** faltava o `p++` após `*p=0` no primeiro
> espaço — o `while(*p==' ')` caía no NUL e o `break` disparava logo no 1º
> argumento, deixando `argc=1` (app sem args). Validado deterministicamente
> via `thais_exec_line` (exposta p/ TEST) sem depender do PS/2:
> `praxia /praxis/echo ola mundo` imprime `ola mundo`.

## 8. Automação de teste (QEMU headless)- `scripts/run_auto_test.py` (e wrapper `scripts/run_auto_test.sh`) roda o
  sistema direto da source (`fat:rw`, sem ISO), automatiza bootstrap + login +
  consentimento + comandos via monitor QEMU (`sendkey`), esperando os prompts
  no serial log:
  `python3 scripts/run_auto_test.py "praxia /praxis/fetch"`.
- **Ressalva honesta:** a injeção de teclas PS/2 via `sendkey` é frágil (o
  driver do kernel lê o teclado por polling com buffer pequeno) — o login
  automatizado funciona, mas **teclas longas podem ser perdidas** com
  intermitência. Para validação determinística do caminho
  `apps_try_run → proc_create_user → ring3`, o teste embutido em `testapp.c`
  (build `TEST=1`) é a fonte de verdade (reprovável, sem interação de teclado).