# N64 — console experimental e histórico de validações

## Candidato 0.2.2-sd — gráficos Daedalus

O `sd-cores` passa a compilar, por padrão, o decodificador real de display lists,
matrizes, clipping e iluminação do DaedalusX64 (commit
`580c0c409358db332149fc2903c610449434f8fc`). O backend novo em
`components/daedalus/src/software.cpp` rasteriza na RDRAM, com triângulos,
profundidade, cores, combinador de um/dois ciclos, duas texturas, retângulos,
scissor e leitura RGBA16/32, CI4/8, IA4/8/16 e I4/8. Usa nearest-neighbor;
não reproduz exatidão de blender, coverage/AA, fog, LOD e todos os efeitos RDP.
Sprites rotacionados e framebuffer microcodes específicos ainda são recusados.
A implementação inicial visa cenas 320×240; outros modos precisam de validação.

É um porte gráfico híbrido: **a CPU permanece Mupen no interpretador puro**,
com os dispositivos, boot, saves e scanout existentes. Tarefas gráficas OSTask
são processadas por Daedalus e completam as interrupções SP/DP; tarefas de áudio
e outras tarefas continuam no CXD4. Comandos RDP diretos continuam no caminho
Angrylion preservado para regressão. Não há CPU Daedalus nem dynarec RV32.
`-DTAB5_N64_DAEDALUS=OFF` permite compilar a referência LLE. O perfil exclusivo
`n64` continua com LLE por padrão; o `stable` continua sendo o perfil padrão do
projeto, com GBA interpretado. Nenhum gerenciador do GBA RV32 foi alterado.

O staging confere todos os SHA dos fontes originais antes de gerar adaptações.
Há limite compartilhado de 40000 comandos por tarefa (inclusive lotes de
triângulos), limite de varredura de 8 milhões de posições e verificações de
pilha/endereço. O produtor coopera com o escalonador durante listas e raster;
no frontend, a espera de 1 ms fica limitada a uma vez a cada 4 ms nesse callback.
Isso permite trabalho das tarefas do sistema; não certifica latência da pausa.
Listas cíclicas, pilha profunda, endereços fora da RDRAM ou desalinhados falham
com diagnóstico. Leituras/escritas de meia palavra CXD4 no staging usam cópias
de bytes com wrap de DMEM, evitando acesso C desalinhado no RV32.

```sh
python3 tools/test_daedalus_host.py
python3 tools/build_n64_host.py --daedalus --sanitize --output /tmp/n64-hle
# ROM própria .z64, sem download ou redistribuição:
# TAB5_N64_PLAY=1 /tmp/n64-hle jogo.z64 300000000 /tmp/cena.ppm
bash tools/build.sh sd-cores
```

A suíte com ASan/UBSan verifica listas F3D próprias 0BSD: preenchimento,
scissor, triângulo transformado, TexRect RGBA16, interrupção DP e recusa de
listas circulares/aninhamento excessivo/ponteiros inválidos. O console LLE e os
testes anteriores permanecem preservados. O mesmo teste do console, compilado
com HLE, também despacha uma OSTask F3D pela interface SP real: confere escrita
na RDRAM, SP TASKDONE/BROKE/HALT e interrupções SP/DP, sem incrementar instruções
RSP/listas RDP. Compile com `build_n64_host.py --daedalus --sanitize --console-tests`
e execute com a ROM própria gerada por `build_n64_console_rom.py`. O runner com uma ROM do usuário
produziu logotipo, rosto e cena externa do castelo/diálogo inicial SM64; capturas e dados do jogo permanecem locais.
Ensaios adicionais produziram o título Mario Kart 64 e a introdução 3D a cavalo de Ocarina of Time, também sem falha ASan/UBSan. São cenas do host, sem prova física ou certificação de gameplay.
`hle_tasks`, `hle_triangles` e `hle_pixels` distinguem trabalho HLE das listas
RDP e instruções RSP reais; não são FPS nem prova de velocidade no P4.

## Validação física 0.2.2-sd

Após autorização do usuário, que atualizou o Launcher, a instalação passou pela
API de operações do Launcher. O catálogo estava vazio: o alocador escolheu
**app0**, reservou 4063232 bytes e retomou a instalação após reiniciar. Não foi
forçado app4 nem usado esptool/offset bruto. O app-only tem 4047216 bytes, SHA
`5dc96802979a68d235cc9812127b259d07c4a739037659e11109cb2e71f88856`;
GET_INFO confirmou ELF `b2000f29e1ef4438f678b2fbe67b2c6c133440672cbb9bb7ce577012114e3996`
no app0, com CPU de 360 MHz. Imagem e cinco módulos foram relidos no SD por SHA.
A geração anterior dos módulos e os jogos permaneceram no cartão.

