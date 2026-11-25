# Investigação GBA — interpretador e candidato RV32

As medições históricas abaixo usam homebrew próprio no Tab5. O desenvolvimento 0.1.15 acrescenta um perfil experimental RV32 e ensaios pareados; seu resultado deve ser lido separadamente, vinculado ao ELF testado. Nenhum FPS do upstream ou de outro aparelho é medição deste port.

## Fatos conferidos no código

- gpSP vendorizado no upstream: `components/gba/gpsp/UPSTREAM_COMMIT`, revisão libretro `8d268a6bb2cd799f8f2791ebb544a7ef550cfc6f` mais alterações locais da base.
- O perfil `stable` usa `cpu.cc`, `-O3`, cache ROM de até 16 MiB, sem `HAVE_DYNAREC`. O perfil `gba-rv32` inclui o tradutor RV32 e permite selecionar interpretador ou dynarec antes de abrir o jogo; omite SNES para caber na mesma reserva app4.
- `gba.cpp`: dynarec desativado no stable; seleção explícita `gba_cpu=0/1` no perfil experimental. Frameskip auto, som 32768 Hz, BIOS auto, temporização aproximadamente 59,7275 Hz, intervalo acumulado. gpSP pode paginar ROMs maiores pelo SD.
- `libretro.c`: sem HAVE_DYNAREC, `retro_run()` chama `execute_arm`, o interpretador. A frequência real vem de `retro_get_system_av_info`.
- O host original não preservava `RETRO_MEMORY_SAVE_RAM` ao encerrar, e gravava estados diretamente no arquivo definitivo. Essa é uma falha de persistência, não evidência de gargalo.

O fork HowBoyAdvance foi baixado e fixado em `d2ed7442e7fbca96146ce043ae127835fa7e4ce8`. Há código RV32 real: `riscv/riscv_codegen.h`, `riscv/riscv_emit.h`, `riscv/riscv_stub.S`, flags HAVE_DYNAREC/RISCV_ARCH/MMAP_JIT_CACHE. `gpsp_esp.c` mapeia cache executável, usa APIs de MMU/cache e alocação próprias do Tanmatsu. Não é uma opção que basta ativar no adaptador libretro atual. Licença GPLv2 deve acompanhar qualquer reutilização.

mGBA é candidato de comparação de correção; não foi portado nem medido, e não há motivo para declarar seu interpretador mais rápido. Nenhum núcleo foi substituído ainda.

## Próxima execução reproduzível

Fixar ROM livre ou fornecida pelo usuário por SHA-256, cena/input, revisão real do chip, cartão, CPU 360 MHz, SDK e flags. Não usar overclock. Repetir cache frio/quente. Medir frames emulados/tempo real a 59,7275 Hz, desenhados, apresentados, descartados, CPU/PPU, áudio, cópia/PPA, espera, SD, heap/maior bloco e mediana/p95/p99.

Comparar baseline e novo frontend; apresentação normal/nula preservando PPU; áudio normal/nulo preservando tempo; cache/residência; depois interpretador/RV32 validado. Os contadores atuais de frames desenhados/pulados não separam todas essas fases. Instrumentação completa e dados brutos são pendências explícitas.

Estados são específicos do núcleo/ABI. Persistência normal GBA nova é raw 128 KiB conforme `retro_get_memory_size`, guardada por hash da ROM. A continuidade de SRAM do homebrew foi validada fisicamente; variantes EEPROM/Flash de títulos ainda exigem teste.

## Instrumentação adicionada

O adaptador registra cada `retro_run`, callback de vídeo, callback de áudio, espera do pacing, bytes de áudio pedidos/aceitos, frames emulados/desenhados e janela de 1024 tempos run para mediana/p95/p99. A consulta USB `settings` publica `gba_metrics`. CPU e PPU permanecem agregadas em run; bytes não aceitos não equivalem por si só a underrun. Ainda faltam tempos internos de CPU/PPU, SD e apresentação.

