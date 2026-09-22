# Siath OS — Dicionário de Diretórios (grego + função)

Todos os nomes são transliterações gregas, função clara, sem `/usr`/`/etc` genérico.

| Caminho | Grego | Significado | Equivalente Linux | Easter egg |
|---|---|---|---|---|
| `/arkhe` | ἀρχή | Princípio/origem | `/` | — |
| `/praxis` | πρᾶξις | Ação humana (Mises) | `/bin` | — |
| `/techne` | τέχνη | Arte/técnica | `/sbin` | — |
| `/nomos` | νόμος | Lei/norma (Hayek) | `/etc` | — |
| `/emporion` | ἐμπόριον | Mercado | `/usr` + `/var/lib` | — |
| `/agora` | ἀγορά | Praça/mercado federado | `/var/cache` | — |
| `/idios` | ἴδιος | Próprio/individual | `/home` | `/idios/thais` |
| `/oikos` | οἶκος | Casa/sistema privado | `/var/private` | — |
| `/kinesis` | κίνησις | Movimento/processos | `/proc` `/run` | — |
| `/paradosis` | παράδοσις | Entrega/temporário | `/tmp` | — |
| `/aisthesis` | αἴσθησις | Percepção/dispositivos | `/dev` | — |
| `/catallaxy` | catallaxy | Ordem de trocas (Hayek) | `/net` `/srv` | — |
| `/synallagma` | συνάλλαγμα | Contrato | `/etc/init.d` | — |
| `/op3n` | — | Discord criadora | — | symlink → `/idios/thais` |
| `/op3ny` | — | Github criadora | — | symlink → `/idios/thais` |
| `/menger` `/hayek` `/rothbard` `/mises` | — | Austríacos | — | easter eggs docs |

Exemplo de contrato voluntário `/synallagma/net.pacto`:
```toml
[contrato]
nome = "catallaxy.net"
acao = "/techne/net"
voluntario = true
caps = ["catallaxy"]
```
