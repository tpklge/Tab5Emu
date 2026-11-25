# Saves e estados — 0.1.15

A identidade é o SHA-256 dos bytes da ROM, não seu nome. O ponto de montagem interno é `/sdcard`; no cartão as pastas começam em `Tab5-Emu/`. Renomear uma ROM não muda seu namespace; outra revisão do jogo terá outro hash. Não há migração automática dos saves por basename do upstream. Para importar explicitamente os arquivos antigos, veja [SAVES_ANTIGOS.md](SAVES_ANTIGOS.md): simulação, validação, cópia sem sobrescrita e preservação da origem.

| Sistema | Save normal | Estado |
|---|---|---|
| GB/GBC | `saves/gb ou gbc/<sha>/battery.sav`, RAM em bancos de 8 KiB quando o cartucho tem bateria | formato gnuboy, tamanho/header conferidos antes de carregar |
| GBA | `saves/gba/<sha>/battery.sav`, raw de 128 KiB exposto pelo gpSP atual | formato gpSP, tamanho conferido |
| SNES | `saves/snes/<sha>/battery.sav`, tamanho SRAM do header, até 128 KiB | formato do núcleo, tamanho conferido |
| Mega Drive | `saves/genesis/<sha>/battery.sav`, SRAM linear até 128 KiB ou EEPROM identificada de 128/256 B | v2 para SRAM (aceita v1), v3 para latch Tekken, v4 para EEPROM; hash ROM, SHA do conteúdo e schema Gwenesis |

Estados ficam em `states/<sistema>/<sha>/`; os slots são específicos do núcleo e sua ABI. GB/GBC, SNES e Mega Drive restauram a SRAM incluída no estado. O gpSP atual **conserva a SRAM mais recente** ao carregar um estado antigo de CPU/PPU. Essa diferença foi testada e deve ser considerada ao voltar no progresso de um jogo.

A gravação usa temporário, verificação de escrita/tamanho, `fflush`, `fsync` e renomeação com backup. A geração anterior distinta fica em `.bak`; flush de bytes idênticos não substitui esse backup. O save normal é descarregado antes de sair do jogo. Se o flush falha, a saída é recusada para permitir nova tentativa.

No carregamento de save normal, tamanho incorreto ou checksum incompatível do primário provoca tentativa do `.bak`. Ao salvar depois de recuperar o backup, o primário recusado é preservado em `.rejected`. Um `.rejected` já existente não é substituído: arquive-o manualmente antes de uma nova recuperação. Se ambas as gerações existem mas são inválidas, o jogo é recusado; não se grava RAM inicial sobre elas. Faça cópias antes de qualquer reparo manual. Os bytes da RAM continuam raw. Desde 0.1.12, o sidecar `battery.sav.sha256` registra até três hashes SHA-256: escrita pendente, primário válido e backup válido. Ele é publicado antes dos dados; uma interrupção entre as duas publicações ainda permite reconhecer as gerações anteriores. O sidecar tem seu próprio `.bak`; metadados inválidos não desativam silenciosamente a verificação. Na recuperação, o sidecar recusado também pode virar `.sha256.rejected`.

Saves legados sem nenhum sidecar são aceitos pelo tamanho e recebem os hashes no primeiro flush, sem rotacionar dados idênticos. Não é possível detectar corrupção já presente antes dessa importação inicial. Preserve save, backup e sidecars juntos; removê-los faz o arquivo parecer legado. Campos inválidos em ambas as gerações de metadados fazem o carregamento falhar. A integridade não converte o FAT numa transação física garantida em perda de energia.

Cartuchos GB com RTC usam `rtc-v2.bin`, snapshot de 40 bytes com timestamp do relógio físico confiável. Lê/migra `rtc-v1.bin` (32 bytes), preservando o original, e avança pelo intervalo desligado quando os timestamps permitem. Carry de 512 dias e halt são preservados. MBC2 mantém o contêiner legado de 8 KiB, com 512 nibbles significativos. Veja [compatibilidade adicional](COMPATIBILIDADE_ADICIONAL.md). RAM e RTC são duas transações independentes, sem atomicidade conjunta.

Os testes físicos usaram ROMs próprias 0BSD de GB, GBA e SNES e seus saves descartáveis. Confirmaram boot novo, estado, backup distinto e recuperação após truncamento; GBA também recusou duas gerações inválidas sem sobrescrevê-las. Dois estados GB/SNES do usuário anteriores à atualização abriram e continuaram executando. ROMs originais foram relidas e comparadas por SHA; não foram alteradas.

Isso não equivale a ensaio de corte de energia ou SD cheio. Não há garantia física de atomicidade do FAT/controlador em perda de alimentação. EEPROM/Flash específicas de títulos e compatibilidade geral permanecem pendentes. Nunca use seu único save para testes de falha.

## Mega Drive

O adaptador reconhece SRAM linear, byte lanes pares/ímpares ou ambas, enable/write-protect em A130F1 e o tamanho indicado no header. ROMs big endian `.md`/`.gen` e SMD validado/desentrelaçado são aceitos. MD e SMD têm hashes distintos e não unem saves implicitamente. Headers fora dos limites e EEPROMs desconhecidas são recusados; as placas Sega/EA X24C01 e Acclaim 16M X24C02 estão implementadas e usam bateria raw no tamanho do chip. Acclaim 32M/J-Cart e outros mappers continuam pendentes. Seis botões é opção aplicada no boot. Estados v2 preservam o handshake; se o modo de controle atual for diferente, o handshake reinicia, mantendo a preferência do boot. O v4 preserva também a transação EEPROM pendente, sem mudar o formato SRAM anterior. Aplicativos antigos não abrem o novo esquema; o app atual continua aceitando v1/v2 para SRAM linear.

Antes do loader de estado Gwenesis, são conferidos tamanho exato, versão, identidade da ROM, SHA do header/payload, capacidade SRAM e sequência completa de nomes/tamanhos dos registros do serializador atual. Um estado recusado não altera o guest. Ponteiros YM2612 são reconstruídos pelo núcleo fixado. O container novo não importa automaticamente estados raw antigos de Mega Drive.

A ROM própria `ec7643d99287d7fd16eed01b1cb69bf2783ec676f199ef16a6c18e97feb197c2` executa instruções 68000, incrementa a SRAM no boot/Start e escreve a cor do VDP. Na 0.1.13, CPU/vídeo/SRAM restauraram juntos; save em boot novo e backup distinto passaram. Estados truncados, com SHA errado, de outra ROM e com schema alterado foram recusados. Estado observado: 155270 bytes.

GB Color também foi exercitado com ROM própria CGB/MBC1, paleta colorida, estados e SRAM. Na 0.1.12, alteração de bytes mantendo o tamanho foi detectada e recuperada para GB/GBA/SNES/Mega Drive; a regressão 0.1.13 inclui GBC. Os arquivos recusados das ROMs de teste foram preservados, com arquivo e releitura antes de repetir testes. Não se testou corrupção em saves comerciais.

## N64 experimental

No perfil `n64`, a persistência normal fica em `saves/n64/<SHA-256 da ROM>/battery.sav`, com journal SHA e geração `.bak` do BatteryStore. O contêiner próprio tem 198656 bytes: EEPROM (2048), SRAM (32768), FlashRAM (131072) e Controller Pak (32768), nessa ordem. A saída e o reset persistem antes de destruir o core; falhas de gravação cancelam a saída. Não importar diretamente `.eep/.sra/.fla/.mpk` de outros emuladores: conversão ainda não implementada. O formato depende da identidade exata da ROM, incluindo sua ordem de bytes. Estados instantâneos são recusados explicitamente.
