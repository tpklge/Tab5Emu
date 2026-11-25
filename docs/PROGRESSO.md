# Progresso — 24/11/2026

## Candidato 0.2.2-sd — gráficos Daedalus e início de áudio GBA

Implementado o pipeline gráfico HLE Daedalus com rasterizador nativo em software,
no módulo N64 do SD. CPU/dispositivos Mupen e áudio CXD4 são preservados; o nome
exato do backend e contadores HLE constam no diagnóstico. Não é um porte da CPU
Daedalus. GB/GBC/GBA/SNES/MD continuam no aplicativo unificado. GBA conserva o
interpretador, sem JIT; seu gerenciador RV32 e os dumps/testes de heap não foram
modificados. Detalhes e limitações em N64_PORT.md.

GBA acumula 50 ms de PCM antes do primeiro envio, reiniciando na mudança de linha
do tempo, sem alterar FPS ou descartar amostras da introdução. Os primeiros seis
blocos I2S agora têm contadores próprios, além dos contadores contínuos mantidos.
A melhora acústica ainda não foi medida no aparelho deste build.

Host: 71 testes Python e suítes estáveis, preroll sem perda de amostras,
primitivas/listas F3D Daedalus e console LLE passam com ASan/UBSan. Com a ROM do
usuário, o caminho HLE executou 966906293 instruções CPU, 2526 tarefas gráficas,
1386387 triângulos e produziu a cena externa do castelo com diálogo inicial
SM64; houve entrada Start/A/analógico automatizada. O teste HLE do console
confirmou despacho OSTask e SP/DP pelo barramento real. MK64 mostrou o título e
Ocarina a introdução 3D a cavalo no host, também com sanitizadores ativos. Não mede velocidade do P4,
nem certifica compatibilidade geral. ROM/capturas permanecem locais.

Candidato: imagem 4047216 bytes, reserva arredondada 4063232; SHA
`5dc96802979a68d235cc9812127b259d07c4a739037659e11109cb2e71f88856`, ELF
`b2000f29e1ef4438f678b2fbe67b2c6c133440672cbb9bb7ce577012114e3996`.
Os cinco módulos conferem com os descritores do ELF; parser canônico aceitou a
imagem e recusou dados extras. Relatório: `artifacts/daedalus-candidate-validation.json`.

**Instalado pelo Launcher, app0:** o usuário confirmou que atualizou o Launcher
e autorizou a gravação. Com catálogo vazio, o próprio Launcher alocou app0 e
retomou a instalação após reiniciar. GET_INFO confirmou o ELF acima; imagem e
cinco módulos foram relidos por SHA. Não houve gravação bruta por esptool ou uso
forçado do antigo app4. Jogos e gerações antigas de módulos foram preservados.

No aparelho, os seis sistemas passaram por 13 ciclos, recusa/recuperação de
módulo truncado, A e EEPROM N64 11→12 com backup. SM64 chegou à seleção de
arquivo por Start: título 4,10 VI/s e rosto 1,88, contra 2,19/1,16 na 0.2.1-sd,
ambas a 360 MHz. Ainda não oferece velocidade jogável e o áudio N64 tem muitas
faltas. O tom GBA próprio manteve 59,77 VI/s sem faltas no intervalo contínuo;
a introdução Circle caiu para 48,81 VI/s e ainda teve lacunas de áudio. O buffer
inicial não resolve esse déficit sustentado. Não se declara o som geral corrigido.
Configurações restauradas, retorno à biblioteca e coredump zero. Evidência:
`artifacts/daedalus-hardware-verification-v0.2.2.json`.


O pedido original está preservado em `prompt_codex_tab5_emu_paludo_n64.md`. O desenvolvimento é incremental: o marco 0.1.15 acrescenta compatibilidade, biblioteca paginada e editor; o stable instalado tem evidências específicas descritas abaixo. N64 continua experimental, no ensaio real de CPU descrito abaixo. As pendências não são declaradas concluídas.

## Continuação do stable — compatibilidade e saves antigos

Implementada EEPROM serial MD: Sega/EA X24C01 (128 bytes) e Acclaim 16M X24C02 (256 bytes), identificadas pelo produto, com prioridade para patches SRAM. O protocolo cobre START/STOP/repeated START, ACK/NACK, página de quatro bytes e leitura com wrap. Reutiliza o buffer existente do cartucho e BatteryStore; o estado v4 inclui a transação em andamento. SRAM linear mantém estados v1/v2 e Tekken mantém v3. Acclaim 32M/J-Cart e outros chips/mappers continuam pendentes; não se simula o tempo elétrico de programação.

`tools/migrate_saves.py` importa uma cópia por ROM explícita, com simulação, validação de geometria/esquema/journal, escrita exclusiva, conferência, procedência e recusa de destinos ocupados. Cobre baterias GB/GBC/GBA/SNES/MD, RTC Tab5 v1/v2, estados gnuboy/gpSP compatíveis e contêineres MD. Raw Gwenesis exige registros iguais ao template atual e bateria antiga correspondente; dados do template não substituem a SRAM histórica. Raw SNES dependente de build e raw MD sem dados de EEPROM/Tekken são recusados. Não há associação automática por basename, conversão universal ou migração de miniaturas. Instruções em SAVES_ANTIGOS.md.

