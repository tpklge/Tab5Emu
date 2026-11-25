# Desempenho do N64 no Tab5

O GBA permanece no interpretador; o RV32/JIT e sua corrupção de heap conhecida não fazem parte desta otimização.

## Comparação da emulação geral

A comparação executa a mesma ROM local de SM64 até aproximadamente 180 interrupções VI, no ESP32-P4 a 360 MHz. **VI/s não é FPS do jogo**: o N64 pode gerar várias interrupções para cada imagem nova. Esta cena inicial também é mais leve que a cabeça 3D e uma partida.

| Variante | VI/s no trecho inicial | Ganho |
| --- | ---: | ---: |
| CPU de referência + CXD4 + gráficos Daedalus | 5,855 | 1,00× |
| Áudio ABI1 HLE reconhecido | 6,241 | 1,07× |
| Interpretador com auxiliares integrados | 6,613 | 1,13× |
| CPU e áudio otimizados | 7,144 | 1,22× |
| CPU, áudio e renderização 160×120 | 10,809 | 1,85× |

O núcleo continua interpretando a CPU MIPS; não usa JIT. Tarefas de áudio desconhecidas voltam ao CXD4. Tarefas reconhecidas com dados inválidos são recusadas. A redução de resolução é experimental e altera a imagem; não equivale a compatibilidade total.

`Tab5-Emu/config/n64-performance.cfg` contém três inteiros: CPU rápida (0/1), áudio HLE (0/1), passo de rasterização (1/2). `1 1 2` seleciona a variante mais rápida medida. Para a imagem completa, use `1 1 1`; para a referência, `0 0 1`.

Os testes de host verificaram a equivalência de estado da CPU e imagem em resolução completa. O PCM do áudio HLE não é idêntico ao CXD4: no trecho de 600 VI, diferença RMS 23,26 para sinal RMS 3190,00, SNR 42,75 dB. Isso não substitui uma avaliação acústica nem certifica todos os jogos.

Os seis sistemas passaram por abertura, saída e trocas de núcleos no hardware, incluindo recusa e recuperação de módulo truncado, sem coredump. As ROMs próprias de diagnóstico e os relatórios anteriores foram preservados.

## SM64 nativo, opcional

O perfil separado `sm64-native` compila a lógica do jogo diretamente para o P4, com o renderizador de software do [port ESP32 de SM64](https://github.com/DynaMight1124/retro-go/tree/sm64), revisão fixada em `components/sm64_native/UPSTREAM.json`. Ele aparece como `tab5-sm64` no Launcher e mantém o `tab5-emu` disponível para os demais jogos e sistemas.

O código e os recursos extraídos da ROM ficam em um módulo no microSD, ligado ao hash, tamanho e endereço deste executável. Não há carregamento arbitrário de plugins. Os recursos, a ROM e as imagens geradas ficam locais e não devem entrar nos pacotes públicos.

Preparação com a ROM US correspondente, depois de ativar o ambiente ESP-IDF 6.1 e instalar Pillow no Python usado:

```sh
python tools/prepare_sm64_native.py --rom '/caminho/da/sua/rom.z64'
bash tools/build.sh sm64-native
```

A instalação atual usa o Launcher: aplicativo geral em app0 e SM64 nativo em app1. O build geral foi feito com `-D TAB5_SD_RESERVE_BYTES=0`, retirando a reserva histórica de app4 para caber ao lado do aplicativo nativo; não use um binário maior para atualizar essa reserva sem um novo plano do Launcher.

O perfil nativo tem `sdkconfig` próprio e pilha de 48 KiB. O modelo `sdcard/Tab5-Emu/config/sm64-performance.cfg` seleciona `80 60`. Copie-o ao cartão junto do módulo deste build. O arquivo `Tab5-Emu/config/sm64-performance.cfg` aceita `160 120`, `128 96` ou `80 60`. A interface e os controles continuam em 320×240; o áudio é sintetizado em 16 kHz estéreo. As métricas distinguem ticks de lógica e quadros renderizados.

O save nativo usa `saves/sm64-native/<SHA256>/battery.sav`, com o mesmo journal do frontend. Na primeira abertura, importa os blocos válidos do EEPROM N64 e converte seus campos numéricos. O save N64 original permanece intacto. Savestates não são suportados. Sair ou reiniciar retorna ao Launcher, pois o jogo precisa começar com os globais reinitializados.

A versão final confirmou **média de 22,61 FPS renderizados e exibidos**, com 29,99 ticks de lógica por segundo, no exterior do castelo. O percurso termina no portão, onde outro diálogo foi aberto e a taxa caiu para aproximadamente 18 FPS.

A comparação controlada das resoluções abaixo foi feita no mesmo executável, antes do ajuste do indicador de FPS. Os testes movimentam Mario por vinte segundos após fechar o diálogo inicial; os dois primeiros segundos são excluídos da janela resumida.

| Renderização 3D | FPS exibidos (média) | Faixa indicada durante a janela | Lógica |
| --- | ---: | ---: | ---: |
| 128×96 | 17,51 | 14,7–18,6 | 30,02 ticks/s |
| **80×60, selecionada** | **23,18** | **22,2–24,3** | **30,00 ticks/s** |

O HUD permanece em 320×240; a imagem 3D perde definição em 80×60. O áudio é sintetizado em 16 kHz e enviado à saída de 48 kHz; o produtor não descartou PCM no ensaio. Ambos terminaram sem coredump. A abertura e os diálogos ainda têm quedas, chegando a cerca de 17–18 FPS no ensaio final; não há garantia de 20 FPS em todos os níveis, nem para outros jogos N64. Evidência: `artifacts/sm64-native-performance.json`.

O primeiro ensaio em 160×120 registrou aproximadamente 10 FPS no diálogo inicial. Esse trecho não é diretamente comparável à movimentação da tabela acima. A tabela de VI da emulação geral também não representa FPS do modo nativo.

O save nativo foi persistido e o SHA do arquivo N64 original permaneceu idêntico após o teste. A versão nativa usa retorno ao Launcher ao sair; o aplicativo geral permanece em app0, e o nativo em app1.

## Outras alternativas

O port completo de outra CPU N64 continua sendo um trabalho separado. [N64Recomp](https://github.com/N64Recomp/N64Recomp) exige preparação específica por jogo; [CEN64](https://github.com/n64dev/cen64) e [ares](https://github.com/ares-emulator/ares) priorizam precisão. Não foram medidos no Tab5 e não há resultado que permita prometer 20 FPS com eles. Nesta etapa, as alternativas executadas foram as cinco variantes do núcleo atual e o modo nativo específico de SM64.

O cabeçalho da pausa usa a cadência real de quadros exibidos, com janela de um segundo e reinício após a pausa. A antiga taxa calculada só pelo tempo da cópia de vídeo fica identificada como capacidade de blit no diagnóstico. Os testes cobrem cadência, pausa/retomada e reinício por jogo.

A coleta de requisitos do ESP-IDF não prepara recursos privados: builds de `stable` e dos outros perfis não exigem a ROM SM64. Um teste CMake cobre esse isolamento.

Entrega final, imagens e verificação de saves: `artifacts/n64-performance-delivery.json`. O aparelho foi deixado no Launcher com os dois aplicativos instalados e as preferências de volume/controles restauradas.
