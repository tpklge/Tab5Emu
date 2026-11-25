# Correções 0.1.16 — validação por cena

A compilação stable usa o interpretador GBA, CPU de 360 MHz e L2 de 256 KiB. Binário SHA-256 `f52f1ed52346bb6f73843eaab33c59703f82e10011c663ce4dce8e4822655e72`, ELF `ed7911535bfcade16213e04a7a866aaac25b1dba5a2214ea6e3c6eef103ae33c`, 4017440 bytes. Foi instalada pelo Launcher do Paludo na reserva app4 de 4063232 bytes, preservando os demais apps. Volume de teste: 5%. Os resultados abaixo identificam cenas efetivamente jogáveis, não apenas aberturas ou menus.

## Alterações

- Start+Select exige 450 ms e dispara uma vez até soltar ambos. A pausa aguarda liberação de todas as entradas antes da navegação. Menu de toque e Home USB usam bordas; Mode+Start de MD com seis botões continua chegando ao jogo.
- A fila de vídeo retém os buffers pendentes/em conversão até liberá-los. Os núcleos não sobrescrevem um buffer ocupado; pausa e mudanças de geometria drenam a fila. Isso evita imagens formadas com dados de quadros diferentes.
- O feeder espera até 8 ms, em vez de 48 ms, para continuar abastecendo a saída mesmo quando o núcleo atrasa. Mantém DMA de 80 ms a 48 kHz e preenche falta de amostras com silêncio. Isso evita reciclar áudio antigo; não acelera a CPU emulada.
- GB/GBC recebe filtro passa-altas DMG/CGB, ganho NR50 de 1 a 8 e destino estéreo corrigido. A medição do sink não substitui avaliação do som do alto-falante.
- SPC/APU SNES (66624 bytes no P4) e backing de IWRAM GBA (65536 bytes, incluindo tags) tentam RAM interna com margem de 64 KiB e fallback para PSRAM. A saída do jogo libera essas alocações. A adaptação SPC corrige quatro deslocamentos com comportamento indefinido, com origem fixada por SHA. Não muda seu formato de estado.
- Renderização adaptativa pode descartar até três apresentações consecutivas quando isso permite recuperar o prazo; mantém CPU, timers e áudio em todos os quadros. Se a CPU sozinha exceder o orçamento, limita o descarte a um. GBA/SNES usam remoção de frame pointers; SPC usa O3. Não houve overclock.
- Tekken Special recebe o latch de proteção de quatro bits, incluindo acessos de byte usados pelo jogo. Antes, a falha da consulta fazia o próprio jogo selecionar um adversário inválido, corrompendo a descompressão e travando. A implementação independente segue o hardware descrito no [Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/core/cart_hw/md_cart.c). Somente esse cartucho usa estado MD versão 3 para preservar o latch; os demais continuam em versão 2.
- O M68000 aceita os 32 espelhos da RAM em 0xE00000–0xFFFFFF. Fetch/write de 32 bits usa duas palavras de 16 bits, incluindo alinhamento válido de dois bytes e wrap em 0xFFFFFE.
- Consultas Web de progresso podem repetir GET após perda transitória. O instalador nunca repete automaticamente a execução de uma instalação.

## Resultados no stable identificado acima

| Jogo e cena | Quadros emulados/s | Observação no sink em 20 s |
|---|---:|---|
| UMK3, round 1, relógio 99→75 | ~60 | zero faltas e erros |
| Top Gear, corrida com acelerador X | 59,99 | 1481 amostras faltantes; zero erros |
| Circle of the Moon, Nathan controlável na primeira área | 59,74 | zero faltas e erros |
| Harmony of Dissonance, primeira área jogável | 59,69 | 186 amostras faltantes; zero erros |
| Aria of Sorrow, dentro do castelo | 47,49 | 196535 amostras faltantes; zero erros |
| F-Zero GP Legend, corrida | 30,47 | 470574 amostras faltantes; zero erros |

São janelas curtas com quantização dos contadores. A taxa emulada difere da taxa apresentada, especialmente com frameskip. Top Gear foi conferido no framebuffer e no LCD, com a pista/carro íntegros nessa cena. Isso não certifica todas as pistas ou todos os efeitos gráficos.

Tekken: dez capturas em mais de um minuto no stable mostraram avanço do relógio e uma rodada seguinte com adversário íntegro. Salvar/carregar o estado versão 3 também passou. O estado de diagnóstico travado anterior foi preservado em backup local; não foi usado para certificar a correção. Isso não constitui uma campanha completa do jogo.

Pausa: pressão de 200 ms não abriu o menu; manter 800 ms abriu uma vez. Continuar segurando Start+Select+A por dois segundos manteve a pausa. Soltar tudo e pressionar B retomou; repetir o atalho demonstrou rearme. Testes físicos das ROMs próprias GBA/M68000 passaram em entrada, estado, SRAM, geração anterior distinta, boot novo e recusa de estados inválidos.

Host: 26 testes Python, sete suítes C++ e duas suítes C com SPC/M68000 reais passaram com ASan/UBSan. Os testes M68000 incluem dados e execução nos 32 espelhos. O homebrew GBA próprio passou em ALU, código ARM/Thumb modificado e aproximadamente 0,332 iterações por quadro.

## GBA: limite ainda aberto

Aria no castelo e GP Legend continuam lentos, com faltas de áudio. Não estão declarados corrigidos. O pátio inicial de Aria, que alcança a frequência nominal, não é usado para certificar as salas mais pesadas. No GP Legend, um ensaio anterior no menu de ajuste da máquina foi excluído dos resultados de corrida. O diálogo inicial de Circle também foi separado da área controlável.

O comparador anterior encontrou ~0,500 iterações por quadro no RV32 contra ~0,333 no interpretador. Como referência independente, o mGBA host fixado em `feeea8f5c2f53f3fc10e534b54c1b4f7366a42f2` executou a mesma ROM própria: 398 iterações em 1200 quadros, checksum 4368456, máscara SMC zero. Esse resultado é próximo do interpretador e não aprova o RV32. O núcleo mGBA não foi portado para o Tab5.

O novo ensaio aberto de Timer0 contém 19 casos/20 modos da suíte mGBA MIT. A ROM completou no stable, mas o gpSP só atualiza o timer em blocos, sem resolução para essas sequências curtas: a contagem de mismatches não é um diagnóstico de conformidade. O mGBA host obteve 364/380 valores iguais à suíte. As divergências restantes também ficam explícitas; o teste não declara o mGBA um oráculo perfeito.

O próximo candidato RV32 tenta contabilizar esperas de dados dependentes da região em tempo de execução. Continua experimental, separado do stable, sem certificação de jogos ou ganho temporal equivalente. O perfil omite SNES para caber na mesma reserva; o preenchimento interno da imagem é rodata canônica com hash, sem RAM adicional nem bytes anexados ao arquivo. A validação usa o parser real do Launcher. Não altera tabela ou posição dos outros apps.

IRAM de 128/256 KiB e LTO parcial GBA foram experimentados e descartados por falta de ganho suficiente. Capturas, ROMs, estados e dumps pessoais permanecem locais e não entram no pacote. GB/GBC, transições de áudio e retorno final ao Launcher continuam sendo registrados na regressão final; a escuta do usuário é separada dos contadores.
