# Testes e evidências

Testes de host não substituem hardware. `bash tools/test_host.sh` executa 63 testes Python, doze suítes C++ e duas suítes C com ASan/UBSan: plataforma/mapeamento/métricas/estados/tags SMC/gate MSC concorrente, parser real de catálogo/índice, parser/editor real de perfis, BatteryStore com arquivos temporários, preferências JSON, SRAM/SMD/pad/schema MD, RTC GB, leituras de arquivos e alocações GB (incluindo código C real dos núcleos), migração de saves e EEPROM MD. As suítes C adicionais executam o SPC/APU SNES e o interpretador M68000 reais. O teste host do store substitui apenas o digest dependente de PSA por uma função determinística; verifica transações, deduplicação, recuperação, rejeição e falhas de escrita, sem alegar validar SHA-256.

Na continuação de compatibilidade, a suíte EEPROM testa o protocolo serial, página de quatro bytes, ACK/NACK, wrap, STOP, reset, snapshot de transação e recusa de snapshot malformado. Outra suíte executa as ROMs próprias no interpretador M68000 real, com leitura/gravação e segunda inicialização nas placas Sega/EA/Acclaim 16M. A suíte Python verifica simulação sem escrita, cópia sem sobrescrita, preservação de origem/journal/backup, rollback em erro de sync após escrever o payload, colisões de RTC, symlinks e formatos GB/GBA/SNES/MD. O teste de catálogo confirma restauração após falha do ensaio.

`tools/smoke_md_eeprom.py --elf-sha256 HASH --report NOVO.json --work DIRETORIO_NOVO` usa três ROMs 0BSD de hash conhecido e namespace próprio. Verifica bateria importada, instruções do guest, Start, boot novo, estado v4, backup e recusas de SHA/placa/contador serial inválidos. `tools/smoke_save_migration.py` com os mesmos argumentos verifica no aparelho baterias/estados GB/GBC e conversão raw Gwenesis com SRAM correspondente; o estado migrado precisa restaurar a linha do tempo anterior. Ambos preservam os originais no diretório local de trabalho e substituem/restauram catálogo e índice com backup exato. Não usam saves comerciais como fixtures. Os relatórios `physical-*-v0.1.16-complete.json` e `physical-gba-save-compat-v0.1.16.json` identificam o novo ELF; tentativas incompletas anteriores ficam preservadas com `complete: false`.

As leituras usam FILE real com injeção de EIO parcial, seek/EOF/close inválidos e falha de alocação; nenhum resultado parcial substitui o buffer anterior. A suíte GB exercita as oito alocações compartilhadas, rollback, nova tentativa, proteção de outro dono do pool, falha de SRAM e SRAM máxima de 128 KiB, com o loader e alocador compartilhado reais. Os seis testes Python adicionais verificam o gate app-only e o staging do loader fixado por SHA.

`tools/smoke_storage_errors.py --elf-sha256 HASH --report ARQUIVO` usa somente a fixture GBA própria de hash exato e um slot vazio. Bloqueia arquivos temporários com diretórios vazios próprios para testar falhas de estado/save e recusa de saída/Launcher; confere a preservação dos dados, a mensagem USB, a recuperação e a remoção dos arquivos descartáveis. Um estado truncado também deve ser recusado sem parar o jogo. Não injeta remoção física do SD nem corte de energia.

A CI app-only compila com SDK fixado, executa esta suíte, verifica tamanho/perfil/exclusão de RV32 e empacota recursos/fontes. Sua execução remota depende de push/PR; validação local não é apresentada como execução do GitHub Actions nem como teste físico de um artefato CI.

O fechamento stable acrescenta casos de RTC monotônico na pausa (halt, fração e ausência de dupla contagem), parsing de consultas USB e seleção exata no índice, aliases de perfis Mega Drive, bordas de teclado e temas inválidos. `tools/smoke_stable_followups.py --elf-sha256 HASH --report ARQUIVO` verifica pausa/estado RTC, captura do menu/editor, cancelamento sem alterar perfis, tema de versão errada e consultas/launch fora da página num catálogo de 5000. As fixtures próprias são conferidas por hash; catálogo, tema e arquivos descartáveis são restaurados. A navegação USB não certifica rollover ou hotplug A164.

