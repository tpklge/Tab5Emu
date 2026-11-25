# Memória e flash

Hardware de referência: Tab5 ESP32-P4, 16 MiB flash, 32 MiB PSRAM, display 1280×720. O aparelho foi confirmado em execução própria: P4 revisão 1.3, CPU 360 MHz, flash 16 MiB, PSRAM 32 MiB a 200 MHz e painel ST7123. Cada medida abaixo identifica seu build e escopo; medidas do homebrew GBA e do ensaio de CPU N64 não representam outros jogos.

A consulta inicial ao launcher encontrou quatro slots ocupados e um campo legado de 6160384 bytes sem alocação, posteriormente corrigido pela leitura da tabela real do backup: 9 MiB livres antes de registrar app4. Fonte: `artifacts/audit/launcher-flash-layout.json`. Esse espaço não representa PSRAM, RAM livre ou um slot que já aceite a nova imagem.

O firmware publica logs de heap interno/PSRAM e maior bloco a cada inicialização. O perfil stable remove a reserva de 2 MiB do port Jedi Knight; a arena de ROM de 6 MiB e o cache gpSP de até 16 MiB permanecem na arquitetura original, cujo reaproveitamento requer medir o jogo ativo. Não se somam essas reservas como se fossem sempre independentes.

Sem índice, o catálogo é limitado a 1 MiB, 2048 jogos e profundidade JSON 8. cJSON aloca uma árvore adicional; medir seu pico antes de aumentar os limites. O scanner visita no máximo 16384 entradas por sistema, profundidade de diretório até 4. O pacote não embute capas/fontes extras/ROMs no app. Uma capa RGB565 360×360 custa 259200 bytes mais alinhamento, temporário JPEG e cache; o cache tem três entradas, fila de quatro pedidos e decoder em worker. Foi exercitado no aparelho; não há medida abrangente do pico em bibliotecas grandes.

O mapa do linker e `size-components` são a evidência necessária para custo de núcleo. Os binários e relatórios de build distinguem o baseline integral do stable. Não há números inventados de RAM em jogo ou desempenho. O relatório final do marco deve atualizar tamanhos após a conclusão das compilações.

Estado N64: o ensaio de CPU alocou e liberou 13635168 bytes no P4; ainda não inclui RDRAM/VI/RSP/RDP de um console. Orçamento futuro: RDRAM 4/8 MiB; framebuffer 320×240 RGB565 150 KiB por buffer; memória de CPU/RSP/RDP, caches de textura/ROM, app e áudio adicionais. O total não permite supor que uma ROM de 32 MiB caiba junto do emulador em PSRAM. Isso é orçamento N64; o candidato RV32 de GBA é separado e continua experimental.

## Medidas do primeiro marco

| Medida estática | Baseline integral | Stable 0.1.0 |
|---|---:|---:|
| App-only | 9896816 bytes | 4000896 bytes |
| Maior overlay (PSRAM compartilhada) | 1119104 bytes | 764800 bytes |
| UI residente em PSRAM | 651776 bytes | 655872 bytes |

Payload de código/tabelas por núcleo stable: gpSP 392576 bytes; SNES 396800; Genesis 764800; gnuboy GB/GBC 61952. Esses payloads já estão incluídos na área de armazenamento em flash do overlay; não os somar novamente ao tamanho do app. Não incluem todo o estado dinâmico/RAM de cada núcleo. O relatório por archive do analisador padrão não atribui o payload de overlay corretamente; a ferramenta publica essa limitação e usa seções ELF para os valores acima.

Seções estáticas do stable: IRAM text+alinhamento 111744 bytes; DRAM0 data 28082 e BSS 33696; DRAM1 BSS 2456; external BSS 323184; external data 112. Heap, DMA/framebuffers, cache de ROM e maior bloco durante jogo ainda não foram medidos. Consulte `artifacts/stable-measured.json` para as seções completas.

Catálogo: além de 1 MiB e profundidade 8, o pré-parser limita delimitadores estruturais a 32768 para impedir uma árvore cJSON muito maior em entradas compactas maliciosas. Ainda não é um parser streaming.

## Medidas 0.1.2