Host: **63 testes Python, doze suítes C++ e duas C**, com ASan/UBSan. Além do protocolo isolado, três ROMs próprias 0BSD executam instruções no M68000 real e preservam EEPROM em nova inicialização. A importação é testada contra corrupção, offsets/tamanhos inválidos, colisões, symlinks, sidecars, versões de RTC e erro de sync após gravar o payload, preservando a origem.

Stable instalado: **4028784 bytes**, reserva app4 **4063232**, margem **34448**; SHA `492f9dba14830b085e93d6aaab1de85c0f5f753d510ef13ecace9e0572368ad2`, ELF `884954de93057637d15b8101bf3680870ede738b12711e3f11dbe6b8d6833ade`. GBA permanece no interpretador, RV32/JIT desativado. O gerenciamento de memória experimental, seus testes, dumps e diagnósticos não foram alterados; corrupção de heap continua pendência conhecida.

Nesse ELF, as três EEPROMs passaram no aparelho: leitura/gravação pelo guest, Start, novo boot, bateria importada, backup, estado v4 e recusa de SHA/placa/contador serial inválidos. GB/GBC importaram bateria e estado por basename; MD converteu raw de 146966 bytes para contêiner de 155270 com SRAM antiga correspondente. Carregar os estados restaurou a linha do tempo anterior, preservando os arquivos de origem. Catálogos/índices foram restaurados. São fixtures próprias, sem certificação de jogos comerciais. Relatórios completos em `physical-md-eeprom-v0.1.16-complete.json` e `physical-save-migration-v0.1.16-complete.json`.

GBA repetiu entrada/estado/SRAM/backup, recusa de JIT e produtor parado na pausa. Tom próprio: 1203 blocos em 20,035 s, zero parciais/faltas/erros, maior intervalo após retomar de 21997 us. A janela não certifica áudio geral ou alto-falante. HOME → Launcher → stable confirmou o mesmo ELF, recursos e coredump zero, com uma ordem explícita adicional de boot antes da reenumeração USB.

Histórico preservado: o Launcher concluiu backup/remoção do app4, mas a primeira instalação retornou `ESP_ERR_INVALID_STATE`. Após inspecionar slot vazio, apps/layout intactos e reler o SHA do arquivo SD, um novo plano instalou somente na mesma reserva. A identidade foi confirmada após boot; nenhuma tabela/bootloader/rádio foi instalada. Tentativas iniciais dos roteiros falharam por cadastro de fixture, helpers que exigiam índice, corrupção que não mudava o byte `0xff` e título de homebrew maior que o campo; os relatórios `complete: false` foram mantidos e os roteiros corrigidos. Não são falhas ocultas nem resultados atribuídos a outro ELF. A evidência consolidada é `artifacts/hardware-verification-v0.1.16-save-compat.json`.

## Marco anterior do stable — pausa, controles, catálogo, RTC e áudio

Escopo confirmado: pendências do stable. N64 fica fora desta etapa, GBA permanece no interpretador e RV32/JIT desativado. O gerenciamento de memória experimental, seus testes, dumps e diagnósticos foram preservados.

Implementados: captura USB da pausa/editor na tarefa principal, com cópia independente e falha de alocação recuperável; editor com grupo de keypad e navegação por Tab/Shift+Tab, setas, Enter e Esc; normalização `genesis` → `megadrive` nos perfis; `CATALOG_QUERY` USB 6 no worker, paginação do catálogo completo e launch fora da página; RTC MBC3 monotônico na pausa, respeitando halt e a linha do tempo dos estados; validação estrita de temas e avisos de recursos SD ausentes/inválidos. A sincronização prévia pela fila principal impede recusar um novo launch durante a curta retomada da HOME após uma ROM inválida. Operações USB que alteram o jogo exigem retomada; captura, diagnóstico e ações do menu continuam disponíveis na pausa.

Áudio: espera limitada a 16 ms para completar um bloco em fluxo, abaixo de seu intervalo de 1/60 s; DMA permanece 16×240 frames. `stage_audio_policy.py` fixa o SHA e gera o mesmo feeder para firmware e testes, sem editar managed components. O teste usa o feeder real com relógio/stream FreeRTOS determinístico: chegada em 12 ms, notificações antigas espaçadas sem renovar o prazo total e silêncio limitado para fluxo atrasado/parado.

Host: **48 testes Python, dez suítes C++ e duas C** passaram com ASan/UBSan. Incluem envelope/paginação USB, seleção exata no índice, aliases MD, bordas do teclado, temas inválidos, RTC fracionário/halt e feeder real. A CI continua sem execução remota; não houve push.

Stable instalado: **4024816 bytes**, reserva app4 **4063232**, margem **38416**; SHA `e54403bacf87022e2509984d41bb5210d93cf1e466bcdc4419bc3156ebf950af`, ELF `21775c87b0823ba885f540ba17b7dc76a3edd7ba0ec927a6ab9c1df4e07abf05`. O Launcher verificou envio/instalação e preservou apps/layout. Os candidatos intermediários não recebem automaticamente os resultados deste binário.