`tools/emulator_usb.py benchmark --seconds 30 --output ...` usa tempo monotônico externo e exige GBA ativo. Compara deltas de frames emulados e apresentados, separando velocidade relativa de FPS da apresentação. Mudança de ROM, reset de contadores e falta de progresso abortam a medição. O diagnóstico USB original revelou estouro da pilha; scratch foi corrigido e consultas físicas repetidas passaram.

Homebrew próprio: `tests/homebrew/gba`, licença 0BSD, build `tools/build_test_roms.py`. A ROM testa framebuffer Mode 3, contador de frames, entrada, tom PSG e SRAM; não contém logo de fabricante e usa boot direto game do gpSP. Não é teste de conformidade de boot de console nem suíte de compatibilidade comercial.

## Resultado físico — 0.1.1, homebrew leve

ESP32-P4 rev. 1.3, CPU 360 MHz, PSRAM 32 MiB/200 MHz, ESP-IDF 6.1, gpSP interpretado. ROM 0BSD `ab627e010bda9132328e5b2f69b5ec1c98f19a5a45737a9bce1dc4553d03d234`; modo 3, tom PSG e SRAM, sem BIOS/logo proprietários. Uma execução de 30 s com consultas USB a cada segundo; o intervalo entre primeira/última amostra foi 29,129636792 s. Dados brutos públicos: `artifacts/gba-homebrew-011-benchmark.json`. Não houve screenshot ou entrada durante o intervalo medido.

| Medida | Resultado |
|---|---:|
| Frames emulados no intervalo | 1740 |
| Frames apresentados no intervalo | 1740 |
| Frames emulados por segundo | 59,73298 |
| Velocidade relativa a 59,7275 Hz | 1,0000917 (100,009%) |
| Mediana/p95/p99 de run, janela final 1024 | 13,373 / 13,792 / 16,191 ms |
| Menor heap interno livre entre amostras | 169527 bytes |
| Menor PSRAM livre entre amostras | 10708036 bytes |

Callbacks de vídeo/áudio são subconjuntos de `run_us`, e não devem ser somados novamente ao total. `pacing_us` é separado. Bytes pedidos/aceitos foram iguais durante a captura; isso não prova qualidade audível ou ausência de underrun no DAC. Essa cena leve não estima jogos maiores, PPU pesada, acesso ao SD ou o custo de outros núcleos. Ainda faltam execuções repetidas frias/quentes, cenas variadas e os experimentos de isolamento exigidos.

O smoke físico confirmou direção diagonal, botão A mudando a cor, Start incrementando o contador, salvar/carregar estado, SRAM raw 128 KiB após sair e contador em boot novo com resume desativado. No gpSP atual, o estado inclui protocolo de backup mas **não os bytes de gamepak_backup**; carregar estado antigo conserva a SRAM mais recente. O teste registra essa política separadamente. Na 0.1.2, flush usa SHA-256 para ignorar gravação idêntica e manter a geração anterior distinta; estado de tamanho inesperado é recusado antes de alocar/alterar a máquina. A regressão física dessa correção passou nas versões seguintes, incluindo 0.1.5.

## Regressão física 0.1.2

Outro ensaio leve de 30 s: intervalo amostrado 29,112211625 s, 1739 frames emulados (59,73438/s; velocidade relativa 1,0001152) e 1738 apresentados (59,70003/s). Janela final: mediana 13,419 ms, p95 14,134 ms e p99 16,195 ms. Essa diferença de uma apresentação no intervalo não foi tratada como queda na velocidade emulada. O segundo ensaio não constitui A/B controlado nem prova de melhoria. Dados em `artifacts/gba-homebrew-012-benchmark.json` e também no SD/benchmarks, relidos com hash igual.

O smoke 0.1.2 passou novamente e comprovou que o backup anterior **distinto** é mantido após o novo save e um boot/saída sem mudança. Um estado de 1 byte, criado somente no slot descartável da ROM própria, foi recusado; o jogo continuou avançando, o arquivo de teste foi removido e o estado válido foi recarregado. Falhas de energia/SD cheio continuam pendentes.

## Regressão física — 0.1.5

