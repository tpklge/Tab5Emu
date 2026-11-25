# Publicação no GitHub

O repositório público é https://github.com/tpklge/Tab5Emu. A integração e as contribuições originais do projeto usam **GPL-3.0-or-later**. O código upstream e as dependências mantêm seus avisos e licenças; consulte `NOTICE`, `LICENSES/Tab5Emu-upstream-MIT.txt` e `docs/LICENCAS.md`.

A release inclui o aplicativo geral `0.2.3-perf`, seus cinco módulos de núcleos para o microSD, os recursos da interface, o aplicativo `tab5-sm64` e os fontes com dependências. Os binários são somente aplicativo: instale pelo Launcher do Paludo, usando o plano oferecido por ele. Não grave o bootloader ou a tabela de partições a partir destes arquivos.

## Instalação

1. Baixe os arquivos na página **Releases**, incluindo `Tab5Emu-microSD-v0.2.3.zip`.
2. Copie `Tab5-Emu/cores` e `Tab5-Emu/assets` para o microSD. Em uma instalação existente, preserve configurações pessoais, catálogo, ROMs, saves e estados.
3. Copie o `.bin` do aplicativo geral para a pasta `APP` do cartão e instale pela interface do Launcher.
4. Adicione suas ROMs às pastas `Tab5-Emu/roms/gb`, `gbc`, `gba`, `snes`, `megadrive` ou `n64`.

O aplicativo geral mantém GBA no interpretador e RV32/JIT desativado. N64 geral continua experimental. O modo nativo de SM64 alcançou aproximadamente 23 FPS no trecho testado em 80×60, com quedas em diálogos. Veja `N64_DESEMPENHO.md` para os limites e as medições.

## SM64 nativo

O módulo de aproximadamente 11 MB contém gráficos e áudio extraídos da ROM e **não integra a publicação**. O `.bin` nativo incluído identifica a versão testada, mas precisa de um módulo do mesmo build para abrir o jogo. Para jogar, gere e instale seu próprio par aplicativo/módulo:

```sh
git clone --recurse-submodules https://github.com/tpklge/Tab5Emu.git
cd Tab5Emu
# Ative o ESP-IDF 6.1 fixado no projeto e instale os requisitos Python.
python tools/prepare_sm64_native.py --rom '/caminho/da/sua/rom-US.z64'
bash tools/build.sh sm64-native
```

Instale `build-sm64-native/tab5-sm64.bin` pelo Launcher, copie o módulo de `build-sm64-native/sd/Tab5-Emu/cores/` e o modelo `sdcard/Tab5-Emu/config/sm64-performance.cfg` para as mesmas pastas no cartão. Aplicativo e módulo devem vir da mesma compilação; o carregador verifica o hash. A preparação usa a dependência fixada em `third_party/sm64-native-reference` e trabalha em `.tools`, mantendo as fontes originais intactas.

## Fontes e reprodução

O clone recursivo preserva todos os commits das dependências. O arquivo `Tab5Emu-source-complete-v0.2.3.tar.gz` contém o projeto, todos os submódulos de primeiro nível inicializados e as dependências gerenciadas da compilação. O manifesto interno registra hashes e revisões. Dependências aninhadas de ports opcionais não habilitados podem exigir inicialização adicional; o SDK e as ferramentas são instalados separadamente.

O aplicativo geral publicado usa:

```sh
idf.py -B build-sd-cores -D TAB5_PROFILE=sd-cores -D TAB5_SD_RESERVE_BYTES=0 app
```

A reserva histórica de app4 não faz parte desse binário. Consulte sempre o plano atual do Launcher antes de instalar.

A publicação contém testes e evidências sanitizadas. ROMs, saves pessoais, capturas de jogos, dumps do aparelho, credenciais, arquivos `.local.*` e recursos gerados da ROM SM64 permanecem locais. `SHA256SUMS` permite conferir os downloads.

O workflow `Paludo stable app-only` aceita `release_tag=v0.2.3` no disparo manual para preparar os arquivos de uma release existente em rascunho. Depois de compilar e testar o stable, ele reúne todos os submódulos fixados e os fontes gerenciados, recupera os binários e núcleos já conferidos no aparelho e gera o pacote completo no GitHub. O processo mantém a release em rascunho para permitir a conferência dos hashes antes da publicação.
