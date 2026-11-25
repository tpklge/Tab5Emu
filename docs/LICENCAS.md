# Licenças e créditos — auditoria inicial

Não se atribui uma licença única a todo o binário. Preservar os avisos por arquivo e distribuir o código correspondente aos componentes habilitados, os scripts de build e os commits das dependências. As contribuições originais desta integração usam GPL-3.0-or-later; os componentes abaixo conservam suas próprias licenças. A publicação fornece os fontes e avisos correspondentes; veja PUBLICACAO_GITHUB.md.

| Componente | Avisos/fontes de licença consultados |
|---|---|
| Frontend tab5-emu, emu-ui, emulink | LICENSE MIT em cada repositório; créditos ESPP/esp-cpp preservados |
| gpSP | components/gba/gpsp/COPYING, GPLv2; adaptações vendorizadas preservadas |
| gnuboy e port | components/gbc/gnuboy/README.md declara GPLv2; conferir avisos de cada fonte |
| gwenesis | LICENSE do fork fixado contém AGPLv3; não assumir licença permissiva pelo nome do núcleo |
| snes9x2005 | copyright reúne frontend MIT, ndssfc GPLv2+ e avisos Snes9x; inclui condições específicas que precisam de revisão antes de redistribuição pública/comercial |
| ESP-IDF/ESPP/LVGL/cJSON e demais managed components | licenças próprias nos pacotes resolvidos; commits/versões em dependencies.lock |
| M5Stack A164 | protocolo consultado no M5Unit-KEYBOARD (MIT); driver equivalente novo, sem importar o framework Arduino |
| HowBoyAdvance | emissor/stub RV32 GPL-2.0-or-later no perfil experimental `gba-rv32`; revisão, origem, licença e mudanças locais em components/gba/rv32/UPSTREAM.json e COPYING. Stable continua no interpretador |
| Mupen64Plus/R4300 | GPL-2.0-or-later por arquivo; interpretador real nos perfis diagnóstico e console N64. Commit e avisos completos em experiments/n64/mupen/UPSTREAM.json, LICENSES e CREDITS |
| Montserrat | SIL Open Font License, fonte original fixada pelo manifesto; subconjuntos externos e licença em assets/fonts |

O manual upstream original fica em `docs/UPSTREAM_README.md`. Imagens de jogos da documentação original não são copiadas ao pacote SD. Não há capas, ROMs ou BIOS comerciais novas na distribuição.

A combinação de licenças de núcleos e dependências não foi declarada adequada para venda ou publicação geral. A publicação inclui fontes correspondentes e preserva os avisos específicos; não constitui uma certificação de compatibilidade para redistribuição comercial. Os avisos originais não são substituídos por este documento.

Adaptações locais GB e Mega Drive ficam fora dos submódulos, com origem indicada nos arquivos. O staging do Musashi limita três fast paths de ROM para respeitar ROM/SRAM, corrige os espelhos da RAM e divide acessos longos em palavras de 16 bits, incluindo o wrap da RAM. Os submódulos originais permanecem fixados. Os testes geradores GB/GBC/GBA/SNES/MD e o programa MIPS do ensaio são próprios 0BSD; não incluem BIOS, logos ou código comercial.

Na 0.1.16, `components/gbc/src/gb_sound.c` deriva do mixer gnuboy fixado e mantém sua GPLv2. `tools/stage_snes_apu.py` adapta no build o SPC original fixado por SHA, sem trocar sua licença. O comportamento do latch do Tekken foi consultado no Genesis Plus GX; nenhum código desse projeto foi incorporado. As referências de hardware e o alcance dos testes estão em CORRECOES_0_1_16.md.

`tools/stage_gb_loader.py` gera uma cópia local do loader gnuboy, fixada por SHA, com verificação de falta de SRAM antes de inicializá-la. `components/gbc/src/gb_memory.cpp` substitui a cola de alocação do port para permitir rollback. Essas adaptações mantêm a licença dos respectivos fontes gnuboy; o submódulo permanece intacto.

O ensaio GBA `tests/homebrew/gba_timing` reutiliza somente assembly e expectativas de temporização da suíte mGBA, revisão `e6942030d25ffe3ba76c72b73a86da073ec857cc`, sob MIT (Jeffrey Pfau). Licença, hashes e alterações do runner estão junto aos fontes. O núcleo mGBA não foi incorporado ao firmware. O runner local é 0BSD e o executável de teste não contém BIOS/logo proprietários.

`tools/stage_audio_policy.py` adapta somente a política temporal do feeder ESPP fixado por SHA, mantendo sua licença e o fonte original intacto. O mesmo staging alimenta firmware e teste host; os stubs FreeRTOS do teste não entram no aplicativo.

`md_eeprom.hpp` implementa originalmente o protocolo dos datasheets Xicor. Somente fatos de mapeamento/códigos de produto foram consultados no Genesis Plus GX; nenhum código daquele projeto foi incorporado. Referências estão em COMPATIBILIDADE_ADICIONAL.md. `tools/build_md_eeprom_rom.py` e as fixtures de migração são originais 0BSD, sem conteúdo dos jogos cujos códigos identificam as placas.

O perfil `n64` inclui CXD4 RSP (CC0; `experiments/n64/plugins/rsp/COPYING`) e Angrylion RDP Plus com avisos MAME históricos, incluindo restrição comercial (`experiments/n64/plugins/rdp/MAME License.txt` e `CREDITS.txt`). Os commits e hashes estão em cada `UPSTREAM.json`. O staging preserva os fontes originais. Não foi concluída a compatibilidade dessa licença com a combinação do core GPL para redistribuição pública; este perfil permanece experimental, com suas licenças e fontes específicas preservadas na publicação. A ROM própria do console e seu gerador são 0BSD. As ROMs fornecidas pelo usuário e suas capturas ficam locais e não integram os recursos distribuídos.

A extração de overlays para o cartão no perfil `sd-cores` não altera as licenças dos núcleos: os módulos são partes da mesma compilação, com fonte correspondente neste checkout. Os avisos Mupen/CXD4/Angrylion e dos núcleos maduros continuam aplicáveis; não se declara concluída a auditoria de distribuição pública.

O porte gráfico Daedalus do candidato 0.2.2-sd preserva os fontes e avisos
GPL-2.0-or-later em `experiments/daedalus/upstream`, com commit e hashes por
arquivo em `UPSTREAM.json`. O backend nativo novo usa a mesma licença.
GLM 1.0.1 (commit `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`) acompanha
`experiments/daedalus/glm/copying.txt` (MIT/Happy Bunny) e seu manifesto.
As adaptações ficam no staging/compat, sem editar os originais. O fallback
Angrylion continua compilado; a pendência de auditoria para distribuição pública
mencionada acima continua válida. A publicação preserva esses avisos e não inclui ROMs ou recursos extraídos de ROMs.

O DSP ABI1 de áudio usa os fontes fixados do Mupen64Plus RSP HLE, com avisos
originais preservados em `experiments/n64/plugins/audio-hle` e adaptações em
`tools/stage_n64_audio.py`. O modo opcional SM64 usa o port ESP32 de
DynaMight1124/retro-go, revisão em `components/sm64_native/UPSTREAM.json`;
seus avisos e `COPYING` permanecem no checkout local fixado. A integração não
inclui os recursos do jogo no repositório: eles são extraídos localmente da
ROM do usuário. Os testes do rasterizador usam geometria e cores próprias.