Os seis sistemas passaram por 13 ciclos de boot/saída e recuperação após módulo
GBC truncado propositalmente. GB 11, GBC 15, GBA 16, SNES 11, MD 17 e N64 22:
92 entradas. A fixture N64 confirmou A, EEPROM 11→12 e backup; o ensaio mantém
as mesmas regras de save, sem inventar suporte a estados instantâneos.

SM64 mostrou logotipo e rosto corretos e chegou à seleção de arquivo por Start.
As janelas medidas deram **4,10 VI/s no título** e **1,88 VI/s no rosto**, contra
2,19 e 1,16 na 0.2.1-sd, com CPU de 360 MHz em ambas: ganhos aproximados de 87%
e 62%. São janelas por cena de um ensaio por versão, sem sincronização quadro a
quadro. VI/s não é FPS do jogo nem prova de velocidade normal. Ainda é lento
demais para jogar SM64; áudio continua com muitos blocos vazios. Gameplay no
Tab5 e compatibilidade geral não foram certificados.

Ao final, o aparelho voltou à biblioteca de todos os sistemas, com configurações
restauradas e coredump vazio. Evidência sanitizada:
`artifacts/daedalus-hardware-verification-v0.2.2.json`. A limitação do início de
áudio GBA em introduções pesadas está registrada em [AUDIO.md](AUDIO.md).


Na 0.2.1-sd, o perfil `sd-cores` reúne N64 e os cinco sistemas maduros, carregando módulos do cartão. Veja [núcleos no SD](NUCLEOS_NO_SD.md). A prova 0.2.0 abaixo corresponde ao perfil exclusivo anterior, preservado como diagnóstico.

O perfil separado `n64` integra um console: CPU Mupen64Plus no interpretador puro, RSP CXD4 e RDP Angrylion em software, com boot de cartucho, RDRAM de 8 MiB, TLB linear, Count/Compare/VI, DMA PI/SP/SI/AI, PIF/Joybus, vídeo RGB565 320×240, áudio e controle de um jogador. É experimental: execução de títulos e desempenho precisam de evidência por cena. Não há JIT N64 nem mudança no backend experimental GBA.

O `stable` continua sendo o padrão, com GBA interpretado e todos os cinco sistemas maduros. O novo perfil ocupa o mesmo app4 e substitui temporariamente esses núcleos por N64; os arquivos e saves continuam no cartão. O perfil antigo `n64-cpu-probe` continua disponível como diagnóstico independente.

## Reproduzir o console

```sh
bash tools/test_n64_console_host.sh
# Opcional: runner de cena para ROM própria .z64 (saída e imagem locais):
python3 tools/build_n64_host.py --output /tmp/tab5-n64-host
# /tmp/tab5-n64-host jogo.z64 50000000 /tmp/cena.ppm
bash tools/build.sh n64
python3 tools/check_n64_artifact.py build-n64 --report artifacts/n64-console-final-build.json
# Somente com o cartão sob posse exclusiva do computador:
python3 tools/sync_n64_roms.py --source 'ROMs/Nintendo 64' --root '/caminho/do/cartao/Tab5-Emu' --apply --report /tmp/n64-transfer.json
```

As ROMs `.z64`, `.v64` e `.n64` ficam em `Tab5-Emu/roms/n64/`. A ferramenta deduplica arquivos idênticos, confere tamanho e SHA da releitura, preserva as origens/destinos ocupados e faz backup do catálogo/índice antes de atualizá-los. Não baixa jogos. Ejete pelo sistema operacional antes de devolver o SD ao Launcher.

`tools/stage_n64.py` gera cópias adaptadas de fontes fixados por SHA, usadas tanto no host como no firmware. CXD4 está fixado em `2a4a30094fdb9f593157cfec978c6560212752fd`; Angrylion em `9c8b9ed3e7d7f00dff8bc872ccdd3fba1a3673fc`. As licenças e os avisos acompanham `experiments/n64/plugins`; a auditoria para distribuição pública desse conjunto permanece pendente.

EEPROM, SRAM, FlashRAM e Controller Pak usam um contêiner de bateria próprio de 198656 bytes, com o journal/backup do BatteryStore e identidade SHA da ROM. Estados instantâneos e importação de formatos N64 de outros emuladores ainda não estão implementados. Não confundir save normal com save state.

O teste original 0BSD `tools/build_n64_console_rom.py` exercita boot, Count/Compare, VI, RDP, RSP, DMA, A/analógico, reutilização de comandos SI, áudio e EEPROM; os testes também conferem FlashRAM e segundo boot. ASan/UBSan são executados no host. O programa não contém IPL/BIOS/logo proprietário.