Nesse ELF, o tom GBA próprio produziu 1201 blocos em 20,021 s com zero blocos parciais, frames de silêncio ou erros de escrita; passou a asserção estrita dessa janela. O produtor parou na pausa e o maior intervalo de submissão após retomar foi 22307 us, excluindo a pausa. Entrada, estado, SRAM e backup GBA passaram; JIT foi recusado. Isso não é avaliação acústica ou certificação geral da APU. Os candidatos anteriores com espera de 8 ms tiveram 141 e 228 frames de silêncio em janelas de 20 s; ambos os resultados e as falhas da asserção de zero foram preservados, com seus ELFs.

No ELF final passaram RTC na pausa/carga de estado, captura e cancelamento do editor sem alterar perfis, tema de versão errada com restauração, consultas de um catálogo de 5000 entradas e launch fora da página 78. GB/GBC/SNES repetiram estados/SRAM/backup; RTC/MBC2 e MD/SMD/seis botões passaram; a fixture M68000 repetiu restauração CPU/vídeo/SRAM, reabertura/backup e quatro recusas de estado inválido. As cinco recusas consecutivas de ROM GB/GBC confirmaram a sincronização da HOME. Falhas de save/estado/saída preservaram os dados, e as capturas da faixa foram inspecionadas. Catálogo, tema, perfis e arquivos descartáveis foram restaurados/conferidos. A evidência `artifacts/hardware-verification-v0.1.16-stable-rest.json` identifica o ELF e os resultados finais. A distribuição local `0.1.16-dev.3` contém fontes exatas com `dirty: true`, preservando as alterações anteriores do workspace.

Histórico preservado: o primeiro ensaio de recursos tentou um setter USB de tema inexistente e foi corrigido para substituir/restaurar apenas o arquivo ativo; no candidato `0a1de4ab…`, uma sequência de recusas encontrou a corrida de retomada da HOME, corrigida pela fila principal; a conferência de boot via console exigiu uma nova ordem explícita no Launcher antes de reenumerar o emulador, com o mesmo ELF e coredump vazio. ACK da console não é apresentado como prova de boot: os roteiros conferem a identidade após a USB voltar.

HOME → Launcher → stable confirmou USB nativa antes da console, o ELF final e coredump vazio. Houve uma ordem explícita adicional de boot pela console antes de o emulador reenumerar; isso consta no relatório, sem tratar o primeiro ACK como boot confirmado. Apps/layout foram comparados na instalação. Ao terminar a regressão MD, a HOME continuou no mesmo ELF com coredump zero.

Nesse marco anterior permaneciam EEPROM serial MD e migração de estados, tratadas na continuação acima. Continuam os limites de compatibilidade, mappers adicionais e ensaios manuais A164/pads, remoção física do SD, energia e avaliação acústica. A navegação por botões emulink não certifica teclado ou dois pads físicos. Nenhum desses ensaios é declarado concluído por haver implementação de frontend.

## Prioridades altas — recuperação de falhas e CI stable

Implementados: inicialização GB/GBC com retorno de erro, rollback das oito alocações compartilhadas e recusa de falta de SRAM antes de acessar o buffer; leituras stdio verificadas para ROM, estados GBA/SNES/MD, catálogo JSON, preferências, perfis, fontes e temas; confirmação/erro no menu de pausa e `operation_error` no status USB. Falha ao persistir o save cancela Sair/Launcher e permite nova tentativa, preservando os dados anteriores. O loader gnuboy é adaptado no build com SHA fixado; o submódulo permanece intacto.

A CI foi substituída pelo fluxo Paludo app-only com SDK fixado, testes host, verificação de tamanho/perfil/JIT e pacote de app/SD/fontes. Não publica firmware como testado fisicamente nem instala no aparelho. Build, comandos de teste/empacotamento e sintaxe YAML foram verificados localmente; não houve push nem execução remota do GitHub Actions.

Host: **42 testes Python, nove suítes C++ e duas suítes C** passaram com ASan/UBSan. Os casos novos injetam falhas de leitura/seek/EOF/close/alocação e de cada alocação GB, verificando rollback, nova tentativa, proteção de outro dono do pool e SRAM máxima de 128 KiB. A falta de memória GB foi exercitada no host; não se força esgotamento de heap no aparelho.

O stable tem **4007840 bytes**, margem de 55392 na reserva app4 de 4063232 bytes, SHA `c447f24b16f58063f2e6d8783e111bc1ced11587d8566b7fcebf5f2956d9b79f`, ELF `10a794d128a4a34e2dbcedb3e367332454c907d6bf8d39f7c224333b45e13e54`. Foi instalado pelo Launcher com SHA recebido conferido e os outros apps/layout preservados. Nesse ELF passaram falha de gravação de estado sem perder o anterior, estado truncado recusado com jogo continuando, recuperação por estado válido, recusa de saída/Launcher quando o journal do save não pode ser gravado e persistência após liberar o impedimento. As mensagens foram conferidas pelo status USB com a pausa aberta; não se declara inspeção visual da faixa. O primeiro ensaio tentou uma captura que a API recusa durante a pausa; seu relatório foi preservado e a ferramenta corrigida passou, removendo somente seus arquivos descartáveis.

