# Controles

O firmware usa eventos físicos do A164, I²C `0x6D`, Ext.Port1: SDA GPIO0, SCL GPIO1, INT GPIO50. O modo Normal expõe pressionar/soltar por posição de matriz; não há repetição de caracteres no estado de jogo. A tarefa consulta até 31 eventos a cada 8 ms usando LP I²C; o BSP é dono do barramento HP interno. INT não é usado nesta primeira versão. Endereço e matriz foram conferidos no driver oficial M5Unit-KEYBOARD `003a83e98a5b0b0dbd975bc4e77e52f0d4e73973`.

| Função | SNES | GB/GBC/GBA | Mega Drive |
|---|---|---|---|
| Cima/esquerda/baixo/direita | E/A/S/D | E/A/S/D | E/A/S/D |
| A | O | O | J |
| B | K | K | K |
| X | I | livre | — |
| Y / C | J (Y) | livre | O (C) |
| Start | T | T | T |
| Select | Y | Y | — |
| L/R | U/P | U/P no GBA | — |

Setas também navegam. Na HOME: O/T/Enter confirma, K/Esc volta/abre configurações, I abre seleção de sistemas. Durante jogo, Esc no A164, touch e BOOT mantêm acesso ao menu; T+Y também solicita a pausa nos sistemas com esses botões. USB conserva suporte da base. Direções opostas são neutralizadas no mapeamento do teclado; diagonais permanecem possíveis.

A partir da 0.1.16, Start+Select (T+Y no mapa padrão) exige 450 ms contínuos. Aciona uma vez e só rearma depois de soltar ambos. Ao abrir a pausa, a navegação aguarda a liberação de teclado, gamepad e toque, para que o Start ainda pressionado não confirme imediatamente Retomar. O botão Menu de toque e Home USB também usam a borda de pressão. Mode+Start do Mega Drive de seis botões conserva sua função no jogo.

Falha de comunicação, desconexão ou FIFO cheio limpa as teclas do A164. Após recuperação, solte e pressione novamente. A tela de teste em Configurações mostra códigos HID; E=08, A=04, S=16, D=07, I=0C, O=12, J=0D, K=0E, T=17, Y=1C, U=18, P=13 (hexadecimal).

Testes de host verificaram a lógica E+A+I+O+J+K+T+Y, liberação e conflitos. **Teste físico realizado:** E+A+O+K foi observado junto por 2,554 s; T+Y por 0,554 s, com release registrado. W isolado não acionou cima. Oito teclas, outras combinações/ghosting e hotplug continuam sem validação.

`config/input_profiles.json` é configuração ativa a partir do firmware **0.1.2**. O arquivo é lido ao entrar/sair do jogo. Não altere o cartão pelo computador enquanto o firmware ainda tem o FAT montado; use o canal de arquivos do emulink ou o Launcher. O Mega Drive usa três botões por padrão; a opção de seis botões vale no próximo boot do jogo.

Precedência: padrões compilados → `global` → `systems.<sistema>` → `games.<SHA-256 real da ROM>`. Sistemas: `snes`, `gb`, `gbc`, `gba`, `megadrive`; GBC herda `gb` se não tiver seção própria. O formato antigo com sistemas diretamente na raiz também é aceito, preservando os arquivos já provisionados. O perfil separado `n64` acrescenta a chave `n64`.

```json
{
  "schema_version": 1,
  "global": {"menu": "F1"},
  "systems": {"gba": {"l": "Q", "r": "R"}},
  "games": {}
}
```

Para um jogo específico, acrescente em `games` uma propriedade cujo nome seja os 64 caracteres do SHA-256 da ROM e cujo valor contenha as ações a substituir. O hash vem do conteúdo, não do nome ou do campo opcional do catálogo. Exemplo do valor: `{"a":"F","b":"G"}`. Campos ausentes conservam a camada anterior; `"NONE"` desliga uma ação. Para restaurar tudo, use `{"schema_version":1}` ou remova o arquivo.

Ações: `up/left/down/right`, `a/b/x/y`, `start/select`, `l/r`, `menu`, conforme os botões disponíveis no console. Mega Drive usa nomes `a/b/c` para os três botões reais. No perfil de seis botões, `x/y/z/mode` acrescentam I/U/P/Y; Mode+Start chega ao jogo e não solicita a pausa. Camadas por console/jogo recusam ações inexistentes; a camada global ignora as ações conhecidas que o console não possui. `menu` é ação do frontend.

Teclas aceitas (maiúsculas ASCII): A–Z, 0–9, `ENTER`, `SPACE`, `ESC`, `UP`, `DOWN`, `LEFT`, `RIGHT`, F1–F12 e `NONE`. Escape fica reservado ao menu como rota permanente de recuperação; setas são aliases permanentes das direções. A164 não possui todas as teclas de um teclado USB; escolha somente as que seu teclado expõe. O arquivo usa apenas ASCII. Limites: 64 KiB, profundidade 6 e 4096 delimitadores estruturais.