A suíte adicional compila o feeder ESPP real, gerado pelo mesmo staging SHA fixado do firmware. Um relógio/stream FreeRTOS determinístico verifica chegada em 12 ms, notificações antigas espaçadas sem renovar o prazo total de 16 ms e preenchimento de silêncio quando o produtor está atrasado ou parado. Isso valida a política de software, sem simular o periférico I2S ou avaliar o alto-falante.

`tools/smoke_gba_session.py --elf-sha256 HASH --report ARQUIVO --require-zero` exige a ROM própria de hash exato e o firmware esperado antes de alterar seus saves. Mede o sink, confere o produtor parado na pausa, recusa JIT no stable e repete entrada/estado/SRAM/backup. A opção exige zero frames de silêncio naquela janela; sem ela, o relatório mantém a ocorrência como observação, sem apagá-la. Não mede o alto-falante.

No macOS foi usado o SDK 26.5 para C++ devido à incompatibilidade do linker instalado com o SDK 27. A instalação de ferramentas do sistema permaneceu intacta.

Na retomada stable 0.1.16, `tools/smoke_gb_rom_validation.py --elf-sha256 HASH --report ARQUIVO` verifica cinco recusas de ROM e suas mensagens. Substitui temporariamente somente o homebrew GB próprio de hash exato, restaura os bytes em `finally` e confere a releitura. Recusa outro firmware, ROM divergente ou jogo ativo antes de modificar arquivos. Os testes host dessa ferramenta verificam a proteção de identidade e a restauração em falhas, sem alegar compatibilidade física.

## Hardware

Tab5 P4 rev. 1.3, CPU 360 MHz, flash 16 MiB/DIO/80 MHz, PSRAM 32 MiB/200 MHz, ST7123 e SD FAT32. Firmware 0.1.5: SHA-256 `fc82a0c7d021d9d2c68587173fc2a3b3dfdb30e25570108bc3c43cb109478227`, ELF `369d4d58d36fa73d8c728f7ec0463c79db32e802f8f6539ef6099c97b92a3551`. Upload, releitura/hash, instalação e boot no app4 foram verificados separadamente.

| Ensaio | Resultado e alcance |
|---|---|
| GB/SNES próprios 0BSD | CPU emulada incrementou SRAM no boot; estado antigo restaurou contador; boot novo manteve o save; `.bak` anterior distinto preservado |
| Recuperação GB/SNES | primário descartável truncado → backup carregado, primário em `.rejected`; estado de 1 byte recusado, jogo continuou, fixture removida |
| GBA próprio 0BSD | diagonal/A/Start via USB, estado, SRAM 128 KiB em novo boot e backup distinto |
| Recuperação GBA | primário truncado recuperado; ambas as gerações inválidas recusadas sem sobrescrita; restauração dos bytes válidos permitiu reabrir |
| Estados anteriores GB/SNES | slot 5 anterior abriu; frames continuaram; originais e cópias das ROMs conferidos por hash; SRAM normal 32 KiB/8 KiB |
| Ciclo final 0.1.5 | retorno HOME → Launcher, USB enumerada antes de abrir console, `SW_RESET`, apps/layout iguais; novo boot/hash e saves GB/SNES após reinício; coredump vazio |
| A164 físico (0.1.2) | E+A+O+K por 2,554 s; T+Y por 0,554 s; liberações observadas; W isolado sem ação |
| Stress volume/foco (0.1.3) | 120 mudanças na HOME, 30 durante GBA, cinco ciclos quit/isolamento, sem novo reset |

Ferramentas reproduzíveis: `tools/build_test_roms.py`, `tools/build_8bit_test_roms.py`, `tools/smoke_gba.py`, `tools/smoke_sram.py`, `tools/smoke_gba_recovery.py`, `tools/capture_input.py`, `tools/emulator_usb.py`. Os scripts de corrupção conferem a identidade das ROMs próprias e escrevem somente em seus namespaces descartáveis. Não executar em saves únicos do usuário.

`artifacts/gba-homebrew-015-benchmark.json` registra amostras brutas do homebrew leve, por tempo monotônico externo, sem screenshots/entrada no intervalo. `artifacts/stable-015-measured.json` descreve as seções ELF, sem atribuir payload de overlay duas vezes à flash. A evidência sanitizada da distribuição vincula os resultados ao SHA exato do binário. Relatórios físicos brutos `.local.*`, dumps e capturas de títulos do usuário não são distribuídos.

