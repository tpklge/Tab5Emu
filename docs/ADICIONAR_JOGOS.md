# Adicionar jogos

Esta é uma implementação incremental. Confira `PROGRESSO.md` e `LIMITACOES.md` antes de instalar. Não há ROMs nem BIOS comerciais no pacote.

1. Extraia o pacote do SD. Copie a pasta **Tab5-Emu** para a raiz do cartão. Não crie uma pasta `sdcard` no cartão.
2. Copie uma ROM obtida legalmente para a pasta do sistema:

| Sistema | Pasta dentro de Tab5-Emu | Extensões neste perfil |
|---|---|---|
| SNES | roms/snes | .sfc, .smc |
| Game Boy | roms/gb | .gb |
| Game Boy Color | roms/gbc | .gbc |
| GBA | roms/gba | .gba |
| Mega Drive | roms/megadrive | .gen, .md, .smd |
| N64 (perfil experimental separado) | roms/n64 | .z64, .v64, .n64 |

ZIPs de ROMs não são abertos automaticamente. Copie o arquivo descompactado. Evite diretórios com mais de quatro níveis. Sem índice, a descoberta limita cada sistema a 16384 entradas visitadas e a biblioteca a 2048 jogos. Para catálogos maiores, gere no computador o [índice de biblioteca](BIBLIOTECAS_GRANDES.md): até 100000 registros, páginas de 64 na HOME. Gere novamente após mudar ROMs ou metadados.

3. Jogos sem metadados aparecem com o nome do arquivo. Para adicionar título e descrição, veja `CAPAS_E_DESCRICOES.md`.
4. Ejete o cartão pelo computador e insira-o no Tab5. Abra novamente o aplicativo para reconstruir a lista. Não monte o FAT simultaneamente no computador e no firmware. O modo disco é exclusivo: ejete pelo sistema operacional antes de devolvê-lo ao firmware. Identifique o disco do Tab5; não presuma que outro volume USB é seu microSD.

O hash SHA-256 identifica o conteúdo selecionado antes da execução; nomes iguais não compartilham estados. Não renomeie estados entre núcleos. A persistência normal de GBA fica em `saves/gba/<sha256>/battery.sav`; os estados ficam em `states/<sistema>/<sha256>/core-v1/`. A string `core-v1` é o ABI desta primeira integração, não promessa de compatibilidade com futuras atualizações dos núcleos.

gpSP oferece a BIOS livre embutida na base. Uma BIOS própria pode ser colocada em `bios/gba_bios.bin`, sem distribuição junto do app. Compatibilidade por título e mensagens específicas de BIOS ainda precisam de teste físico. ROM rejeitada ou ilegível deve ser retirada do catálogo; o frontend retorna à biblioteca se não conseguir calcular sua identidade.

Para atualizar recursos, faça backup de `config` e `library`. Copie apenas arquivos novos; revise manualmente conflitos. Nunca substitua `roms`, `saves`, `states`, capas próprias ou preferências por uma extração com sobrescrita automática.

N64 exige compilar/instalar o perfil `n64`; o stable continua exibindo apenas seus cinco sistemas. Veja N64_PORT.md.
