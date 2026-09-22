# Siath OS

> Sistema operacional microkernel de código aberto para **x86_64 (UEFI)**, escrito do zero em **C e Assembly**.
> Release: **Siath OS v1.0.0 alpha — "Independent System"** · 2026-09-22 · ABI 1.6 (SYS_COUNT=38)

## O que é

O Siath OS é um sistema operacional real, em desenvolvimento ativo, que roda numa máquina virtual (QEMU) e num hardware físico UEFI. Ele não é uma distro Linux nem um fork: cada camada — do bootloader ao kernel, do sistema de arquivos à shell — foi escrita do zero, em C e Assembly, sem dependências externas.

É um projeto de engenharia de sistemas: um lugar para experimentar como um SO funciona por dentro (memória, processos, permissões, drivers), com código próprio e audível, sem segredos.

## O que ele é capaz de fazer

Estado atual (tudo abaixo roda de verdade, no QEMU):

- **Boot UEFI próprio** — bootloader original (`ThaisBoot`, PE32+) que carrega o kernel sem depender do GRUB.
- **Interface gráfica de boot** — splash centralizado com barra de progresso durante a inicialização.
- **Multitarefa preemptiva** — vários processos rodando ao mesmo tempo, alternados por um escalonador (round-robin com timer).
- **Gerenciamento de memória** — alocação de páginas físicas, heap próprio, memória virtual com paginação de 4 níveis.
- **Sistema de arquivos em memória (RAMFS)** — árvore de diretórios com dono e permissões; nada é perdido ao desligar porque é volátil por projeto (persistência em disco é meta futura).
- **Usuários e login** — primeiro acesso cria o administrador; senhas guardadas com hash forte (PBKDF2-HMAC-SHA256).
- **Shell própria** — prompt interativo com comandos nativos (em grego transliterado) e aliases POSIX (`ls`, `cat`, `cd`...) que funcionam normalmente.
- **Programas em modo usuário (ring 3)** — executa binários ELF64 como processos separados, cada um com seu espaço de memória.
- **Serviços em anel 3** — autenticação (`authd`), sistema de arquivos (`fsd`), teclado (`odigos`), dispositivos (`devd`) e contratos de serviço (`synd`) rodam como processos de usuário, não dentro do kernel.
- **Permissões por capacidades** — nada de superusuário onipotente: cada processo só acessa o que lhe foi concedido.
- **Comunicação entre processos** — mailboxes e 38 chamadas de sistema (`int 0x80`, ABI 1.6).
- **Drivers** — framebuffer (tela), teclado PS/2 e USB (xHCI), timer (PIT), porta serial e barramento PCI.
- **18 aplicativos embutidos** — `ls`, `cat`, `echo`, `ps`, `mem`, `heap`, testes de IPC, serviços do sistema etc., empacotados no kernel e executados em ring 3.

## Como experimentar

Requisitos (Debian/Ubuntu/elementary OS):

```bash
sudo apt install build-essential nasm xorriso qemu-system-x86 ovmf mtools
```

Compilar e rodar:

```bash
make iso          # gera build/siath.iso
make run          # QEMU UEFI (com janela gráfica)
make run-headless # QEMU sem display, log serial em build/boot.log
```

No primeiro boot o sistema pede para criar o administrador (`Novo admin`), depois a senha (e confirmação). Em seguida há o login, o consentimento dos contratos de serviço e a shell `praxia`:

```
Siath OS v1.0.0 alpha
=== Primeiro acesso ===
Novo admin (nome): thais
Senha: ···
=== Login ===
usuario: thais
senha: ···
Bem-vindo, thais
thais@thais:/arkhe$ ls
```

> O boot de produção é validado a cada release: o log precisa chegar ao prompt `Novo admin` sem panics.

## Como funciona (visão geral)

Em três camadas:

