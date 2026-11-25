# ROMs de diagnóstico — fontes e licenças

`tools/build_test_roms.py` compila a ROM GBA destes fontes. `tools/build_8bit_test_roms.py` contém os fontes em opcodes comentados e o montador mínimo das ROMs GB e SNES; gera os binários sem toolchain externo. As ROMs originais destes três geradores são distribuídas sob 0BSD. Binários/manifests ficam em `artifacts/test-roms` e não são incorporados à flash do app.

GB: 32 KiB, MBC1+RAM+bateria, SRAM 8 KiB. A cada boot habilita RAM, lê a assinatura 0x54 e incrementa o segundo byte; primeiro boot escreve `[0x54,1]`. Exibe um padrão de tiles baseado no contador e entra em loop. Sem logo proprietário; exige boot direto no emulador, não é teste de boot de hardware original.

SNES: 32 KiB LoROM, RAM+bateria 8 KiB, NTSC. Faz o mesmo contador em 0x700000/0x700001 e usa o contador no backdrop. Código original 65C816, cabeçalho/checksum/vetor de reset gerados. Não contém gráficos ou código comerciais.

Esses contadores validam gravações feitas pela CPU emulada e continuidade entre sessões, sem uma API de debug injetando bytes em RAM. Não constituem suíte de compatibilidade dos consoles ou de todos os tipos de cartucho.

O diretório `gba_timing` acrescenta um runner próprio 0BSD e um subconjunto separado da suíte mGBA (MIT), fixado em `mgba-suite/UPSTREAM.json`, com licença integral. `tools/build_gba_timing_rom.py` gera uma ROM sem BIOS/logo, com 19 casos e 20 modos por caso. São medidas de Timer0 da CPU guest, comparadas com os valores da suíte, não FPS do Tab5. `tools/probe_gba_timing.py` substitui temporariamente somente a ROM T5RV própria, conferida por SHA, e a restaura com releitura; não altera o catálogo. Mismatches retornam código diferente de zero. Compilar esse ensaio ou completá-lo no emulador não significa passar em conformidade.

`tools/build_n64_console_rom.py` gera o programa original 0BSD N64, sem IPL proprietário. Testa vídeo RDP/VI com cores uniformes, A/analógico via Joybus, DMA PI/SP/SI/AI, áudio e contador de boot em EEPROM. `tests/test_n64_console.c` exercita o console real com ASan/UBSan, incluindo FlashRAM. O programa MIPS headless anterior permanece independente.
