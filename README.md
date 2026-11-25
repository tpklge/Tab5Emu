# Tab5-Emu para o Launcher do Paludo

**Código e binários:** [repositório público](https://github.com/tpklge/Tab5Emu) · [downloads nas Releases](https://github.com/tpklge/Tab5Emu/releases/latest) · [instalação e fontes completos](docs/PUBLICACAO_GITHUB.md).

As contribuições desta integração usam **GPL-3.0-or-later**; os avisos e as licenças próprios dos componentes de terceiros são preservados em `NOTICE` e nos respectivos diretórios.

Implementação incremental baseada em [esp-cpp/tab5-emu](https://github.com/esp-cpp/tab5-emu), fixada em `d120517`. O manual original está em [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md). Créditos do frontend, BSP ESPP e núcleos são preservados; veja [licenças](docs/LICENCAS.md).

O perfil `stable` habilita SNES, GB/GBC, GBA e Mega Drive, com conteúdo externo em **/Tab5-Emu** no microSD e A164 I²C usando **EASD/IOJK/TY**. O marco 0.1.15 acrescenta RTC GB com avanço desligado, MBC2, SMD e controle MD de seis botões. Os cinco sistemas passaram em estados, saves normais e backup; integridade SHA-256 também detecta corrupção sem mudança de tamanho. O retorno direto pela pausa salva antes de sair.

A 0.1.16 trata o atalho de pausa, a posse dos buffers de vídeo, o feeder de áudio e a proteção do cartucho Tekken Special. Acrescenta filtro/ganho estéreo GB e ajustes de renderização GBA/SNES. A validação por jogo e os limites de desempenho ficam em [correções 0.1.16](docs/CORRECOES_0_1_16.md).

A continuação stable acrescenta EEPROM Mega Drive nas placas Sega/EA X24C01 e Acclaim 16M X24C02, com estados v4 e bateria protegida. A [importação de saves antigos](docs/SAVES_ANTIGOS.md) associa explicitamente a ROM, valida o formato e preserva a origem, sem sobrescrever destinos existentes. GBA continua no interpretador; RV32/JIT permanece desativado.

A retomada mantém **GBA no interpretador e RV32/JIT desativado no stable**. Corrupção de heap no experimental é uma pendência conhecida, com testes e dumps preservados. Esta etapa acrescenta mensagens de falha de ROM GB/GBC, validação de seus bancos/mappers, agregação dos controles USB e limpeza de áudio nas transições.

As prioridades altas acrescentam rollback das alocações GB/GBC, leitura verificada de arquivos nos caminhos ativos e mensagens de erro de estado/save no menu de pausa. Falha ao persistir o save cancela a saída e permite tentar novamente. O gerenciamento de memória do RV32 não foi alterado nesta etapa.

O fechamento do stable acrescenta captura USB da pausa/editor, navegação do editor por teclado/keypad, correção dos perfis Mega Drive, consultas ao catálogo completo e launch fora da página visível, avanço RTC MBC3 durante pausas e validação/avisos dos recursos SD. As limitações de compatibilidade e os ensaios físicos pendentes continuam identificados em LIMITACOES.md.

Botões de toque ficam visíveis nas laterais. Busca, favoritos, recentes, fontes OFL externas, temas, preferências JSON, capas assíncronas e política de inatividade estão implementados. O áudio GB recebeu correção de frequência/ritmo e tolerância à variação dos blocos na saída; o usuário confirmou melhora no Pokémon Yellow. Veja [interface](docs/INTERFACE.md), [áudio](docs/AUDIO.md) e [saves](docs/SAVES.md).

A 0.1.17 corrige o indicador vazio da bateria: a HOME e a pausa mostram a estimativa pela tensão do pack de duas células, mantendo tensão/corrente reais no diagnóstico. O perfil experimental separado `n64` (0.2.0-n64) acrescenta boot de ROM, CPU Mupen interpretada, RSP CXD4, RDP em software, vídeo, áudio, controle e save normal. Ele ocupa o mesmo app4 e mostra apenas N64; o `stable` permanece o padrão com os cinco sistemas. Consulte [N64_PORT](docs/N64_PORT.md) para reprodução, evidências e limitações. RV32/JIT GBA e a corrupção de heap conhecida continuam desativados e preservados para diagnóstico.

O perfil `sd-cores` (0.2.1-sd) reúne os sistemas maduros e N64 no mesmo aplicativo, carregando os núcleos do microSD para PSRAM com validação SHA. A interface corrige a barra de paginação sobreposta ao rodapé. Consulte [núcleos no SD](docs/NUCLEOS_NO_SD.md) e [alternativas N64](docs/N64_ALTERNATIVAS.md). O perfil exclusivo anterior `n64` continua disponível para diagnóstico; seus resultados de 0.2.0 são históricos.

A continuação `0.2.3-perf` mantém os seis sistemas no aplicativo geral e acrescenta CPU/áudio N64 otimizados e renderização reduzida opcional. O aplicativo separado **SM64 nativo** atingiu **média de 22,6 FPS no exterior do castelo**, em 80×60 com HUD de 320×240 e lógica a 30 Hz. Outros níveis, abertura e diálogos podem cair abaixo de 20 FPS; os demais jogos N64 continuam experimentais. Os saves antigos ficam preservados, e GBA continua sem RV32/JIT. Medições, configuração e build privado com a ROM do usuário: [desempenho N64](docs/N64_DESEMPENHO.md).

## Build app-only

Use ESP-IDF **6.1**, commit `fff9895c82d744c7237be8847347bdd1b07c6643`, ferramentas ESP32-P4 e dependências fixadas em `dependencies.lock`. O ESP-IDF 5.4.4 usado pelo launcher não compila o BSP atual da base. A instalação local está em `.tools/esp-idf`, separada dos outros projetos.

```sh
# Em outro checkout, inicialize os submódulos necessários:
git submodule update --init components/emu-ui components/emulink components/gbc/gnuboy components/genesis/gwenesis
# Em um checkout novo, use a configuração efetiva registrada (sem credenciais):
cp configs/sdkconfig.0.1.16 sdkconfig
# Ative ESP-IDF 6.1 com export.sh e compile:
bash tools/build.sh stable
```

O alvo utilizado é `idf.py -B build-stable -D TAB5_PROFILE=stable app`. Ele gera **build-stable/tab5-emu.bin**, somente aplicativo. `build` também tenta gerar um bootloader ESP-IDF 6.1 que não cabe no espaço anterior à tabela atual; não é necessário nem deve ser instalado. Não use `idf.py flash` nem o updater upstream no fluxo Paludo.

A CI em `.github/workflows/build.yml` usa o SDK fixado, compila `stable`, executa os testes host e empacota app, recursos SD e fontes. `tools/check_stable_artifact.py build-stable --report artifacts/ci-stable-image.json` verifica a reserva app4 de 4063232 bytes e a exclusão do RV32/JIT e do diagnóstico de heap. A CI não instala no aparelho e seus pacotes ficam marcados como não testados fisicamente.

`TAB5_PROFILE=upstream` mantém os outros núcleos/ports selecionados pela base e exige seus submódulos adicionais. É perfil de investigação, sem validação desta integração. `n64-cpu-probe` troca temporariamente GB/GBC pelo diagnóstico R4300, compartilhando os dados do cartão. Não oferece jogos N64. Detalhes e orçamento em [N64_PORT](docs/N64_PORT.md). `gba-rv32` troca temporariamente SNES pelo tradutor GBA, com escolha antes de abrir o jogo; é opt-in experimental e não é a entrega stable. Veja [comparação e limitações temporais](docs/GBA_PERFORMANCE.md).

`bash tools/build.sh stable-rv32` compila os mesmos sistemas do stable mais a opção RV32. A imagem combinada exige ampliar a reserva app4: veja o [plano revisável](docs/APP4_RV32_0_1_16.md). Compilação e ensaios no perfil menor não certificam o binário combinado. A atualização comum recusa uma mudança de reserva; a autorização original de registrar app4 não é reutilizada para ampliá-lo.

## Preparar o cartão

```sh
python3 tools/library.py --root /caminho/do/cartao/Tab5-Emu init
python3 tools/library.py --root /caminho/do/cartao/Tab5-Emu validate
```

Extraia o ZIP de recursos para que a raiz do cartão contenha `Tab5-Emu/`. O ponto de montagem interno `/sdcard` não vira uma pasta adicional no cartão. O pacote não contém ROMs ou BIOS comerciais. Preserve suas preferências, ROMs e saves ao atualizar; a ferramenta `init` só cria os arquivos que faltam.

Para bibliotecas grandes, gere `cache/library-v1.t5i` no computador com `tools/library_index.py --root /caminho/do/cartao/Tab5-Emu`. A HOME usa páginas de 64 e consulta busca/favoritos/recentes no índice. Veja [bibliotecas grandes](docs/BIBLIOTECAS_GRANDES.md) e [editor de controles](docs/EDITOR_CONTROLES.md).

Tutorial: [adicionar jogos](docs/ADICIONAR_JOGOS.md), [capas e descrições](docs/CAPAS_E_DESCRICOES.md), [controles](docs/CONTROLES.md).

## Atualização pelo launcher

O contrato real foi encontrado em `../Tab5LauncherPaludo` e consultado no aparelho. O launcher ativo respondeu como `0.8.0-rc.2`. Leia [LAUNCHER_PALUDO](docs/LAUNCHER_PALUDO.md). A reserva app4 já foi registrada com autorização explícita; as atualizações usam a mesma reserva e não movem os demais apps.

```sh
.venv/bin/python tools/paludo.py inspect --port /dev/cu.SEUPORT --report /tmp/paludo-inspect.json
# Upload no SD e preparação de plano para uma reserva vazia, sem executar:
.venv/bin/python tools/paludo.py stage --port /dev/cu.SEUPORT --image build-stable/tab5-emu.bin --report /tmp/paludo-plano.json
```

Para atualizar o próprio app já instalado, com backup do binário anterior e preservação de sua reserva/dados:

```sh
.venv/bin/python tools/paludo.py update --port /dev/cu.SEUPORT --image build-stable/tab5-emu.bin --report /tmp/paludo-update.json
```

Porta e endereço são locais/configuráveis; nenhum IP ou token é embutido. `install` recusa plano com reorganização e substituição de outro app. Construído, enviado, recebido, instalado, iniciado e retorno são estados distintos.

A HOME oferece retorno ao Paludo quando detecta seu contrato de boot de uso único. O menu de pausa também oferece retorno direto, persistindo os saves antes de liberar o núcleo. Boot e retorno pela HOME foram exercitados no aparelho; consulte a evidência específica de cada versão.

## Ferramentas e evidências

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements-host.txt
bash tools/test_host.sh
python3 tools/source_bundle.py dist/0.1.16-dev.3/Tab5Emu-source-0.1.16-dev.3.tar.gz
python3 tools/package.py --version 0.1.16-dev.3 --profile stable --image build-stable/tab5-emu.bin --output dist/0.1.16-dev.3
python3 tools/verify_package.py dist/0.1.16-dev.3 --allow-dirty
```

O manifesto é formato próprio de distribuição, não manifesto nativo do Paludo. Traz hashes, requisitos e estado de verificação. `artifacts` contém consultas do launcher, logs dos builds e testes de host. [TESTES](docs/TESTES.md) distingue essas evidências de testes físicos; [GBA_PERFORMANCE](docs/GBA_PERFORMANCE.md) e [N64_PORT](docs/N64_PORT.md) registram as investigações sem atribuir resultados ainda não medidos.

O pacote local `0.1.16-dev.3` do fechamento stable inclui evidência física do binário exato. Ao gerar um pacote novo, o padrão é não atribuir testes físicos; `--evidence` só deve receber o relatório do mesmo SHA. `--allow-dirty` aceita um snapshot de desenvolvimento identificado como tal, preservando as verificações de bytes.