App-only 4016848 bytes, app4 reservado em 0x6F0000–0xAD0000 (4063232 bytes), margem 46384 bytes. A reserva foi criada pelo Launcher com autorização explícita; os outros quatro apps ficaram nas posições anteriores. O binário não contém tabela ou bootloader. Fonte estática completa: `artifacts/stable-012-measured.json`.

Payloads do overlay por núcleo: gba 393216 bytes, snes 396800 bytes, genesis 764800 bytes, gbc 61952 bytes. UI residente: 656896 bytes. Esses payloads já pertencem à flash do app.

Na primeira HOME 0.1.1: 170095 bytes internos livres, maior bloco 102388; PSRAM livre 23174204, maior bloco 23068660. Durante o benchmark leve 0.1.1: mínimos entre amostras 169527 internos e 10708036 PSRAM; mínima desde boot na amostra de jogo: 66788 internos e 10690280 PSRAM. Não extrapolar para jogos maiores ou outros núcleos. O loop de bateria 0.1.2 acrescenta uma tarefa de 4096 bytes de stack em prioridade 2, sem I2C no callback LVGL. A164 usa stack 4096 e prioridade 5; diagnóstico USB usa stack 6144. Scratch de quantis usa 4096 bytes residentes e leitores serializados; snapshot usa 64 bytes de stack.

## Medidas 0.1.5 — histórico

App-only **4028112 bytes**, SHA `fc82a0c7d021d9d2c68587173fc2a3b3dfdb30e25570108bc3c43cb109478227`. Reserva app4 4063232 bytes; **35120 bytes livres**. Overlays: GBA 389632, SNES 397312, Genesis 764800 e GB/GBC 65536 bytes; maior região compartilhada 764800; UI residente 657280. Esses payloads já integram a flash do app. Relatório estático: `artifacts/stable-015-measured.json`. Saves normais usam staging limitado a 1 MiB; estados GB/SNES também são conferidos e carregados em buffer temporário antes de modificar a máquina. Orçamento dinâmico depende da ROM e não pode ser deduzido só do mapa.

## Stable e diagnóstico 0.1.13

Stable: **4027408 bytes**, margem **35824** na reserva app4 de 4063232. SHA `ae73c4d6b72e4ac6d7a240456ec356e5144077d7c22098c046a5a7b36a9b00eb`. Overlays: GBA 389632, SNES 398336, Genesis 777600 e GB/GBC 66688 bytes; armazenamento já incluído no app. O SRAM MD de 128 KiB fica em BSS, sem array inicializado de 128 KiB no binário. Fontes extras (87384 bytes) estão no SD. Fonte estática: `artifacts/stable-v0.1.13-measured.json`.

O combinado N64+todos os núcleos foi medido com 4076784 bytes mesmo com otimização por tamanho no diagnóstico; excederia app4 em 13552. O perfil diagnóstico final conserva O3 e troca GB/GBC pelo probe: **4013792 bytes**, margem **49440**, mesma reserva/mesmo arredondamento de 64 KiB. A fonte de medidas está em `artifacts/n64-cpu-probe-v0.1.13-measured.json`. Stable mantém os cinco sistemas; nenhum padding de dados foi acrescentado.

O `r4300_core` do ensaio mede 13635168 bytes no P4, além dos 4 KiB de scratch e da aplicação. Esse custo real precede RDRAM/texturas/ROM/áudio de um futuro console. O perfil diagnóstico faz o ensaio antes da UI, libera o core e registra o ELF/resultado no cartão. Não é um benchmark de jogos nem prova de que uma ROM de 32 MiB caiba na PSRAM.

A política nova de áudio usa 16 descritores DMA de 240 frames e espera de até 48 ms; a profundidade de 80 ms a 48 kHz acrescenta aproximadamente 4 KiB internos frente ao feeder anterior. As fontes do SD são carregadas com hash/tamanho e a GUI fica pausada no jogo. Os snapshots de heap do benchmark são específicos da cena; não somar overlays compartilhados como se fossem residentes simultâneos.

## Build corretivo 0.1.14

Stable: **4026928 bytes**, margem **36304** na reserva de 4063232. Diagnóstico N64: **4013392 bytes**, margem **49840**. Os dois continuam no mesmo intervalo de arredondamento de 64 KiB; não exigem mudar a tabela. A remoção da rota de atualização antiga compensa o pequeno validador UTF-8 adicional nas preferências. Relatórios estáticos: `artifacts/stable-v0.1.14-measured.json` e `artifacts/n64-cpu-probe-v0.1.14-measured.json`. Esses números são de compilação; a evidência física de cada perfil identifica separadamente o ELF executado.