1. **Bootloader (ThaisBoot)** — inicializa o UEFI, obtém o framebuffer e o mapa de memória, carrega `kernel.elf`, sai dos serviços do firmware (`ExitBootServices`) e salta para o kernel no modo 64 bits.
2. **Kernel (thais-kernel)** — o núcleo: gerencia memória, processos, escalonador, interrupções, sistema de arquivos em RAM, syscalls e capabilities. Hoje também hospeda o console de login/shell.
3. **Processos de usuário (ring 3)** — serviços (`authd`, `fsd`, `devd`, `odigos`, `synd`) e aplicativos (`ls`, `echo`, ...), executados com espaço de endereçamento próprio e acesso restrito por capacidades.

```
BOOT (ThaisBoot/UEFI)
   └─▶ KERNEL (memória · processo · escalonador · syscalls · FS)
           └─▶ serviços ring 3 (auth, fs, teclado, dispositivos, contratos)
                  └─▶ login/shell praxia · apps user ring 3
```

## Estrutura do sistema de arquivos

Os nomes de diretórios são transliterações do grego, cada um com função clara (nada de `/usr`/`/etc` genérico):

| Caminho | Função | Equivalente (Linux) |
|---|---|---|
| `/arkhe` | raiz | `/` |
| `/praxis` | executáveis | `/bin` |
| `/techne` | ferramentas de sistema | `/sbin` |
| `/kormi` | serviços do sistema (drivers/daemons) | `/bin` + `/init.d` |
| `/nomos` | configurações | `/etc` |
| `/idios` | diretórios pessoais | `/home` |
| `/oikos` | dados internos do sistema | `/var` |
| `/kinesis` | processos | `/proc` + `/run` |
| `/paradosis` | arquivos temporários | `/tmp` |
| `/aisthesis` | dispositivos | `/dev` |
| `/catallaxy` | rede | `/net` |
| `/emporion` | pacotes/bibliotecas | `/usr` + `/var/lib` |
| `/agora` | cache de repositórios | `/var/cache` |
| `/synallagma` | contratos de serviço | `/etc/init.d` |

Comandos principais da shell (os aliases POSIX também funcionam):

| Nativo | Função | Alias |
|---|---|---|
| `horasis <caminho>` | listar | `ls` |
| `metabasis <caminho>` | mudar de diretório | `cd` |
| `ktisis <nome>` | criar arquivo/dir | `touch` / `mkdir` |
| `graphe <arquivo>` | ler arquivo | `cat` |
| `praxia <bin> [args]` | executar programa | `exec` / `run` |
| `kinesis` | listar processos | `ps` |
| `aisthesis list` | listar dispositivos | — |
| `synallagma list` | listar contratos | — |
| `passwd`, `useradd`, `whoami`, `edit`, `echo`, `pwd`, `clear`, `reboot`, `poweroff` | — | — |

## Estado atual (honesto)

O Siath OS está em **alpha (v1.0.0 alpha)**. O núcleo (boot, memória, escalonador, FS, syscalls, ring 3) é sólido e coberto por regressão automatizada; porém:

- **Login, shell e parte do FS ainda rodam dentro do kernel** — a migração completa para o modelo "tudo como serviço" é o objetivo em andamento (auth, fs e contratos já saíram do kernel como serviços ring 3).
- **Persistência** ainda não existe (sistema de arquivos é em memória RAM).
- **Rede** e **runtimes** (Python/Node) são metas futuras.

A versão técnica detalhada — o que é real, o que é parcial e o que é planejado — está no **Manual Técnico** e em [`docs/STATUS.md`](docs/STATUS.md).

## Documentação

- **[Manual Técnico](docs/MANUAL-TECNICO.md)** — arquitetura, subsistemas e detalhes de implementação (recomendado para quem quer entender como funciona por dentro).
- [docs/STATUS.md](docs/STATUS.md) — status técnico honesto: o que é real vs. objetivo.
- [docs/SYSCALL-ABI.md](docs/SYSCALL-ABI.md) — ABI das 38 chamadas de sistema e gates de capability.
- [docs/SERVICES.md](docs/SERVICES.md) — modelo de serviços ring 3 e plano de separação.
- [docs/DIRS.md](docs/DIRS.md) — dicionário dos diretórios.
- [docs/COMANDOS.md](docs/COMANDOS.md) — comandos da shell.

## Licença

MIT.