GB/GBC/SNES repetiram estados, SRAM, reabertura e backup; as cinco recusas de ROM GB/GBC preservaram/restauraram a fixture. GBA repetiu entrada, estado, SRAM e backup no interpretador, recusando `gba_cpu=1`. RTC/avanço desligado, MBC2 e MD/SMD/seis botões também passaram. Evidência sanitizada e específica deste binário: `artifacts/hardware-verification-v0.1.16-high-priority.json`. A corrupção de heap RV32 permanece pendência conhecida; testes, dumps e diagnósticos foram preservados, sem alteração de seu gerenciamento de memória nesta etapa.

HOME → Launcher → stable confirmou USB nativa antes da console, mesmo ELF e coredump vazio. O Wi-Fi do Launcher não ficou pronto nessa última volta após uma tentativa de reconexão; o boot foi concluído pela console USB. Apps/layout foram comparados na instalação, sem nova consulta Web nessa volta. Não houve alteração de rádio ou credenciais. A distribuição local `0.1.16-dev.2` inclui fontes/CI e manifesto de bytes com `dirty: true`, mantendo as alterações de desenvolvimento sem atribuí-las ao commit base.

## Retomada stable — interface, controles, áudio e compatibilidade

Por orientação do usuário, o interpretador GBA permanece padrão e o RV32/JIT fica desativado. A investigação de corrupção de heap está registrada em GBA_PERFORMANCE.md, com testes, dumps e diagnósticos preservados. Não se altera o gerenciamento de memória desse backend nesta etapa. O perfil stable força a exclusão do tradutor mesmo com uma opção antiga no cache CMake; sua API permite somente `gba_cpu=0`.

Implementados para a próxima compilação: validação GB/GBC antes do loader (cabeçalho/bancos truncados, mapper e geometria não suportados); motivo da falha na HOME e no status USB; agregação independente dos gamepads USB; descarte de PCM/interpolação ao pausar, carregar estado ou sair; reinício do filtro/PCM na carga verificada de estados GB/GBC. O descarte de áudio conserva os contadores da sessão. ROMs/saves inválidos permanecem no SD para diagnóstico.

Host: 36 testes Python, sete suítes C++ e duas suítes C passaram com ASan/UBSan. Os casos novos incluem limites de cabeçalhos/bancos GB, mappers recusados, relatórios neutros/remoção de controles e eixos de −32768. O stable desta retomada tem 4022640 bytes, SHA `51fc4bb9260b1c3838fbb465957dfd3a1df4f1961bd2afbff4da4631bd0a2356`, ELF `0c532fe57d9a0cad282504fc0b7c173792880c3d9571dc3d7379653cc96f59cd`. Foi instalado em app4 com a reserva de 4063232 bytes, os demais apps e layout preservados. GB/GBC/SNES repetiram estados, SRAM, novo boot e backup. As cinco recusas de ROM GB/GBC passaram, com mensagem na HOME e restauração da fixture por releitura. GBA repetiu entrada/estado/bateria/backup no interpretador; `gba_cpu=1` foi recusado. No tom próprio de ~440 Hz, 20,024 s/1201 blocos tiveram zero faltas ou erros no sink; a pausa de dois segundos não produziu PCM nem entrou no maior intervalo de submissão após retomar (22515 us). Esses números não são avaliação acústica. Os testes físicos de dois gamepads USB continuam pendentes.

RTC/MBC2 e MD/SMD/seis botões passaram neste ELF. O ensaio RTC antigo exigia dia zero e falhou porque a fixture persistida já estava no dia 1; o relatório da tentativa foi preservado. O teste corrigido confere registros latched contra o relógio salvo, carry e avanço entre reaberturas sem zerar os dados: observou quatro segundos de avanço desligado. A falha da asserção antiga não é apresentada como defeito do relógio emulado.

Retorno HOME → Launcher → stable confirmou USB nativa antes da console, apps/layout iguais, mesmo ELF, interpretador selecionado e coredump vazio. Evidência sanitizada: `artifacts/hardware-verification-v0.1.16-stable-followup.json`. O pacote de desenvolvimento usa snapshot com arquivos/hash próprios e `dirty: true`, preservando as alterações locais sem atribuí-las ao conteúdo do commit base.

## Desenvolvimento 0.1.16 — correções por jogo

O candidato stable sem rastreamento e sem o experimento LTO tem 4017440 bytes, SHA `f52f1ed52346bb6f73843eaab33c59703f82e10011c663ce4dce8e4822655e72`, ELF `ed7911535bfcade16213e04a7a866aaac25b1dba5a2214ea6e3c6eef103ae33c`. A instalação pelo Launcher terminou, preservando a reserva app4 e os outros apps. A regressão específica desse ELF está em andamento. [CORRECOES_0_1_16](CORRECOES_0_1_16.md) registra implementação, testes, tentativas descartadas e pendências sem confundir contagem de quadros com progresso do jogo.