Pendências registradas no marco 0.1.5 (consulte o estado atual em LIMITACOES.md): oito teclas/ghosting/hotplug, overrides físicos de perfis, sem SD, transições de alimentação, áudio avaliado, ROMs inválidas em cada núcleo, SD cheio/corte de energia em cópia descartável, RTC/MBC2/EEPROM/Flash, Mega Drive, baseline GBA comparável e N64. Veja [limitações](LIMITACOES.md).

## Marco 0.1.13

- Host: 19 testes Python e seis suítes C++ ASan/UBSan (plataforma/toque, catálogo, perfis, BatteryStore/journal, preferências JSON e SRAM/schema MD). N64 tem suíte própria com caso positivo e cinco recusas explícitas.
- `tools/smoke_sram.py`: GB/GBC/SNES, estados, boot novo, backup distinto e estado truncado recusado.
- `tools/smoke_gba.py`: diagonal/botão/Start, estado, SRAM e backup; mantém a política de SRAM mais recente ao carregar estado gpSP.
- `tools/smoke_md.py`: instruções 68000, Start/VDP, estado CPU/vídeo/SRAM, reinício do núcleo e recusas de tamanho/SHA/ROM/schema.
- `tools/smoke_integrity.py`: cinco sistemas, corrupção de save normal mantendo o tamanho, recuperação do backup e preservação do recusado. A opção de arquivar um recusado anterior exige a identidade exata das ROMs próprias, copia e relê antes de repetir. Não usar com ROMs ou saves do usuário.
- Retorno direto na pausa: guest incrementou SRAM GBA; Launcher enumerou pela USB antes de abrir a serial; leitura do save confirmou o incremento e reset SW_RESET.
- Fonte/temas, busca/favoritos/recentes, capas e despertar têm evidência por build em PROGRESSO.md. Não transferir silenciosamente um teste antigo para um binário novo.

Relatórios `*.local.json/bin/elf/png`, logs e ROMs geradas permanecem locais/ignorados. Os resumos públicos retiram MAC, tokens, caminhos pessoais e capturas dos jogos comerciais. O manifesto da distribuição vincula o binário ao SHA da evidência. O ensaio N64 é CPU headless, não compatibilidade de jogos.

Na regressão final 0.1.13, busca/favoritos/recentes/despertar passaram novamente; quatro entradas de capa, uma inválida, exerceram 20 trocas e três reloads. Handover USB passou com exclusão do FAT, ejeção real do disco identificado pelo próprio Tab5 e releitura SHA igual após remount. O áudio GB teve zero faltas/erros em 1886 blocos no intervalo de 31,435 s. Coredump final vazio.

## Corretivo 0.1.14

A mudança de firmware remove a rota USB herdada que escrevia `otadata` e reforça o parser de preferências. Host: 19 testes Python e seis suítes C++ ASan/UBSan passaram, incluindo UTF-8 inválido, caminhos longos, preservação de campos desconhecidos e o limite exato de 65536 bytes já contando a quebra de linha final. Crescimento além do limite retorna falha e preserva o JSON existente. N64: caso positivo e cinco recusas passaram novamente.

`tools/smoke_updater_guard.py` exige a versão 0.1.14 e o hash ELF esperado **antes** de enviar `ENTER_UPDATER`. Confere a mensagem do Paludo, uptime crescente, mesmo firmware/jogo e progresso do guest. Não executar esse comando manualmente em builds antigos; a implementação anterior tinha efeito de boot e escrita em flash.

`tools/verify_package.py <diretório> --require-tested` confere SHA256SUMS, manifesto/binário, CRC do ZIP, fontes/tamanhos, diretórios de dados pessoais vazios e cada arquivo do source bundle contra seu manifesto, sem extrair nem instalar.

Para um snapshot de desenvolvimento com alterações ainda não commitadas, `--allow-dirty` aceita o campo `dirty: true` explícito e mantém todas as verificações de bytes/fontes/arquivos. O padrão continua exigindo fonte commitada. Essa opção evita atribuir um snapshot local ao conteúdo exato do commit base; não marca a árvore como limpa nem substitui testes físicos.

