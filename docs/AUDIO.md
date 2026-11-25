# Áudio GB e saída I2S

O relato de estalos/cortes do Pokémon Yellow levou a duas correções no adaptador, sem alterar a ROM. O mixer gnuboy produzia amostras com uma frequência diferente dos 32768 Hz declarados ao resampler. Agora usa 32768 Hz exatos, com capacidade estéreo par. O loop espera pelos ciclos realmente executados pela CPU, com deadline fracionário; contar iterações como quadros não funciona durante períodos de LCD desligado. O turbo oculto em X foi removido.

A saída do BSP também precisava absorver variações na chegada dos blocos. A política do feeder fixado recebe uma adaptação reproduzível em `tools/audio_policy.cmake` e `tools/stage_audio_policy.py`. A espera de 48 ms das versões até 0.1.15 passou por 8 ms no desenvolvimento 0.1.16. O fechamento usa 16 ms, abaixo de um bloco de 1/60 s, para absorver jitter de um produtor em fluxo; DMA permanece 16×240 frames (80 ms a 48 kHz). O código original e seu SHA são conferidos antes de gerar o header no build; os managed components não são editados. O aumento histórico da reserva DMA custa aproximadamente 4 KiB internos e acrescenta até 20 ms à profundidade anterior da saída; não é medição ponta a ponta da latência.

`audio` mede produtor/resampler. `audio_sink` intercepta os recebimentos do stream e escritas I2S **da tarefa de saída**, contando blocos parciais/vazios, frames preenchidos com silêncio, erros e maior intervalo entre escritas. Menus, capturas e aquecimento ficam fora da expectativa de fluxo contínuo. Esses contadores observam a alimentação por software; não medem acusticamente o alto-falante.

Na 0.1.16, `late_blocks` e `late_frames` observam atrasos entre inícios de escrita maiores que um bloco mais 2 ms. São atrasos do escritor por software, não contadores de underrun do periférico: jitter normal pode incrementá-los. A contagem anterior de zero frames faltantes não bastava para diagnosticar jogos lentos: esperar 48 ms por bloco podia atrasar o próprio escritor, que então registrava somente os blocos que conseguia completar. A espera menor permite continuar substituindo a saída por silêncio quando faltam amostras, evitando a repetição de descritores antigos; não torna um núcleo lento capaz de produzir áudio em tempo real.

