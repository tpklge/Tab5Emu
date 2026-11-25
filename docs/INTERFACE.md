# Biblioteca, recursos e tela

A HOME mantém a organização do frontend original e acrescenta Todos, Favoritos, Recentes, Buscar e Favoritar. Busca cobre nome, descrição e caminho, ignorando caixa e acentos portugueses comuns. F2 alterna favorito, F3 abre busca e F4 muda a lista. O teclado da tela e o A164 permitem digitar; Enter/Aplicar confirmam e Esc cancela. O teclado do jogo fica isolado enquanto o diálogo recebe texto. Favoritos e recentes usam caminhos das ROMs; o save usa a identidade SHA-256 do conteúdo.

`config/settings.json` schema 1 é aplicado sobre as preferências binárias anteriores. Campos ausentes preservam os valores anteriores e campos desconhecidos sobrevivem à gravação. Limites: 64 KiB, profundidade 6, 4096 delimitadores, busca de 128 bytes, 128 favoritos e 20 recentes, com até 320 bytes por caminho. UTF-8 inválido é recusado. A gravação que excederia 64 KiB é recusada e preserva o JSON anterior. Versão errada, campos repetidos, tipos/limites inválidos recusam o documento inteiro. Um arquivo inválido não é sobrescrito automaticamente. O schema documentado está em `schemas/settings.schema.json`.

```json
{
  "schema_version": 1,
  "ui": {
    "theme": "system",
    "search": "",
    "library_view": 0,
    "idle_seconds": 0,
    "idle_in_game": false,
    "favorites": [],
    "recent": []
  }
}
```

`assets/fonts/tab5-latin-{16,20,24,32}.font` contém as fontes adicionais OFL, sem glifos grandes no firmware. Tamanho e SHA-256 são verificados no mesmo buffer carregado antes de abrir a fonte LVGL. O manifesto e a licença acompanham os arquivos. Falha ou ausência usa a fonte de emergência compilada. `tools/build_fonts.py` reproduz os arquivos com Node 22.12.0 e lv_font_conv 1.5.3 fixados. Preserve seus recursos existentes ao copiar uma atualização.

`assets/themes/{system,dark,light}.json` define schema 1, `mode` (`console`, `grayscale`, `invert`) e `strength` (0–100). O filtro é aplicado aos widgets existentes e aos criados depois; imagens de capas e tiles preservam seus pixels. Esses presets não constituem um editor completo de temas. O modo system conserva o estilo por console da base.

Capas JPEG baseline até 360×360 e 256 KiB são decodificadas numa tarefa de prioridade baixa, pausada durante jogos e entrega do cartão ao USB. A fila tem até quatro pedidos e o cache mantém até três entradas. Imagens em uso não são liberadas; a invalidação remove referências e o cache LVGL antes da memória. Não há decodificação de capas no loop do jogo. Na 0.1.15, o índice persistente no SD oferece páginas de 64 e filtros em streaming para bibliotecas grandes; veja BIBLIOTECAS_GRANDES.md. Sem índice, permanece a descoberta limitada em RAM.

Com timeout habilitado, a política padrão apaga apenas o backlight na HOME ou pausa. Jogos ativos mantêm a tela acesa, inclusive em cutscenes. `idle_in_game: true` permite o apagamento também durante jogo. O primeiro toque/tecla só desperta e aguarda liberação antes de entregar ações; brilho configurado é preservado. Não suspende a emulação nem desliga o aparelho. O padrão é timeout desativado.

Durante jogos, os controles ficam visíveis nas laterais, com o vídeo limitado à região central de 864 pixels. GB/GBC mostram direcional, A/B, Start/Select e Menu; GBA inclui L/R; SNES inclui X/Y/L/R; Mega Drive usa A/B/C e Start. Botões inexistentes no console ficam ocultos. Original/Fit/Fill respeitam essa área.

Se um jogo falhar ao abrir, a HOME mostra o motivo no campo de status até a próxima tentativa. O status USB também publica `launch_error`. GB/GBC distingue cabeçalho incompleto, bancos ausentes, mapper/geometria não suportados e falha de save; não apaga nem corrige automaticamente esses arquivos. A verificação não exige logo ou checksum proprietário e aceita os homebrews próprios.

No menu de pausa, salvar/carregar estado informa sucesso ou falha em uma faixa de status. Slot vazio, arquivo inválido e erro de leitura/escrita têm mensagens; uma falha não confirma o slot como estado de retomada. Falha de persistência do save cancela Sair ou Voltar ao Launcher e mantém o jogo disponível para nova tentativa. O status USB publica `operation_error`; uma operação bem-sucedida ou Retomar limpa o erro. GB/GBC também recusa falta de memória na inicialização, desfazendo as alocações parciais antes de voltar à HOME.

O protocolo USB 6 permite capturar a pausa e o editor modal. A tarefa principal atende a captura sem avançar o guest; uma cópia independente mantém os pixels estáveis até terminar a transferência. A alocação dessa cópia pode falhar com retorno de erro. Salvar/carregar/sair pela API exige retomar primeiro; as ações do próprio menu continuam disponíveis.

Fontes SD incompletas e temas ausentes, de versão errada ou inválidos têm aviso e fallback interno. O parser de tema recusa modo desconhecido e intensidade fracionária/fora de 0–100, preservando o arquivo. `settings.resources` informa fontes carregadas, fallback e erro de tema. Recarregar a biblioteca também relê o tema. A HOME distingue erro do índice e biblioteca vazia/filtro sem resultados no status.

Na 0.2.1-sd, a paginação foi deslocada de y=650 para y=604, acima da barra de dicas. Setas ficam ocultas quando há uma única página. A linha de status e os rótulos da toolbar são limitados ao seu espaço, impedindo que textos longos cubram controles. A troca de núcleo usa mensagens de falta/corrupção dos arquivos no SD, sem executar arquivos incompatíveis.