No Tab5 stable 0.1.14, GB/GBC/SNES repetiram estados/SRAM/novo boot/backup e estado truncado recusado; GBA repetiu entrada diagonal/A/Start, estado e política de SRAM mais recente; MD repetiu CPU/vídeo/SRAM/novo boot e quatro recusas de estados. Toque Start e guard legado passaram; retorno direto da pausa persistiu SRAM, enumerou a USB nativa e fez novo boot do mesmo ELF. Pokémon: estado preservado no slot 3 antes da atualização e restaurado, 31,237 s/1874 blocos de áudio sem faltas/erros, coredump zero. Evidências: `artifacts/hardware-verification-v0.1.14.json` e `artifacts/n64-cpu-probe-verification-v0.1.14.json`. Os ensaios de falha de save com tamanho igual, busca/cache/despertar e handover USB permanecem identificados como regressão da 0.1.13.


## Marco 0.1.15

Stable final: SHA `e70e0cf76ade9e3e93021beebe344fc71ffca889c4438b9235f31e1b4b96f650`, ELF `360a8d438ebfe5eb0bfc82ee7d3084b17d87a57bffd747ee3e37d073c5456c64`. Nesse ELF passaram o catálogo de 5000 entradas com ejeção segura/remount, 12 reaberturas consecutivas RTC/MBC2 com logs CDC sem falha de armazenamento/pânico e coredump vazio, RTC/avanço desligado, MBC2 e MD/SMD/pad6. GB/GBC/SNES/MD/GBA repetiram estados, SRAM em novo boot e backup distinto; estados inválidos GB/GBC/SNES/MD foram recusados. A corrupção profunda de save com tamanho igual permanece identificada como ensaio anterior, sem repetir alterações desnecessárias nas fixtures. O updater legado foi recusado na HOME e no jogo; o toque A lateral alterou o framebuffer da fixture GBA e apareceu aceso no LCD.

O editor Global foi aplicado/restaurado no stable, preservando campos legados/desconhecidos (`artifacts/editor-global-v0.1.15.json`). Aplicação Sistema/Jogo, conflito, cancelamento, precedência e bloqueio modal foram exercitados no candidato ELF `25d8e5ef…` (`artifacts/editor-v0.1.15.json`). A comparação GBA de oito janelas também pertence a esse candidato; não é transferida para a imagem experimental recompilada. O RV32 ficou reprovado para produção por divergência de temporização, apesar de ALU/SMC corretos e ausência de faltas de áudio nos intervalos observados.

Ferramentas novas: `smoke_large_library.py --usb`, `smoke_sd_reopen.py`, `smoke_compatibility.py`, `compare_gba_backends.py`. O helper de ejeção identifica o disco pelo VID/PID/serial exatos do Tab5, exige ejeção pelo macOS e mantém o SD com o host se o resultado for falha/ambíguo. O teste de concorrência host verifica READ/WRITE/Ready recusados sem invocar backend e espera pelo callback já aceito antes de fechar o gate. Não injeta escritas brutas no cartão real. Relatórios públicos removem MAC, portas, endereços de rede e caminhos pessoais.

O fechamento voltou ao Paludo pela pausa e confirmou SRAM GBA persistida e reservas/apps iguais. Após restaurar preferências/perfis e carregar o estado preservado do usuário, o Pokémon apresentou zero faltas/erros em 1801 blocos durante 30,014 s. Foi deixado pausado; coredump final vazio. Evidência: `artifacts/hardware-verification-v0.1.15.json`.

## Console N64 e bateria 0.1.17

`bash tools/test_n64_console_host.sh` usa o interpretador Mupen, RSP CXD4 e RDP Angrylion reais, com ASan/UBSan. Confere vídeo RGB565, apagamento/reativação VI, Count/Compare com wrap, ROM paginada, DMA, Joybus reutilizado, RSP/BREAK, EEPROM e FlashRAM. O antigo `tools/test_n64_host.sh` continua passando separadamente. `tools/test_host.sh` passou com 69 testes Python, doze suítes C++ e duas C; inclui bateria de duas células, mapa N64 e recusa de arquivos AppleDouble na descoberta.

