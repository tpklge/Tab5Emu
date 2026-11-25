# Arquitetura incremental

Base `esp-cpp/tab5-emu` no commit `d120517` (SHA completo em `artifacts/audit/upstream-commit.txt`). Fonte original, UI e núcleos maduros foram preservados. `docs/UPSTREAM_README.md` mantém o manual original; suas instruções de flash completo não se aplicam ao launcher pessoal.

- `components/tab5-emu`: BSP ESPP, energia/INA226, touch, áudio, PPA, USB; resolvedor `content_paths.hpp`; serviço A164 em `keyboard_i2c.cpp`; persistência e identidade SHA-256; verificação do retorno ao Paludo.
- `components/rom_info`: catálogo JSON limitado, isolamento de metadados defeituosos e descoberta de ROMs.
- `components/emu-ui`: submódulo UI original, não alterado; sistemas no topo, lista à esquerda, capa e detalhes à direita. Foi inspecionada a imagem `docs/images/launcher.jpg` antes da alteração.
- `components/gui`: ligação da biblioteca/UI, descrições no campo de subtítulo, diagnóstico SD/teclado/tensão e configurações. Capas usam worker de prioridade 2/core 0 com stack de 6144, fila limitada e cache de três entradas; a tarefa para nos jogos e na entrega ao USB. Fontes extras e presets de tema vêm do cartão.
- `main`: ciclo de vida dos carts e menu de pausa herdados, seleção de perfil do teclado e estados por hash real da ROM.
- `components/gba`: adaptador libretro pequeno; gpSP interpretado, BIOS livre; persistência normal e estados GBA com arquivo temporário e geração anterior.
- `tools`: catálogo, miniaturas, build, pacotes e integração externa, separados do frontend.

O perfil padrão `stable` inclui SNES, GB/GBC, GBA e Mega Drive. `TAB5_PROFILE=upstream` preserva a seleção original dos demais núcleos/ports para investigação, não uma certificação de funcionalidade na biblioteca JSON nova. Nenhum port PC/PSX grande é acrescentado ao perfil padrão.

O BSP continua sendo o dono do barramento interno I²C HP0, GPIO31/32. A164 usa LP I²C separado GPIO0/1, sem reconfigurar um barramento já ocupado. A tarefa de entrada tem prioridade 5, stack 4096, sem afinidade fixa. O loop principal mantém a prioridade 20 herdada. Vídeo e áudio mantêm a distribuição original; detalhes de afinidade dependem da configuração resolvida e devem ser registrados no teste físico. Não foi feita paralelização especulativa do emulador.

PPA escala/converte imagens 2D; não é GPU 3D do N64. Os núcleos continuam código compilado na flash, com overlay real copiado para PSRAM pelo mecanismo existente da base. Nenhum arquivo de núcleo no SD é apresentado como módulo carregável.

A saída de áudio roda no core 1/prioridade 20, 48 kHz, sem gravação de microfone. `audio_probe.cpp` mede stream/I2S por wrappers do linker filtrados por tarefa; capturas intencionais e menus desativam a expectativa de fluxo. `inactivity.cpp` usa tarefa de prioridade 1/stack 3072 e intervalo de 50 ms para o backlight. Não suspende o guest.

`battery_store.cpp` é residente e compartilhado, com journal SHA write-ahead, formato raw e geração anterior. Isso evita registrar destruidores globais em overlays a cada troca de ROM. As adaptações GB/MD ficam em fontes locais; o staging Musashi e o header de timing de áudio são gerados e conferidos sem editar submódulos ou managed components.

O perfil N64 diagnóstico usa o interpretador puro Mupen64Plus e acesso KSEG0/1 limitado ao ensaio. O programa vem do SD, com tamanho/hash exatos; relatório também no SD. Em stable, esse código não é ligado. Não existe carregador de código arbitrário do SD.
