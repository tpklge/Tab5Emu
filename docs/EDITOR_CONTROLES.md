# Editor de controles

Abra Configurações → Controles na HOME ou na pausa. Escolha a camada Global, Sistema ou Jogo e ajuste as teclas pelos seletores. `NONE` desabilita uma ação; Esc permanece como recuperação para o menu. A camada por jogo usa SHA-256 real da ROM. Na HOME, aguarde o cálculo da identidade do jogo selecionado; na pausa, a identidade já está disponível.

Aplicar valida o documento e os conflitos antes da gravação atômica. Global é validado também nos outros sistemas. Cancelar preserva o arquivo. Restaurar camada remove as substituições daquela camada, fazendo herdar a camada anterior/padrões; só Aplicar confirma a alteração. Campos desconhecidos válidos são preservados e o formato plano antigo é migrado sem descartar os outros sistemas. Arquivo inválido, cartão entregue ao USB ou alteração concorrente do arquivo impedem a gravação. A geração anterior fica em `.bak`.

O editor usa uma camada modal que bloqueia toques na tela subjacente e suspende o grupo de navegação do keypad. Impede ações de jogo/atalhos enquanto está aberto e bloqueia teclas mantidas até soltá-las ao devolver o foco. O perfil de Mega Drive de seis botões precisa estar selecionado antes de abrir o jogo. A164 físico continua exigindo ensaios próprios de rollover, ghosting e reconexão; editar um mapa não certifica essas propriedades elétricas.

O editor agora tem grupo próprio de navegação para teclado/gamepad USB. No teclado físico, Tab/Shift+Tab muda o campo, setas e Enter escolhem valores, e Esc cancela sem gravar. Teclas já mantidas na abertura não acionam o editor. No gamepad, B cancela; o grupo anterior e o bloqueio até soltar são restaurados ao fechar. Aplicar permanece a única confirmação de gravação.

O identificador `genesis` usado pelo frontend e pelos saves é normalizado para `megadrive` ao ler/editar perfis. Isso corrige o editor da pausa e os overrides por sistema/jogo do Mega Drive, sem mover saves ou renomear seus diretórios. O nome canônico no JSON continua sendo `megadrive`.