Host: 26 testes Python, sete suítes C++ e duas suítes C passaram com ASan/UBSan. O M68000 real executou código e acessou dados pelos 32 espelhos da RAM, incluindo longwords alinhados a 16 bits e wrap em 64 KiB. O SPC real passou no relógio de um segundo, estado, alocação interna/fallback/falha e ciclo de vida.

## Situação do desenvolvimento 0.1.15

Implementados: MBC2, RTC MBC3 com carry/halt/avanço desligado, SMD/seis botões, estado MD v2 compatível com v1, índice de até 100000 registros/páginas de 64 e editor Global/Sistema/Jogo. Host: **24 testes Python e sete suítes C++ ASan/UBSan** passaram, incluindo falha de leitura no parser real do índice e concorrência no fechamento do acesso MSC.

O stable instalado tem **4040032 bytes**, SHA `e70e0cf76ade9e3e93021beebe344fc71ffca889c4438b9235f31e1b4b96f650`, ELF `360a8d438ebfe5eb0bfc82ee7d3084b17d87a57bffd747ee3e37d073c5456c64`. Mantém os cinco sistemas, a reserva app4 e margem de 23200 bytes. O Launcher conferiu envio/releitura/instalação; o boot confirmou esse ELF. Os demais apps e reservas foram comparados antes/depois e preservados.

O catálogo de **5000 entradas** passou nesse stable em paginação, última página de oito, busca sem acento, favoritos (inclusive lista vazia), recentes, cancelamento para launch e CRC inválido preservado. A ejeção segura pelo macOS precisou de duas tentativas; o firmware retomou o índice sem erro e restaurou o catálogo original. A API recusou arquivos/launch durante ownership do host; `msc_io` confirmou acesso fechado no firmware, aberto somente durante o drive e fechado novamente após a devolução. Relatório: `artifacts/large-library-v0.1.15.json`.

Duas falhas foram corrigidas durante esse desenvolvimento. `std::ifstream` pode abortar o SDK sem exceções quando a leitura física falha: o parser agora usa stdio, distingue I/O de corrupção, tenta uma leitura adicional após remount e preserva o modelo na falha. O dump de 30432 bytes foi arquivado com o ELF correto. Depois, logs revelaram callbacks MSC READ10 atrasados alcançando o SD após a devolução ao firmware. O gate local intercepta READ10/WRITE10/Ready, espera callbacks em andamento e drena o driver antes de montar FAT. Recusas usam SCSI No Medium e têm contadores; nenhum componente gerenciado foi editado.

No candidato experimental SHA `09db172fd33fc71f861f4ff1ee092de2f7bcd5a14c6a4ad836a5d79adb30cedb`, ELF `25d8e5effabfadd3df5e233be22acfc2be91ffd64b7f71b66a8845f6c5277ba5`, a comparação GBA completou oito janelas de 20 s com ALU/SMC corretos e zero faltas/erros no sink observado. A cena ALU/SMC produziu razão temporal RV32/interpretador **1,501191** em iterações do guest por quadro. O interpretador permanece padrão; tempos nativos menores não certificam otimização correta. O candidato RV32 continua experimental e sem SNES. A imagem final recompilada tem 4027776 bytes, SHA `5433c6ca4e7b786e42734b5c66972b78b46a2ef4fc567b3a17edd8b01a06b507`; é build-only, sem herdar os ensaios do candidato comparado. Relatório: `artifacts/gba-rv32-v0.1.15-comparison.json`.

O editor passou no candidato `25d8e5ef` em aplicar por sistema/jogo real SHA, recusar conflito, cancelar, restaurar camadas e bloquear a tela subjacente (`artifacts/editor-v0.1.15.json`). No stable final, Aplicar/Restaurar Global e a preservação dos campos legados/desconhecidos também passaram (`artifacts/editor-global-v0.1.15.json`). As 12 reaberturas RTC/MBC2 não tiveram erro de armazenamento nos logs CDC e mantiveram coredump vazio (`artifacts/sd-reopen-v0.1.15.json`). RTC/avanço desligado, MBC2, MD/SMD/pad6 e preservação do modo do pad em estados foram repetidos no stable (`artifacts/compatibility-v0.1.15.json`). Os cinco sistemas repetiram estado/SRAM/backup, e o updater legado foi recusado na HOME e no GBA. Os relatórios identificam explicitamente cada ELF.

## Aparelho e entrega

Stable **0.1.14 foi o marco anterior**: 4026928 bytes, SHA-256 `9c9ffdb457dccb96145a26283084f9769d44cbab1603bf97b762f35c5e36eb5d`, ELF completo `eea38372e6a1fa6e0b722e3d476d9d346c7210fd2b44f766b11504f178c454a2`. Reserva 4063232 bytes; margem 36304. P4 revisão 1.3, 360 MHz, flash 16 MiB/DIO/80 MHz, PSRAM 32 MiB/200 MHz, painel ST7123 1280×720, SD FAT32.