`tools/build_n64_host.py --output /tmp/tab5-n64-host` compila o runner de cenas com os mesmos fontes adaptados. O orçamento informado limita as chamadas; interrupções/VI podem encerrá-las antes. Use o contador `instructions` efetivo do relatório para análises. O runner não fornece ROMs. `TAB5_N64_PLAY=1` injeta Start/A em pontos definidos; não é certificação manual de gameplay.

`tools/smoke_n64.py --elf SHA_COMPLETO --report /tmp/n64-physical.json` exige o ELF exato via GET_INFO, a ROM de teste própria já no SD e a HOME ativa. Confere verde/vermelho por A, recusa de estado instantâneo, dois boots EEPROM com backup e coredump vazio. Capturas e relatórios brutos permanecem locais. A prova física final e o intervalo SM64 são registrados em N64_PORT.md/PROGRESSO.md com seu alcance, sem reutilizar silenciosamente o candidato anterior.

## Núcleos no SD 0.2.1

`tools/check_sd_cores_artifact.py` compara os cinco módulos exportados com os descritores embutidos no ELF, além de conferir o binário canônico, a reserva app4 e a ausência de GBA RV32/JIT. `test_sd_core_format.py` cobre adulteração, truncamento, bytes extras e vínculo ao endereço exato. Os testes comuns passaram com 71 casos Python, doze suítes C++ e duas C; o console N64 passou novamente com ASan/UBSan após a separação do estado interno e a liberação da memória RDP.

`tools/smoke_sd_cores.py` usa somente ROMs próprias, confere catálogo GB/GBC/GBA/SNES/MD/N64 e executa 13 ciclos de carga/saída alternando módulos. Substitui temporariamente apenas a cópia deste build do módulo GBC por três bytes, exige recusa sem executar, restaura o arquivo exato e prova a retomada. A geração original fica disponível no computador e é conferida por SHA antes/depois. A prova física terminou com coredump vazio.

`tools/smoke_n64.py` repetiu cores por A e EEPROM 6→7 com backup no ELF 0.2.1-sd. Essa contagem pertence ao diagnóstico compartilhado entre os testes e não é valor fixo de boot. A captura física da HOME confirma a paginação acima do rodapé; a comparação SM64 é registrada separadamente, usando o mesmo ELF e cenas equivalentes.

## Candidato Daedalus / início GBA 0.2.2-sd

`tools/test_host.sh` passou com 71 testes Python, a suíte nova de preroll GBA e
as suítes anteriores, com ASan/UBSan. `tools/test_n64_console_host.sh` passou
após tornar os acessos de meia palavra CXD4 independentes de alinhamento.
`python3 tools/test_daedalus_host.py` compila o decodificador Daedalus real e
confere fill, scissor, triângulo, textura RGBA16, DP e falhas limitadas de listas.
O runner `build_n64_host.py --daedalus --sanitize` usa os mesmos fontes;
`TAB5_N64_PLAY=1` envia Start/A também em intervalos posteriores para explorar
menus. Contadores e imagens de ROM do usuário são locais. Nenhum desses testes
substitui medição física de FPS/áudio ou comprova compatibilidade geral.

O teste de console HLE (`build_n64_host.py --console-tests --daedalus --sanitize`)
passou com a fixture própria: boot, periféricos e saves do caminho LLE, mais
OSTask gráfica real via SP, interrupções SP/DP e framebuffer escrito pelo HLE,
sem executar instruções RSP gráficas. A fixture configura a escala VI para o
viewport de 320×240 do Daedalus; os testes LLE mantêm sua configuração anterior.
Mario Kart 64 (título) e Ocarina of Time (introdução a cavalo) também produziram
cenas no runner ASan/UBSan, sem usar ROMs ou capturas no pacote distribuído.

A validação física da 0.2.2-sd está em
`artifacts/daedalus-hardware-verification-v0.2.2.json`: app0/ELF exato, cinco
módulos relidos por SHA, 13 trocas entre os seis sistemas, recusa/recuperação de
módulo truncado, controles/EEPROM N64, cenas SM64/Start, início/fluxo contínuo
GBA, retorno à biblioteca e coredump vazio. Permanecem explícitas as limitações
de velocidade/áudio N64 e da introdução Circle GBA, sem confundir coleta
concluída com compatibilidade ou qualidade acústica certificadas.