Após dois segundos de aquecimento e boot com resume desativado, o homebrew próprio emulou e apresentou 1744 frames em 29,200914958 s entre amostras: **59,72416 frames emulados/s**, velocidade relativa 0,999944 (99,9944%). Dados brutos em `artifacts/gba-homebrew-015-benchmark.json`, também enviados e relidos no SD/benchmarks. Sem entrada ou screenshot durante a medição. Não é comparação A/B nem prova de ganho; continua sendo uma única cena leve.

A 0.1.5 usa o BatteryStore residente comum a GB/SNES. Smoke de entrada/estado/SRAM/backup passou novamente. Recuperação de primário truncado preservou `.rejected`; com ambas as gerações inválidas o jogo foi recusado sem alterar os arquivos. Restaurar os bytes válidos permitiu reabri-lo.

## Regressão 0.1.13

Na mesma ROM própria leve, sem entrada ou captura durante o intervalo: **59,74926 frames emulados/s**, velocidade relativa 1,000364; 1749 frames apresentados no intervalo (59,71512/s). A diferença entre contagens de emulação e apresentação permanece explícita. A amostragem curta tem quantização de quadros e não constitui ganho A/B. A persistência normal agora usa journal SHA com raw preservado; testes de corrupção usam somente a ROM gerada.

`audio_sink` acrescenta observação do stream e das escritas I2S, separando faltas de amostras de bytes aceitos pelo produtor. Ainda faltam medição interna CPU/PPU e SD, cenas pesadas, repetições frio/quente, saída nula controlada e um port RV32 comparável.

## Candidato RV32 — desenvolvimento 0.1.15

O emissor/stub vem da revisão fixada do HowBoyAdvance (GPL-2.0-or-later, fontes e licença em `components/gba/rv32`). Usa o tradutor do gpSP atual, incluindo resolução adiada de saídas para limitar a pilha. O adaptador Tab5 reserva e mapeia PSRAM executável, mantém os registradores usados pelo assembly contíguos na RAM interna e sincroniza dados/instruções. A primeira tentativa física abortou na sincronização: o alinhamento de 64 bytes não satisfazia as linhas de 128 bytes do cache L2. Dump arquivado e decodificado com o ELF exato; a correção usa a configuração L2 do SDK. Esse ensaio abortado não produziu dados válidos de desempenho.

`tools/compare_gba_backends.py` alterna a ordem dos dois backends no mesmo firmware e reinicializa o núcleo a cada execução. Confere o backend efetivamente ativo, mantém áudio/apresentação normais e registra carga, frames, run, CPU, update de hardware, PPU, tradução JIT, páginas de ROM e leituras de SD. Os tempos internos são subconjuntos e podem se sobrepor; não devem ser somados como fases disjuntas. A primeira janela de aquecimento é separada. Reiniciar o núcleo não esvazia fisicamente o cache do cartão.

Duas ROMs próprias 0BSD: a cena leve histórica e `tests/homebrew/gba_bench`, que executa uma carga ALU de 4096 iterações e modifica código ARM/Thumb em RAM. A SRAM contém assinatura, iterações, checksum ALU esperado 4368456 e máscara de falhas de código modificado (zero esperado). O teste recusa publicar sucesso quando correção ou progresso falha. Nenhuma dessas cenas substitui testes comerciais, de PPU complexa ou paginação de uma ROM grande.

O primeiro candidato que passou na cena leve falhou no ensaio SMC: máscara `0x0a`, indicando código antigo após alterar as instruções ARM e Thumb. Os stubs de memória reutilizados chamavam stores C do interpretador, que não consultam tags de tradução. O adaptador RV32 passou a conferir tags de RAM (incluindo byte/halfword/word, espelhos e alinhamento), emitir `CPU_ALERT_SMC` e voltar ao lookup do próximo PC após invalidar o cache. O ensaio abortado não certifica o backend; o relatório final identifica o binário retestado.

### Critério temporal do candidato

Após corrigir o SMC, ambos os modos produzem o checksum e a máscara de falhas esperados. Isso ainda não demonstra equivalência de tempo: no ensaio curto, o interpretador concluiu aproximadamente uma iteração da carga a cada três quadros emulados; RV32, uma a cada dois. A contagem é feita pelo próprio guest, persistida na SRAM e normalizada pelos quadros do núcleo. Os dois manterem ~59,73 quadros/s não elimina essa divergência. Sem uma referência do hardware original, o teste não certifica qual temporização é exata.