O usuário informou 9 MiB livres; a leitura da tabela no backup confirmou o valor, apesar de um cálculo legado incorreto da API. O Launcher registrou app4 em 0x6F0000–0xAD0000 após autorização explícita e backup integral de 16 MiB verificado. Meshtastic, duas versões de Gotchi e TabChat não foram movidos. Atualizações posteriores exigem a mesma reserva e os mesmos descritores dos outros apps; nenhum bootloader, tabela, rádio ou NVS foi gravado pelo projeto.

Base `d12051778528af718bbc20142c0592b7dbaff96e`; branch `paludo/implementation`. SDK 6.1 isolado, commit `fff9895c82d744c7237be8847347bdd1b07c6643`; o SDK dos outros projetos permanece separado. Baseline integral original: 9896816 bytes; não foi instalado. O build e a entrega normais são app-only.

## Matriz de aceite

| Requisito | Componente/etapa | Evidência e estado |
|---|---|---|
| Auditoria, SDK e hardware | build/BSP | compilação baseline e identificação física concluídas |
| App4, backup e preservação | paludo.py | envio/releitura/instalação/boot físicos; reserva e outros apps conferidos |
| Updater legado recusado | remote_control | 0.1.14 remove escrita de otadata; comando recusado sem reset na HOME e no jogo |
| Retorno pela HOME e pausa | paludo_platform, Cart/Menu | HOME 0.1.15 intermediária e pausa 0.1.15 final retornaram; USB nativa antes da serial e SRAM GBA incremental persistida no final |
| Diretório e catálogo | content_paths, rom_info | fallback 1 MiB/2048; índice 64 MiB/100000, páginas de 64; 5000 exercitados no stable 0.1.15 |
| Importação CSV e miniaturas | tools/library.py | ferramentas e testes host; formato original de três campos preservado |
| Busca/favoritos/recentes | gui/library_tools | toque, escrita JSON e lista filtrada exercitados na 0.1.9 e repetidos na 0.1.13 |
| Fontes adicionais | assets/fonts, loader | Latin-1/OFL externo com SHA/tamanho; acentos conferidos no LCD |
| Temas externos | gui/library_tools | presets JSON e filtro de widgets; persistência no reboot e cores verificadas na 0.1.12 |
| Capas assíncronas/cache | gui/cover_worker | 0.1.13: quatro capas (uma inválida), 20 trocas e três reloads sem pânico |
| Preferências JSON | settings | parser transacional; 0.1.14 verifica UTF-8 e limite final de 64 KiB, preservando JSON anterior na falha |
| Inatividade e primeiro despertar | inactivity/input | HOME apaga; jogo ativo preservado por padrão; primeiro toque Start não altera guest e segundo altera, 0.1.10 e 0.1.13 |
| Botões laterais visíveis | touch_controls, vídeo | LCD e entrada real GBA; direções, diagonal, A/B, L/R, Start/Select, Menu e USB+touch |
| A164 I²C e padrão EASD | keyboard_i2c, profiles | E+A+O+K e T+Y observados fisicamente; W sem ação de cima |
| Perfis global/console/jogo | input_profiles, controls_editor | parser e editor Global/Sistema/SHA; aplicação/precedência/conflito/cancelamento em hardware; A164 físico por jogo pendente |
| GB/GBC/SNES/GBA/MD estados e bateria | adapters, BatteryStore | cinco sistemas exercitados com ROMs próprias; estados, novo boot e backup distinto |
| Integridade de save normal | BatteryStore/journal | host e cinco sistemas físicos: corrupção com tamanho igual, recuperação e arquivo recusado preservado |
| Estados Mega Drive | md_port/md_state | CPU/vídeo/SRAM restaurados; tamanho, SHA, ROM e schema inválidos recusados |
| GBC | gnuboy + ROM própria CGB | paleta colorida, MBC1, estado/SRAM/backup e estado truncado testados |
| RTC/EEPROM/Flash/mappers | adaptadores | RTC MBC3 com avanço desligado, MBC2 e MD SMD/pad6 implementados; EEPROM/Flash/mappers adicionais dependem de cobertura específica |
| Bateria e alimentação | BSP/INA226 | tensão/corrente/carregamento físicos; percentual não calibrado oculto |
| Áudio GB | gb_port, HAL/I2S | ritmo corrigido; usuário confirmou melhora; zero faltas/erros em 30,014 s na 0.1.15, ver AUDIO.md |
| gpSP desempenho | gba_metrics/benchmark | comparação pareada RV32/interpretador, ALU/SMC e métricas CPU/PPU/SD; RV32 reprovado para produção por divergência temporal |
| N64 primeiro marco | experiments/n64 | interpretador Mupen real: 22 instruções no host e P4, repetidas na 0.1.14 |
| N64 console interativo | CPU/dispositivos/VI/RSP/RDP | pendente; nenhum jogo, áudio, save ou compatibilidade N64 anunciado |
| Índice persistente/streaming de catálogo | biblioteca | índice persistente CRC/worker cancelável; paginação/busca/favoritos/recentes/USB com 5000 no stable 0.1.15 |
| Energia, remoção SD, falta de espaço | storage/BSP | proteção lógica e testes de falha host; ensaio físico abrangente pendente |
| Distribuição reprodutível | package/source_bundle | binário app-only, SD sem dados pessoais, fontes/licenças e hashes; validação por pacote |