## Build 0.1.15

Stable: **4040032 bytes**, margem **23200** no app4. GBA RV32 experimental (sem SNES): **4027776 bytes**, margem **35456**. Os dois ficam na reserva existente de 4063232 bytes. Stable usa `-Os` apenas nos arquivos originais da GUI/toolbar; os núcleos conservam suas flags de desempenho. Fontes dos relatórios: `artifacts/stable-v0.1.15-measured.json` e `artifacts/gba-rv32-v0.1.15-measured.json`.

Overlays stable: GBA 390016, SNES 398336, Genesis 778368 e GB/GBC 68224 bytes. RV32 leva o overlay GBA a 739328 bytes; a maior região compartilhada continua 778368. UI residente: stable 594816, experimental 638592 bytes. Cache JIT adicional: 1 MiB ROM + 256 KiB RAM em PSRAM mapeada para execução, liberada no unload. Esses números estáticos não incluem heap dinâmico nem implicam equivalência temporal de CPU.

Com índice, o firmware percorre o arquivo em streaming: scratch de 1 KiB, página de 64 jogos e caches de identidade/contagem limitados a 128 entradas. O gerador limita JSON/índice a 64 MiB e catálogo a 100000 registros; uma saída que ultrapassa o limite não substitui o índice anterior. O ensaio físico usa 5000 entradas de metadados próprios, sem criar 5000 ROMs. Não equivale a medir 100000 jogos no aparelho.

No candidato de diagnóstico GBA RV32 da 0.1.16, 32 KiB de rodata reservada mantêm o tamanho canônico na mesma faixa de arredondamento do app4 (4063232 bytes). O parser real do Launcher recusa bytes anexados após o SHA, portanto o espaço pertence ao segmento rodata da imagem e ao seu digest, sem alocação de RAM. A imagem experimental tem 4010656 bytes. O stable de 4017440 bytes não usa esse espaço. A tentativa experimental de 3977888 bytes não foi instalada porque encolheria a reserva; nenhum layout foi alterado para acomodá-la.

## Console N64 0.2.0

O perfil exclusivo elimina a arena de ROM dos outros núcleos. Usa RDRAM de 8 MiB e cache SD de ROM de 64 KiB, sem carregar cartuchos de 32/64 MiB inteiros na PSRAM. CPU/RSP, buffers de vídeo, bateria e cerca de 7 MiB de BSS do RDP também usam o orçamento de PSRAM. As estruturas cacheadas/JIT e as grandes LUTs TLB do core original são removidas somente da cópia de build desse perfil; o diagnóstico antigo e o gerenciador GBA experimental permanecem intactos.

A imagem final mede 4025968 bytes, reserva app4 inalterada de 4063232. Há 1277952 bytes de rodata reservada em flash, pertencentes ao segmento e ao SHA canônico, sem alocação RAM. Isso preserva o arredondamento exigido pelo Launcher inclusive ao trocar para uma imagem menor. Nenhum byte é anexado depois do digest e nenhuma partição é reorganizada. O stable 0.1.17 mede 4033184 bytes.

## Núcleos externos 0.2.1-sd

O combinado anterior com todos os núcleos na flash mediu 4411488 bytes, acima da reserva; não foi instalado. O perfil `sd-cores` exporta cerca de 1,98 MB de código para o cartão e mantém o carregador e a interface residentes. A imagem final mede 4030000 bytes e a reserva continua 4063232, com 1601680 bytes de rodata reservada pertencentes ao SHA. Cada núcleo ocupa o mesmo intervalo PSRAM de código, com capacidade igual ao maior módulo (Mega Drive, 762624 bytes).

No N64, registradores da CPU e os 8 KiB de memória SP ficam em uma pequena alocação interna. RDRAM, cache, framebuffer e bateria continuam na PSRAM. Os 4 MiB auxiliares de cobertura RDP são alocados apenas no boot N64 e liberados na destruição; o prescaler VI de desktop de 1,6 MiB deixa de ser inicializado. Essas alterações pertencem ao novo núcleo N64; o gerenciador GBA RV32 não foi modificado.