Os stubs genéricos RV32 ainda não debitam todas as esperas de barramento dependentes da região; o tradutor usa custos aproximados e uma tabela de fetch padrão. O interpretador atual consulta `ws_cyc_nseq/ws_cyc_seq` dinamicamente. Esse é um ponto concreto para a próxima adaptação, incluindo mudanças de WAITCNT e acessos sequenciais. É preciso verificar também ramos, multiplicação, exceções e estados, com uma suíte de temporização e as ROMs fornecidas. Não se corrige isso adicionando um multiplicador arbitrário para igualar o benchmark.

Assim, tempos nativos menores por quadro são resultados brutos de cargas temporalmente diferentes, não um ganho certificado para jogos. O stable mantém o interpretador; RV32 permanece opt-in e experimental. O relatório de comparação publica `guest_timing_check` e `production_approved=false`, mesmo quando os ensaios de ALU/SMC passam.

A comparação alterna backends dentro da mesma imagem experimental. Mesmo ao selecionar o interpretador, essa imagem contém `HAVE_DYNAREC`, reserva o cache JIT e inicializa stubs/BIOS do tradutor. Portanto, ela controla layout/configuração entre os backends, mas não é uma comparação binária entre o pacote stable e o experimental. Esse custo de inicialização aparece na carga medida, separado da janela contínua.

## Comparação física — candidato experimental 0.1.15

Imagem experimental SHA-256 `09db172fd33fc71f861f4ff1ee092de2f7bcd5a14c6a4ad836a5d79adb30cedb`, ELF `25d8e5effabfadd3df5e233be22acfc2be91ffd64b7f71b66a8845f6c5277ba5`. P4 rev.1.3, 360 MHz, PSRAM 32 MiB/200 MHz, ESP-IDF 6.1. Duas cenas próprias, duas repetições de 20 s por backend, ordem alternada; sem entrada ou screenshot durante cada janela. Dados numéricos por amostra: `artifacts/gba-rv32-v0.1.15-comparison.json`.

| Cena | Backend | Frames emulados/s | Run médio por quadro, ms |
|---|---|---:|---:|
| Leve | Interpretador | 59,760 | 14,384 |
| Leve | RV32 | 59,739 | 9,249 |
| ALU + SMC | Interpretador | 59,739 | 14,086 |
| ALU + SMC | RV32 | 59,728 | 7,740 |

A tabela usa a mediana das duas médias por execução, não a mediana de tempos individuais. A quantização dos contadores explica pequenos desvios da frequência nominal. Todas as oito janelas tiveram zero amostras faltantes/erros de escrita no sink observado. Os quatro ensaios ALU/SMC tiveram checksum 4368456 e máscara de falhas zero.

**Decisão: conservar o interpretador como padrão.** No guest da cena ALU/SMC, foram 0,332805 iterações por quadro no interpretador e 0,499604 no RV32: razão **1,501191**. Os tempos nativos da tabela correspondem a trabalho temporalmente diferente; não são um speedup certificado. O teste não é um oráculo do console original. Temporização de barramento/WAITCNT, conformidade e jogos representativos permanecem necessários antes de promover o backend. Não houve substituição do núcleo stable por mGBA nem validação comercial RV32.

A correção posterior da leitura do índice/retomada do SD recompilou a GUI e o catálogo. O ELF do ensaio acima permanece identificado: um binário posterior não herda automaticamente essa validação. A distribuição stable e o candidato RV32 compilado mais recente têm registros de verificação separados.


## Cenas comerciais — desenvolvimento 0.1.16

