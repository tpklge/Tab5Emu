# Capas e descrições

O formato JSON abaixo é próprio deste projeto. O upstream usa `metadata.csv` com três campos: caminho da ROM, caminho da capa e nome. O restante da linha após a segunda vírgula pertence ao nome; não existe coluna de descrição nesse CSV.

Exemplo de tutorial no computador, a partir da raiz do projeto:

```sh
python3 tools/library.py --root /caminho/do/cartao/Tab5-Emu init
# Copie sua ROM livre para roms/gba/demo.gba antes dos próximos comandos.
python3 tools/library.py --root /caminho/do/cartao/Tab5-Emu scan --output /tmp/games-novo.json
```

A ferramenta cria arquivos exclusivamente e não sobrescreve um catálogo existente. Revise `/tmp/games-novo.json`, combine seus jogos com o catálogo atual e faça backup antes de substituir `library/games.json`.

Para preparar uma capa local, instale Pillow em um ambiente Python próprio:

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements-host.txt
.venv/bin/python tools/library.py thumbnail minha-capa.png /tmp/demo.jpg
```

Copie `demo.jpg` para `Tab5-Emu/library/thumbnails/`. A ferramenta preserva a proporção, aplica orientação EXIF e gera JPEG baseline com até 360×360 pixels, sem ampliar a imagem original. O limite de arquivo é 256 KiB. O buffer RGB565 de 360×360 ocupa 259200 bytes antes de alinhamento; o worker mantém fila de até quatro pedidos e cache de três entradas. A decodificação JPEG ocorre fora da UI e é pausada nos jogos e durante entrega do SD ao USB. Isso não é medida de pico total de heap; veja [interface](INTERFACE.md).

Edite `Tab5-Emu/library/games.json`, salvo em **UTF-8**:

```json
{
  "schema_version": 1,
  "games": [
    {
      "id": "demo-livre-gba",
      "system": "gba",
      "title": "Ação e exploração",
      "rom": "roms/gba/demo.gba",
      "cover": "library/thumbnails/demo.jpg",
      "description": "Uma demonstração livre com acentos em português."
    },
    {
      "id": "meu-gb-sem-capa",
      "system": "gb",
      "title": "Meu jogo sem capa",
      "rom": "roms/gb/meu-jogo.gb"
    }
  ]
}
```

Use apenas entradas de ROMs que você realmente copiou. Todos os caminhos são relativos a **Tab5-Emu**, sem `/` inicial, `..`, `.` ou barras invertidas. `cover` é opcional. IDs usam letras ASCII, números, `_` ou `-`, com até 80 caracteres, sem repetição. Título: até 160 bytes UTF-8; descrição: até 2048; caminho: até 240. Catálogo: até 1 MiB e 2048 jogos. O schema está em `schemas/games.schema.json`; limites em bytes e verificações de arquivo são adicionais ao JSON Schema.

```sh
python3 tools/library.py --root /caminho/do/cartao/Tab5-Emu validate
```

A validação informa o índice de cada entrada defeituosa. `--no-files` valida um exemplo sem exigir ROMs locais. Para recuperar o catálogo, corrija só as entradas apontadas. No firmware, metadados inválidos caem na descoberta dos arquivos por nome, e capas ausentes usam a alternativa visual da base. Reabra o app para recarregar.

Importação preservando o CSV original:

```sh
python3 tools/library.py --root /caminho/do/cartao/Tab5-Emu import-csv metadata.csv --output /tmp/importado.json --report /tmp/importacao.json
```

Os caminhos são preservados no importador. Se os arquivos antigos estavam na raiz do cartão, copie-os para a nova raiz **Tab5-Emu** ou ajuste os caminhos no JSON antes de validar. O importador não move nem apaga arquivos antigos. Sistemas opcionais não habilitados são registrados como ignorados no relatório.