Os relatórios são vinculados ao hash de cada build. Um teste realizado na 0.1.9/0.1.10/0.1.12 não é apresentado como se tivesse sido executado novamente na 0.1.13. A regressão atual repete os testes de persistência e retorno, com evidências próprias.

## Correções importantes

- O checker original de overlay interpretava instruções RISC-V como ponteiros. A análise passou a distinguir texto de rodata, mantendo o verificador de instruções.
- A primeira consulta de métricas revelou estouro na pilha USB. Scratch e leitores foram corrigidos; consultas repetidas passaram.
- O mux USB LP_SYS sobrevivia ao reset. A restauração IRAM antes do reset normal/pânico permitiu reencontrar o Launcher.
- A 0.1.2 revelou estouro na tarefa de volume. Eventos passaram a apenas sinalizar o redraw; 150 alterações de volume e cinco ciclos de saída/isolamento passaram na 0.1.3.
- A 0.1.7 encontrou leitura de SD pelo worker durante unmount da inicialização USB. Na 0.1.8, GUI e worker começam pausados durante a transição. Dump antigo de 27616 bytes foi arquivado, comparado byte a byte e limpo depois da regressão na 0.1.9.
- O mixer GB e a frequência declarada divergiam; o loop não seguia corretamente os ciclos durante LCD desligado. O relógio e o feeder I2S foram corrigidos, com contadores reais da saída.
- O alocador do Launcher encolhe reservas para imagens menores. O adaptador passou a recusar esse caso antes da remoção do app. Nenhum plano de reorganização adicional foi executado.

## Histórico resumido

| Versão | Resultado específico |
|---|---|
| base upstream | 9896816 bytes, compilada e não instalada |
| 0.1.1–0.1.3 | pilha USB, reset/PHY, perfis, bateria, diagnóstico A164 e stress de volume |
| 0.1.4–0.1.5 | persistência compartilhada GB/GBA/SNES, estados anteriores e recuperação |
| 0.1.6 | botões laterais e primeiro ajuste do mixer GB; testes de toque |
| 0.1.7 | fontes/UI/worker; falha de unmount identificada, build não aprovado |
| 0.1.8 | startup corrigido e ritmo por ciclos; melhora audível relatada pelo usuário |
| 0.1.9 | busca/cache/inatividade e CPU N64 no P4; duas faltas parciais de áudio medidas |
| 0.1.10 | feeder ajustado; zero faltas em 46,494 s e primeiro toque de despertar testado |
| 0.1.11 | build intermediário MD; não instalado nesta sequência |
| 0.1.12 | MD, journal SHA, GBC colorido, temas e recuperação física |
| 0.1.13 | retorno direto pela pausa e regressão dos cinco sistemas |
| 0.1.14 | updater legado removido; validação UTF-8 e tamanho final das preferências |
| 0.1.15 | RTC/MBC2, SMD/pad6, índice paginado/editor, comparação RV32 e isolamento MSC em callbacks |

O pacote não distribui ROMs/BIOS comerciais, saves, screenshots dos jogos do usuário, backup integral, tokens ou relatórios brutos pessoais. As limitações de N64 e gpSP permanecem em N64_PORT.md e GBA_PERFORMANCE.md.

Na regressão 0.1.13, o SD também foi entregue ao Mac: acesso a arquivos e launch foram recusados no firmware enquanto o host tinha o cartão; o disco foi identificado pelo VID/PID USB exato do Tab5 e ejetado antes da retomada. Fonte relida teve SHA igual. O stable foi confirmado novamente com ELF completo e coredump vazio, e o Pokémon foi deixado no estado preservado com os botões laterais visíveis.

## Fechamento do corretivo 0.1.14

Os cinco sistemas repetiram estados, persistência e backup no stable; MD conferiu também as quatro recusas de estados inválidos. A HOME e o jogo recusaram o updater herdado, sem reset ou interrupção do guest. Retorno direto da pausa salvou SRAM e fez novo boot do mesmo ELF, com USB nativa observada antes da console. O diagnóstico N64 repetiu 22 instruções no P4 e foi substituído pelo stable. O estado atual do Pokémon foi preservado no slot 3 livre e verificado por SHA antes do load final. Na medição de 31,237 s, 1874 blocos tiveram zero faltas/erros; coredump vazio. Relatórios públicos 0.1.14 identificam explicitamente os ensaios mais amplos da 0.1.13 como históricos.

## Fechamento físico 0.1.15

Retorno pela pausa persistiu o incremento SRAM GBA e enumerou a USB nativa antes da console. O Launcher confirmou novamente apps e reservas iguais; os bytes originais de preferências/perfis e catálogo foram relidos. O estado preservado do Pokémon foi conferido por SHA, carregado e deixado pausado. No intervalo de 30,014 s, 1801 blocos de áudio tiveram zero faltas, blocos parciais/vazios ou erros. Coredump final vazio. Evidência sanitizada: `artifacts/hardware-verification-v0.1.15.json`.