Ensaios host com as ROMs fornecidas chegaram ao título 3D de Super Mario 64 e Mario Kart 64 e à introdução 3D de Ocarina of Time, com instruções RSP, listas RDP e amostras de áudio reais. São provas de cenas, sem certificação de gameplay ou velocidade no P4. Donkey Kong 64 passou da detecção de Expansion Pak após o handoff de 8 MiB, mas ainda apresentou imagem corrompida; não é anunciado como compatível.

O adaptador respeita a janela de apagamento VI: `H_START=0` e modo sem pixels geram preto, evitando exibir o framebuffer que libultra reutiliza durante cargas. O caso surgiu na transição do título SM64 e tem regressão de apagar/reativar no teste real do console.

Pendências: conformidade completa de CPU/TLB/exceções, temporização de AI/FIFO e periféricos, RDRAM elétrica, exatidão VI (AA/gamma/interlace), CIC/EEPROM por título, multiplayer e rumble, estados e otimização de velocidade. O renderizador de software pode ser muito lento no P4. A antiga evidência headless abaixo continua histórica; seus limites não descrevem o novo console.

## Validação física do console 0.2.0-n64

Imagem SHA `c774e6f9bcfd9c15a2fb278e67947079bfeaa1d55c6ccacc01bb6a93ce5911e4`, ELF `917ed3a68e8bdabc1a602eb6846023d9057929c14a8e3a46ed54f71cc2550865`, instalada e relida pelo Launcher em app4. Os demais aplicativos e reservas foram preservados. O diagnóstico original gerou cores corretas por A e confirmou EEPROM 1→2 com backup e coredump vazio.

No mesmo ELF, Super Mario 64 gerou título e rosto 3D animado, apagou a saída durante a carga e iniciou fade por Start. O intervalo completo durou aproximadamente 252 s. A cena do título ficou em cerca de 2 VI/s e a do rosto em cerca de 1 VI/s; não oferece velocidade de jogo normal. A saída AI submeteu áudio, mas o sink teve muitos intervalos vazios e silêncio. Seleção de arquivo, gameplay e compatibilidade dos outros 20 títulos não foram certificados no aparelho. Mario Kart/Ocarina têm somente evidência host nesta etapa.

Ao final, o Tab5 voltou à biblioteca N64, sem panic/coredump. Há 21 jogos fornecidos (444 MiB) e um diagnóstico próprio. Evidência sanitizada: `artifacts/n64-hardware-verification-v0.2.0.json` e `artifacts/n64-rom-transfer.json`. O stable 0.1.17 foi compilado/verificado e deixado no SD como alternativa; não se atribui a ele este ensaio físico N64.

## Histórico do primeiro marco headless

## Fontes investigadas

| Base | Commit consultado | Observação inicial |
|---|---|---|
| Mupen64Plus core | daae68e2cc871cef4ac76e162c173894d052a28d | interpretadores puro/cacheado em src/device/r4300; backend RV64 não é RV32; plugins e serviços de desktop precisam de adaptação |
| ParaLLEl-N64 | e854c84a175388bfb4643ff654522be8c941baf1 | Makefile e opções de renderizadores consultados; extrair uma compilação sem Vulkan/OpenGL e auditar software RDP antes de escolher |
| DaedalusX64 | 580c0c409358db332149fc2903c610449434f8fc | árvore Core/HLEGraphics e backends SysGL/SysGLES; não assumir que renderer de desktop funcione no P4 |