Os ensaios novos usam as ROMs já fornecidas no SD, volume de 5%, sem overclock. O ELF `ed7911535bfcade16213e04a7a866aaac25b1dba5a2214ea6e3c6eef103ae33c` mantém o interpretador, L2 de 256 KiB, IWRAM em RAM interna quando há margem e renderização adaptativa. O resultado depende da cena: Aria no pátio inicial do candidato anterior ficou perto de 59,7 quadros emulados/s, mas dentro do castelo na compilação atual caiu para 47,49. Harmony na primeira área jogável ficou em 59,69. GP Legend na corrida real ficou em 30,47. Essas janelas de 20 s não são A/B controlado nem uma campanha completa. As quedas de Aria/GP ainda causam faltas de áudio.

Para investigar o próximo backend sem assumir o interpretador como oráculo de hardware, `gba_timing` compila 19 casos/20 modos a partir da suíte aberta mGBA (MIT, revisão fixada junto aos fontes). Exporta o Timer0 da CPU guest por SRAM, com baseline, observado e diferença. Valores esperados vêm da suíte; nenhum byte é injetado em RAM pelo host. O probe recusa firmware/template inesperados, substitui apenas a ROM T5RV própria e restaura seus bytes com releitura, mantendo o catálogo. Compilação/execução não equivalem a conformidade; mismatches ficam explícitos e retornam erro.

### Referência independente e resolução do timer

No stable identificado acima, o ensaio de Timer0 completou, mas os casos curtos quase sempre leem zero ou um salto do scheduler. A atualização de `io_registers` ocorre em `update_gba`, fora de cada instrução. Portanto, os 376 mismatches de 380 comparações não demonstram 376 falhas independentes da CPU. O probe publica `timer_reads_usable=false` e não certifica conformidade. No mGBA host fixado em `feeea8f5c2f53f3fc10e534b54c1b4f7366a42f2`, a mesma ROM obteve 364/380 resultados iguais aos valores da suíte; as 16 diferenças também ficam explícitas.

A carga longa ALU/SMC evita depender de leituras de timer tão próximas. `tools/gba_reference.c` é um adaptador host 0BSD para a biblioteca mGBA, sem injetar valores na RAM do guest. Com a ROM T5RV SHA `c0191014009e2f48412888a6cd99a6a76e1019740ac0a2be0f5c78df013c726a`, foram 398 iterações em 1200 quadros, checksum 4368456 e máscara SMC zero. O stable físico concluiu 239 em 719 quadros (0,332406/frame), próximo dessa referência (0,331667/frame), enquanto o RV32 anterior ficou próximo de 0,500. É uma comparação de correção agregada de uma carga, não uma medição de desempenho do mGBA no Tab5 nem conformidade de todos os opcodes.

Para reproduzir, compile o adaptador contra os headers e a biblioteca da revisão mGBA indicada, com `ENABLE_VFS`, `ENABLE_VFS_FD` e `ENABLE_DIRECTORIES` iguais ao build da biblioteca. Execute `gba-reference artifacts/test-roms/tab5-rv32-bench.gba OUTPUT_SRAM 1200`; o destino deve estar ausente. Decodifique assinatura e três palavras little-endian nos offsets 0/4/8/12. O clone e o build host permanecem em `.tools`, sem modificar SDKs dos outros projetos nem incorporar mGBA ao firmware.

### Candidato com esperas de dados

No ELF experimental `386c27175f42126e4d3125b56e245f58d48b58fafa3e4e00bd61807169977925`, os stubs debitam `ws_cyc_nseq` em tempo de execução pela região/endereço e largura, em vez de presumir sempre o barramento IWRAM. O arquivo contíguo de registradores do assembly também é mantido na RAM interna. A comparação pareada na mesma imagem produziu 0,332056 iterações/quadro no interpretador e 0,333333 no RV32 (razão 1,003846), com checksum e SMC corretos. Não se acrescentou um multiplicador de ciclos para ajustar esse resultado.

Nas mesmas cenas/estados fornecidos, janelas de 20 s: Aria no castelo, 47,50→59,73 quadros emulados/s; GP Legend em corrida, 30,47→59,69. Aria teve 240 amostras faltantes no RV32 contra 196536 no interpretador; GP teve zero contra 467385. São resultados dessas cenas, não aprovação de toda a temporização: fetch/prefetch, transferências múltiplas e uma suíte CPU mais ampla continuam pendentes.