## Console N64 0.2.0 e bateria stable 0.1.17 — 24/11/2026

O indicador vazio foi corrigido na HOME e na pausa com estimativa pela curva de tensão de um pack de duas células. No Tab5, a captura mostrou 8,33 V e 96% estimados durante a carga. Não é medidor de carga calibrado. O build stable continua padrão, com os cinco sistemas e GBA interpretado; RV32/JIT e a corrupção de heap conhecida permanecem isolados, com seus diagnósticos preservados.

O perfil separado `n64` passou do probe de CPU ao console: Mupen puro, CXD4, RDP em software, boot, RDRAM paginando a ROM por SD, VI, AI, Joybus e bateria. A imagem final tem 4025968 bytes, na mesma reserva app4 de 4063232; o stable 0.1.17 tem 4033184 bytes. O Launcher fez backup e instalação somente em app4, preservando reservas e descritores dos demais aplicativos. A leitura física do ELF final é `917ed3a68e8bdabc1a602eb6846023d9057929c14a8e3a46ed54f71cc2550865`.

Foram transferidos 21 jogos únicos (465567744 bytes), das 24 variantes de nomes fornecidas, para `Tab5-Emu/roms/n64`, com SHA de releitura conferido e origens preservadas. Catálogo e índice anteriores têm backup. A biblioteca N64 mostra 21 jogos e um diagnóstico próprio; arquivos AppleDouble gerados pelo macOS foram excluídos do índice, e a descoberta ganhou filtro com regressão. Nenhuma ROM comercial ou captura desses jogos integra o pacote. Resumo: `artifacts/n64-rom-transfer.json`.

No ELF final, o diagnóstico 0BSD gerou verde/vermelho por A, gravou o contador EEPROM 1→2 em dois boots e preservou a primeira bateria como backup; coredump vazio. Os testes host passaram com ASan/UBSan: 69 testes Python, doze suítes C++, duas C, console N64 real e o antigo probe separado. A CI passou a incluir a suíte do console; execução remota depende de push/PR.

O ensaio inicial SM64 no P4 gerou o título 3D e áudio, com cerca de 2 VI/s nessa cena. Exibir memória durante a transição revelou uma falha do adaptador: ignorava `H_START=0` enquanto libultra reutilizava o framebuffer. O corretivo gera preto no apagamento e tem teste de apagar/reativar. A validação posterior identifica separadamente o ELF final; as capturas do candidato anterior não são usadas como prova dele. Limitações completas em N64_PORT.md.

A repetição final no ELF acima conferiu título e rosto 3D animado de SM64, preto durante a carga e fade por Start, em uma janela de 252 s. A cena do título ficou próxima de 2 VI/s e a do rosto de 1 VI/s. AI produziu 272480 frames fonte e 408652 frames de saída, mas houve muitos blocos vazios; não há promessa de áudio contínuo. Seleção de arquivo e gameplay não foram validados. Retorno à biblioteca com coredump vazio e 22 entradas N64 (21 jogos + diagnóstico). Evidência sanitizada: `artifacts/n64-hardware-verification-v0.2.0.json`. O stable alternativo foi somente compilado e relido no SD nesta etapa.

## Perfil unificado com núcleos no SD — 0.2.1-sd

O usuário apontou que o perfil exclusivo N64 havia ocultado os demais sistemas e que a paginação sobrepunha o rodapé. O novo perfil `sd-cores` reúne os cinco sistemas maduros e N64. Seus módulos são lidos do microSD para o overlay PSRAM já existente, com descritores de SHA/endereço/tamanho vinculados à compilação. As ROMs e saves anteriores foram preservados. GBA continua no interpretador, sem alterar o gerenciador RV32 experimental.

O combinado integral na flash media 4411488 bytes e não foi instalado. A imagem SD mede 4030000 na reserva app4 inalterada de 4063232. ELF físico `2f36109e31bb87b94dfe27fbcd088e1164c4bf20764b74bcfc36d13ccb0e5d4e`; os cinco módulos ocupam cerca de 1,98 MB. A instalação preservou os outros apps. Treze ciclos físicos conferiram GB/GBC/GBA/SNES/MD/N64, recusa de módulo truncado e recuperação, com coredump vazio. N64 repetiu A e bateria com backup.

A barra de paginação foi reposicionada acima das dicas; status/rótulos têm limites de espaço e as setas são ocultas em listas de uma página. A captura física confirmou o layout. Registros/DMEM/IMEM N64 ficam em RAM interna, código em PSRAM; memória auxiliar RDP é liberada na saída e o prescaler VI sem uso foi removido. O ganho medido em SM64 foi modesto, cerca de 6–7% nas cenas equivalentes. Chegou à seleção de arquivo por Start, com rosto 3D e áudio ainda descontínuo. Nenhuma certificação de gameplay ou velocidade normal. Relatório: `artifacts/sd-cores-hardware-verification.json`; reprodução em NUCLEOS_NO_SD.md e alternativas em N64_ALTERNATIVAS.md.
