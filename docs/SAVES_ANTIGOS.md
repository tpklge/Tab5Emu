# Importar saves antigos

O firmware mantém saves por sistema e SHA-256 do **arquivo da ROM**, para que jogos com o mesmo nome não compartilhem memória. A ferramenta `tools/migrate_saves.py` importa uma cópia de um arquivo antigo para esse namespace. A origem não é removida nem alterada. O destino ocupado, inclusive `.bak`, `.tmp`, sidecar ou lock de uma importação interrompida, é recusado.

Saia do jogo, encerre o acesso do firmware ao cartão e monte o SD no computador. `--root` aponta para a pasta **Tab5-Emu**, não para a raiz do volume. Use a mesma ROM que será carregada no aparelho; renomear a ROM não muda o hash, mas muda o nome dos slots. Arquivos MD e SMD continuam com identidades distintas. A associação é explícita: um save antigo sem hash da ROM não permite provar retrospectivamente a qual jogo pertencia. Guarde o original.

Exemplo de bateria GB:

```sh
python3 tools/migrate_saves.py \
  --root /Volumes/SD/Tab5-Emu --system gb --kind battery \
  --rom /Volumes/SD/Tab5-Emu/roms/gb/Jogo.gb --source /copia-antiga/Jogo.sav
```

Sem `--apply`, o comando apenas mostra o caminho, os tamanhos e hashes, sem criar diretórios ou arquivos. Confira o sistema, a ROM e a origem. Repita o mesmo comando com `--apply` para gravar. A ferramenta confere a cópia e grava `.migration.json` com a procedência. Baterias/RTC recebem o journal SHA `T5B1` aceito pelo firmware. Se a origem já tiver journal, precisa passar nessa verificação, inclusive ao importar uma geração `.bak`. A falta de journal não permite detectar corrupção anterior.

## Formatos aceitos

| Sistema | Bateria | Estado |
| --- | --- | --- |
| GB/GBC | RAM com geometria do header; MBC2 de 512 bytes ou contêiner antigo de 8 KiB; RAM de 2 KiB expandida para o contêiner do port | gnuboy `GbSs=0x105`, com geometria, offsets e tamanho válidos |
| GBA | Contêiner Tab5/gpSP de 128 KiB; EEPROM 512 B/8 KiB, SRAM 32 KiB e Flash 64/128 KiB com opção explícita e assinatura correspondente na ROM | BSON gpSP `0x10004`, 416 KiB, com esquema igual a um estado novo do firmware atual |
| SNES | SRAM convencional LoROM/HiROM, tamanho declarado no header, `--snes-layout lorom` ou `hirom` explícito | Raw dependente das estruturas da build: recusado; importe a bateria |
| Mega Drive | SRAM linear ou EEPROM das placas identificadas, no tamanho exato | Contêiner Tab5 v1–v4 validado contra template atual; raw Gwenesis convertido quando os registros coincidem e a bateria antiga é fornecida |

Em GBA, acrescente `--gba-backup eeprom512`, `eeprom8k`, `sram32k`, `flash64k` ou `flash128k` para formatos menores/externos. A expansão usa `0xff` e não troca a ordem dos bytes. Isso não é um conversor de formatos proprietários de outros emuladores. A ferramenta não adivinha qual variante de EEPROM uma assinatura genérica usava.

Para um estado, use `--kind state --slot 0` (slots 0–5). GB/GBC dispensam template. GBA/MD exigem `--template /copia/estado-novo.sav`, salvo **com a mesma ROM no firmware atual**. O template fornece o esquema; seus dados de jogo não substituem o payload antigo.

Exemplo de conversão de estado raw MD:

```sh
python3 tools/migrate_saves.py \
  --root /Volumes/SD/Tab5-Emu --system genesis --kind state --slot 0 \
  --rom /Volumes/SD/Tab5-Emu/roms/megadrive/Jogo.md \
  --source /copia-antiga/Jogo_0_genesis.sav \
  --template /copia/estado-novo-genesis.sav \
  --battery /copia-antiga/bateria-da-mesma-epoca.sav
```

Em raw MD, a SRAM vem de `--battery`, nunca do template. O protocolo do controle é reiniciado; a preferência de três/seis botões continua a do boot. Raw de outra ABI é recusado. Raw não contém a transação de EEPROM nem o latch Tekken, portanto essas conversões são recusadas. Estados Tab5 desses cartuchos precisam ter o contêiner correspondente. Estados MD v1/v2 existentes continuam aceitos pelo firmware para SRAM linear; o novo esquema v4 se aplica à EEPROM.

RTC MBC3 nativo pode ser importado separadamente com `--kind rtc`: aceita snapshots Tab5 v1 de 32 bytes e v2 de 40 bytes, valida seus campos e escolhe `rtc-v1.bin`/`rtc-v2.bin`. A migração v1→v2 ocorre no boot do firmware. RTC anexado ao save de outro emulador não é reconhecido. RAM e RTC são transações separadas.

Miniaturas e `paused.bin` antigos não são copiados: o slot poderá não ter imagem até ser salvo novamente. Não se procura por basename nem se importa automaticamente na inicialização.

## Interrupção e recuperação

Rode uma importação por vez, com o emulador parado. Arquivos são criados com exclusividade, sem sobrescrever dados existentes; bateria/RTC recebem o journal antes do payload. Um erro normal remove apenas os arquivos criados naquela tentativa. Em queda de energia, podem restar lock e arquivos incompletos. Preserve-os para inspeção e refaça a importação em uma cópia limpa do SD; não apague backups para forçar uma tentativa. FAT e o cartão não garantem uma transação física absoluta. Depois de `--apply`, ejete o volume pelo sistema operacional antes de devolver o SD ao firmware.