Esse candidato reiniciou no startup de IRQ de Circle of the Moon, reproduzido após boot limpo. O dump foi arquivado e decodificado com o ELF exato: salto nativo inválido após uma saída indireta do tradutor. O diagnóstico não estabelece ainda qual instrução guest causou a corrupção. A adaptação seguinte força o interpretador somente para o SHA-256 verificado dessa ROM, inclusive quando renomeada, mantendo os demais títulos com a seleção solicitada. Não declara o problema interno do RV32 corrigido. A métrica `backend` informa o núcleo efetivamente usado.

`gba-rv32` continua como imagem de diagnóstico sem SNES, na reserva original. O novo perfil `stable-rv32` inclui todos os núcleos do stable e a opção RV32/fallback, com `bash tools/build.sh stable-rv32`. Sua instalação exige uma reserva maior e aprovação explícita do layout; compilar esse perfil não significa tê-lo instalado ou testado como imagem combinada.

### Retomada em 24/11/2026: corrupção de heap entre jogos

**Pendência conhecida; investigação suspensa nesta etapa por orientação do usuário.** O desenvolvimento segue no stable com interpretador e JIT excluído. Os fontes experimentais, testes, dumps e diagnósticos são preservados; não se modifica seu gerenciamento de memória.

O candidato de sincronização de cache ELF `2db1bcbf704a8339b46ad8d51b01db9e6b7dc26def2d08524577ecfa93dc28ef` passou dez ciclos curtos do homebrew, alternando interpretador/RV32. A sequência comercial posterior terminou com reinício ao sair do homebrew ALU/SMC. Os quatro intervalos comerciais anteriores a essa falha não aprovam o ciclo de vida completo. O dump arquivado tem 29408 bytes e SHA `f4775600ca2f31bb6046cbea8e6f4c255c89526ccd4219d381d208818aec7996`; decodificado com o ELF exato, mostra corrupção do alocador TLSF durante uma alocação na persistência de bateria, antes do desmapeamento do JIT.

Na retomada, o homebrew isolado executou 719 quadros e voltou à biblioteca. Repetir Circle → Aria → GP Legend → Harmony com seus estados preservados e depois o homebrew reproduziu o reinício na saída. O segundo dump (29568 bytes, SHA `30580bcf2860100e8eac21f2f101c0ed8d6fe9feb975a7205746b0ffd00d1f2a`) aponta uma liberação com cabeçalho inválido. Esses pontos são onde a corrupção foi detectada, não sua causa estabelecida. A sincronização de cache anterior permanece insuficientemente validada.

`tools/probe_gba_lifecycle.py` registra a sequência e o último status antes de `quit`, exige o ELF completo e hashes de ROM/estados, confere ALU/SMC, retorno à biblioteca e coredump, e preserva o erro principal se a limpeza USB também falhar. Aceita metadados locais de cenas por `--scene`; eles não são distribuídos. Recusa um dump preexistente, que deve ser arquivado e conferido antes de apagar no aparelho.

Para localizar a primeira corrupção, há a opção CMake `TAB5_GBA_HEAP_CHECK=ON`: verifica integridade após inicialização/carga/estado, a cada quadro e antes da persistência/saída. É desativada por padrão e seus tempos não servem como benchmark. Compilar com `TAB5_GBA_HEAP_CHECK=ON bash tools/build.sh gba-rv32`. Sem essa variável, o script restaura explicitamente `OFF`, inclusive se o diretório de build estava configurado para diagnóstico. Em chamadas diretas a `idf.py`, desativar com `-D TAB5_GBA_HEAP_CHECK=OFF`.

O diagnóstico ELF `109e3c6c1665f093654360bc0631bceecc2ad908a8406d9ca9b4be40b4655902` detectou heap inválido na checagem após `retro_run` do homebrew, depois das quatro cenas. Dump de 28928 bytes, SHA `57f27708396714b3443d7d54766499989c99fa2e3891d98d9baa93fc779e9596`. Duas execuções isoladas posteriores do homebrew passaram; isso não resolve a falha entre jogos nem aprova o JIT. Nenhuma conclusão de causa raiz foi estabelecida.