As cópias de pesquisa em `.tools` não são dependências executáveis do stable. A investigação inicial antecedeu o novo console descrito acima. Fontes primárias: [Mupen64Plus](https://github.com/mupen64plus/mupen64plus-core), [ParaLLEl-N64](https://github.com/libretro/parallel-n64), [DaedalusX64](https://github.com/DaedalusX64/daedalus).

## Código e reprodução

`experiments/n64/mupen/UPSTREAM.json` fixa o core `daae68e2cc871cef4ac76e162c173894d052a28d`; fonte e avisos originais acompanham o repositório. O adaptador `probe.c` fornece acessores no layout upstream, memória KSEG0/1 big endian de 4 KiB, Count básico e término explícito para dispositivos/TLB/scheduler ausentes. A cópia do interpretador acrescenta somente o hook de contagem/limite. Não há dynarec RV32.

```sh
python3 tools/build_n64_probe.py
bash tools/test_n64_host.sh
bash tools/build.sh n64-cpu-probe
```

O programa de 92 bytes é original 0BSD, SHA `9c8d794f22356d396b5f7780a2538356f474503f1d250456e1da33bde4c36c47`. Não contém ROM/BIOS/logo de console e não é embutido no firmware. Para o perfil integrado, coloque-o em `Tab5-Emu/benchmarks/tab5-n64-cpu-probe.mips`. O startup exige tamanho/hash exatos, executa o ensaio, libera sua memória e grava `benchmarks/n64-cpu-probe-<versão>.json`, vinculando o ELF do próprio app. O stable não liga esse código.

O perfil `n64-cpu-probe` troca temporariamente **GB/GBC** pelo diagnóstico, conservando GBA/SNES/MD, o frontend e os arquivos do cartão. A versão 0.1.13 compilou com 4013792 bytes, margem 49440 na mesma reserva app4 de 4063232. O combinado com todos os núcleos foi medido e excedeu a reserva; a troca foi escolhida antes da instalação, sem mudar flags dos núcleos maduros ou adicionar padding artificial. Voltar ao stable restaura GB/GBC e seus dados.

Existe também um projeto standalone em `experiments/n64`: build app-only mínimo e protocolo USB próprio. Seu envio pequeno foi interrompido pelo guard do alocador do Launcher, que tentaria encolher a reserva. Não se executou a reorganização; o stable foi restaurado. A prova física inicial ocorreu pela integração na 0.1.9, e não por esse firmware standalone. Não usar `flash`/`app-flash` ou uma tabela do build no Tab5.

## Resultados e limites

No host, 22 instruções realmente executadas conferiram ALU/shifts de 64 bits, HI/LO, SD/LD e endianness, desvio com delay slot, registrador zero, MTC1/MFC1 e exceção BREAK. Cinco ensaios negativos verificam tamanho inválido, limite de execução, idle loop que exige scheduler, acesso fora da RAM e TLB ausente. Rodam com ASan/UBSan. Isso não valida toda a CPU nem operações aritméticas FPU.

No P4 1.3 a 360 MHz, a 0.1.9 conferiu as mesmas 22 instruções, Cause 36 e EPC 0x80000058. O `r4300_core` ocupou **13635168 bytes**; no host ocupa 17830328, diferença de layout/ponteiros da arquitetura. O intervalo de aproximadamente 106746 µs inclui alocação e limpeza, não é taxa de instruções por segundo. Estruturas cacheadas/TLB do upstream ainda fazem parte do layout apesar de não usadas nesse ensaio. Evidência: `artifacts/n64-probe-p4-result.json`.

| Marco | Estado |
|---|---|
| Core escolhido, build e execução headless | realizado no escopo do ensaio MIPS próprio |
| CPU de 64 bits e endianness | subconjunto testado; correção completa pendente |
| CP0/exceções/interrupts/TLB/FPU | BREAK e transferência CP1 parciais; timers/MMU/scheduler completos pendentes |
| RDRAM e boot livre | pendentes; não há cartucho N64 executado |
| VI/framebuffer e RSP/RDP | pendentes |
| PIF, controle e áudio | pendentes; nenhum mapa N64 ativo no jogo |
| Homebrew interativo e jogo representativo | pendentes |
| EEPROM/SRAM/FlashRAM/Pak/estados | pendentes |

Orçamento futuro: RDRAM 4/8 MiB, framebuffers, CPU/RSP/RDP, ROM/cache, texturas, frontend e áudio. O tamanho medido do core evidencia que 32 MiB de PSRAM não equivalem a espaço livre para ROM. PPA não substitui RDP/3D. Antes de anunciar compatibilidade, cada ROM precisa de licença/hash, cena, velocidade emulada, imagens realmente geradas, áudio, memória e erros registrados.

## Regressão física do perfil 0.1.13

Instalado pelo Launcher no mesmo app4, sem mudar reservas ou descritores dos outros apps. O ELF `56ffa7176fa0208121e01123ae688634fbb897ba654fccebc5f6d7bdf4c6df45` confirmou novamente as 22 instruções, Cause 36, EPC 0x80000058 e core de 13635168 bytes; intervalo total 106643 µs. A biblioteca mostrou quatro entradas, omitindo GB/GBC conforme o perfil. Evidência em `artifacts/n64-probe-p4-v0.1.13-result.json`. Depois do ensaio, o stable foi restaurado com seu ELF próprio e os sete jogos/diagnósticos do cartão reapareceram.

## Regressão do corretivo 0.1.14

O diagnóstico de 4013392 bytes foi instalado na mesma reserva, executou novamente as 22 instruções e liberou o core antes da UI. ELF `45c26dd3e702c7f093c056069ef64f5786278ac9e12dc1c6c37ef2c17c382ca0`, Cause 36, EPC 0x80000058, core de 13635168 bytes e intervalo de 106842 µs, incluindo alocação. A lista omitiu GB/GBC e mostrou quatro entradas. O comando legado de updater foi recusado na HOME e no GBA sem reset; toque Start e retorno direto da pausa passaram. Evidência CPU: `artifacts/n64-probe-p4-v0.1.14-result.json`. O teste positivo e as cinco recusas também passaram novamente no host com ASan/UBSan.