Duas ações não podem compartilhar uma tecla. O firmware valida o mapa final; conflito, chave repetida, tecla inválida, versão desconhecida ou falha de leitura restaura **todo o padrão do console**, sem aplicar metade da configuração nem regravar o arquivo. A tela de teste informa `perfil SD ativo`, `padroes` ou o erro. Escape e touch continuam acessíveis. Teclas mantidas durante a troca de perfil/foco ficam bloqueadas até serem soltas; o diagnóstico HID continua mostrando o estado físico.

Testes host cobrem todos os 256 usages nos perfis, precedência, legado, conflitos, bounds, Mega Drive e bloqueio até soltar. O teste físico do remapeamento/A164 continua separado dos ensaios do editor.

A opção **Teste de teclas (HID)** agora é um toggle. Enquanto ativa, exibe as teclas físicas e bloqueia as ações do teclado no jogo e na navegação; touch e controles USB de teste continuam disponíveis. Toque no toggle para sair. Esse modo não é salvo e é encerrado ao trocar de perfil. `tools/capture_input.py` registra mudanças reais sem injetar teclas.

0.1.3 corrige a preservação do isolamento na volta do jogo à HOME e publica `platform.keyboard_buttons` separadamente de touch/USB. A captura verifica `settings.input_test` continuamente; por padrão mantém o modo protegido ao encerrar, até sair por touch. A ativação por USB exige firmware 0.1.3 para o diagnóstico separado.

## Controles visíveis e despertar

Os jogos agora mostram painéis laterais de toque e reservam 864 pixels centrais ao vídeo. Direcional permite diagonais; perfis ocultam botões inexistentes no console. O botão Menu superior direito e BOOT dão acesso à pausa. Toque e USB podem ser combinados com o teclado. O teste físico GBA confirmou direções, diagonal, A/B, L/R, Start/Select e liberação. Não há turbo oculto imposto a I/J no GB.

F2 alterna favorito, F3 busca e F4 lista na HOME. No diálogo de busca, teclado físico recebe letras/dígitos, Space, Backspace, Enter e Escape; ações de navegação ficam bloqueadas enquanto se digita. A tela apagada aguarda a liberação da entrada que a despertou. O teste próprio GBA confirmou que o primeiro toque Start não incrementa o contador e o segundo incrementa.

O perfil `n64-cpu-probe` é um diagnóstico headless: não oferece jogo/controlador N64 nem ativa o mapa analógico proposto. O perfil stable conserva GB/GBC; o diagnóstico troca temporariamente esse núcleo pelo interpretador R4300 para caber na reserva de app.

O desenvolvimento 0.1.15 acrescenta o [editor gráfico](EDITOR_CONTROLES.md) nas configurações da HOME e da pausa, com camadas Global/Sistema/Jogo e validação antes de Aplicar.

No fechamento stable 0.1.16, o editor aceita Tab/Shift+Tab, setas, Enter e Esc do teclado, além do keypad USB. Ele mantém as ações de jogo isoladas e restaura o foco ao cancelar. `genesis` é convertido para a chave de perfis `megadrive`, corrigindo a aplicação dos mapas MD na HOME, na pausa e no boot do jogo. Esses caminhos têm cobertura lógica; isso não substitui o ensaio físico A164.

Na retomada 0.1.16, cada gamepad USB conserva seu próprio relatório. Os botões são combinados: um relatório neutro ou a desconexão de um pad não solta os botões de outro. Cada eixo usa a maior magnitude entre os pads presentes (empate conserva o primeiro na ordem dos dispositivos); remover todos neutraliza a entrada. O firmware continua tratando essa combinação como um jogador, sem anunciar multiplayer. A lógica passou no host; hotplug com dois pads físicos depende de ensaio específico.

## Console N64 experimental

No perfil `n64`: direções E/A/S/D movem o analógico; O é A, K é B, Y é Z, T é Start, U/P são L/R, I/J/N/M são C↑/C←/C↓/C→. Gamepad USB usa o analógico esquerdo e o direito aciona os botões C; D-pad mantém seus bits digitais e oferece movimento analógico quando não há eixo. Há onze botões de toque, incluindo Z e C. O editor oferece o perfil N64. Home USB, Esc, BOOT ou Menu de toque abrem a pausa; Z+Start é entregue ao jogo. Apenas um jogador está implementado.

No perfil `sd-cores`, todos esses mapas coexistem, incluindo N64. O botão C↓ tem largura ajustada para não se sobrepor a C→. O carregamento de módulos não altera os perfis de controle salvos.