O mixer GB/GBC local acrescenta o filtro passa-altas descrito no [Pan Docs](https://gbdev.io/pandocs/Audio_details.html), com constantes distintas para DMG e CGB na frequência real de 32768 Hz. O volume NR50 usa ganho 1–8; campo zero não desliga o canal. O estado do capacitor é reiniciado ao reiniciar/carregar um estado do núcleo. Testes de host verificam degrau DC, decaimento, separação estéreo e retenção do capacitor com os DACs desligados; isso não certifica toda a APU gnuboy.

Na retomada 0.1.16, pausa/carga de estado/saída descartam PCM ainda na fila de software e o histórico de interpolação, com o produtor parado. Os contadores da sessão permanecem cumulativos; apenas uma nova sessão os reinicia. Submissão e descarte são serializados. O caminho verificado de load GB/GBC também zera o PCM parcial e o capacitor, que antes eram reiniciados somente pelo loader legado. A fila DMA já enviada pode terminar seu bloco; isso não promete silêncio instantâneo nem elimina todo estalo acústico.

| Build | Intervalo | Ritmo CPU/tempo real | Blocos parciais/vazios | Frames faltantes |
|---|---:|---:|---:|---:|
| 0.1.9, espera anterior | 31,333 s | 1,000143 | 2 / 0 | 267 |
| 0.1.10, feeder ajustado | 46,494 s | 0,999900 | 0 / 0 | 0 |
| 0.1.12, regressão | 31,343 s | 0,999806 | 0 / 0 | 0 |
| 0.1.13 | 31,435 s | 1,000076 | 0 / 0 | 0 |
| 0.1.14 | 31,237 s | 1,000158 | 0 / 0 | 0 |

A 0.1.12 escreveu 1881 blocos no intervalo, sem erros de escrita I2S. O usuário relatou que os estalos sumiram ou diminuíram bastante após a correção de ritmo na 0.1.8. A medição posterior apoia a melhora, sem alegar exatidão completa da APU ou ausência de falhas em todos os jogos. A ROM, suas capturas e dados brutos pessoais permanecem privados.

Na medição final 0.1.13, 1886 blocos passaram pela saída sem frames faltantes ou erros. Depois, o estado de slot 4 preservado antes das atualizações foi carregado; o jogo ficou aberto no stable com os controles laterais visíveis. A captura e o load ocorreram após o intervalo medido.

Na 0.1.14, o slot 3 capturado antes da atualização foi restaurado e mediram-se 1874 blocos sem faltas ou erros, com coredump final vazio. `artifacts/audio-gb-v0.1.14.json` vincula a medição ao ELF exato; não substitui avaliação acústica. O Pokémon ficou aberto no stable.

## Continuação stable 0.1.16

O prazo de 8 ms podia encerrar a espera antes de um produtor completar o bloco seguinte. O fechamento usa 16 ms, abaixo do intervalo de 1/60 s, com o mesmo DMA de 80 ms. O SHA do feeder original é conferido; `stage_audio_policy.py` gera o header tanto para o build como para a suíte host. O feeder real absorve a chegada simulada em 12 ms, conserva o prazo total diante de notificações antigas e preenche silêncio quando o produtor continua atrasado. Não altera o relógio emulado nem torna um núcleo lento capaz de produzir em tempo real.

| ELF / espera | Janela do tom GBA próprio | Blocos | Parciais | Frames de silêncio | Erros de escrita |
|---|---:|---:|---:|---:|---:|
| 0a1de4ab / 8 ms | 20,016 s | 1201 | 1 | 141 | 0 |
| 844acdd8 / 8 ms | 20,014 s | 1201 | 1 | 228 | 0 |
| 21775c87 / 16 ms | 20,021 s | 1201 | 0 | 0 | 0 |

As asserções estritas de zero faltas nas duas primeiras janelas falharam; relatórios e ELFs foram preservados. A janela final passou `smoke_gba_session.py --require-zero`, incluindo parada do produtor por dois segundos de pausa e persistência GBA. São execuções distintas de uma fixture própria, sem avaliação acústica nem certificação de toda a APU ou de todos os jogos. Não se transfere o zero do binário final para os candidatos anteriores, nem se declara o desempenho de cenas pesadas resolvido. A maior lacuna de submissão após retomar foi 22307 us, excluindo a pausa.

## Stable 0.1.15

Após restaurar as preferências e o estado preservado do usuário, o Pokémon Yellow rodou por 30,014 s: 1801 blocos, zero blocos parciais/vazios, faltas de frames ou erros de escrita. O jogo foi restaurado novamente ao estado preservado e deixado pausado. Coredump vazio; ELF `360a8d438ebfe5eb0bfc82ee7d3084b17d87a57bffd747ee3e37d073c5456c64`. É medida do sink nesse intervalo, sem certificação geral da APU. Evidência: `artifacts/hardware-verification-v0.1.15.json`.

## AI N64 experimental

O novo perfil lê os buffers estéreo de 16 bits da RDRAM pelos registradores AI e fornece a taxa do DAC ao feeder existente, com conversão para a saída física de 48 kHz. Os testes próprios conferem DMA e entrega de amostras; títulos reais também submetem áudio. A conclusão da transferência/FIFO AI ainda usa um modelo simplificado. CPU/RSP/RDP de software abaixo do tempo real deixam intervalos sem amostras: produção de áudio não certifica continuidade, sincronismo ou qualidade acústica.

## Candidato 0.2.2-sd — início GBA

O adaptador GBA acumula 50 ms de PCM estéreo a 32768 Hz antes do primeiro envio
ao resampler/DAC. As amostras são preservadas em ordem, sem silêncio artificial,
mudança do relógio ou supressão da introdução. A reserva é limitada a 2048 frames;
no uso GBA são 1639 frames (cerca de 6,4 KiB). Esse adiantamento da fila custa
aproximadamente 50 ms de atraso inicial do áudio e não absorve travamentos
maiores indefinidamente. Reset, carga de estado, pausa e troca de cartucho
reiniciam a coleta pela geração da linha do tempo; mute descarta somente a
coleta que não será reproduzida. FPS e política de renderização permanecem.

`audio_sink.startup_blocks/startup_partial_blocks/startup_empty_blocks/
startup_missing_frames/startup_write_errors` medem os seis primeiros blocos
após começar/retomar o fluxo. Os contadores de fluxo contínuo anteriores são
preservados, mas o aquecimento passa a ser observável separadamente.
`test_audio_preroll.cpp` verifica ordem/conteúdo completos, lotes parciais,
capacidade e reset com ASan/UBSan. A suíte existente do feeder também passa.
No ELF `b2000f29…`, instalado no app0, o tom próprio manteve 59,77 VI/s e zero
faltas/erros em uma janela de 10,00 s (600 blocos). Os seis primeiros blocos
registraram um vazio de 800 frames, agora exposto; isso não é medição acústica.
Circle of the Moon iniciou os primeiros cinco segundos sem faltas no contador
contínuo, mas a introdução seguinte caiu para cerca de 48,81 VI/s: houve 87816
frames ausentes em 10,00 s (112 blocos parciais, sem erros de escrita). Não houve
leituras de ROM durante essa janela: o déficit observado é de execução, não um
atraso do SD naquele intervalo. O preroll não resolve lentidão sustentada.
A qualidade acústica não está certificada e esse caso continua pendente.
Evidência: `artifacts/daedalus-hardware-verification-v0.2.2.json`.
