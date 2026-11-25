# Launcher do Paludo: contrato auditado

Fonte real: projeto vizinho `../Tab5LauncherPaludo`, versão `0.8.0-rc.2`. O commit e o estado local estão em `artifacts/audit/launcher-commit.txt` e `launcher-worktree.txt`; a árvore possui alterações locais, portanto o commit sozinho não descreve todo o código consultado. A auditoria não alterou esse projeto.

O dispositivo respondeu em `/dev/cu.usbmodem12201` ao comando **somente de consulta** `tab5 status`. Isso é evidência do launcher ativo e do SD montado, não de execução deste emulador. As consultas Web autenticadas a `/api/apps` e `/api/flash-layout` foram guardadas separadamente. Não se publica o código de pareamento nem cookies.

Fluxo implementado pelo launcher:

1. Console USB `tab5 web-start` fornece IP e código efêmero de sessão. Nada é fixado no frontend.
2. `POST /api/session`, header `X-Launcher-Token`, cliente `web`, estabelece cookie.
3. `POST /api/upload?directory=...&relative=...` recebe arquivo no SD. A resposta `verified` e a releitura por `GET /api/file?path=...` permitem comparar SHA-256.
4. `POST /api/operation` com `action=install` e `path` **prepara um plano**. Retorna mensagem e ticket; não instala.
5. `action=execute` com o ticket executa. `GET /api/operation` informa progresso. Apps e layout devem ser lidos novamente, sem repetir uma operação ambígua.

Os quatro slots consultados já contêm apps: Meshtastic, duas versões de Gotchi e TabChat. Não são destinos autorizados para substituição pelo emulador. A API informou inicialmente 6160384 bytes, mas esse cálculo usava uma fronteira legada de 8 MiB. A tabela efetiva do backup confirmou **9 MiB livres**, conforme informado pelo usuário. Isso não era um slot já registrado. O plano inicial exigiu registro na tabela; o usuário o autorizou explicitamente após backup integral verificado. O documento de requisitos exclui alteração automática de layout e substituição de apps de terceiros.

Entrega: somente `tab5-emu-stable-<versão>.bin` app-only. Não enviar bootloader, tabela, updater upstream, imagem merged ou rádio C6. O pacote SD é independente. O build derivado usa flash DIO a 80 MHz, CPU nominal 360 MHz e tabela em 0x8000, conforme configuração do launcher consultada; o upstream usa 120 MHz e 0x9000. O layout efetivamente instalado é dinâmico; nunca usar o `partitions.csv` upstream para gravar o Tab5.

Retorno implementado na HOME: Configurações → Voltar ao Launcher do Paludo. O app verifica em runtime a partição factory `launcher`, descritor `tab5_launcher`, partição `launch_req` e capacidade de boot de uso único. A posição relativa 64 e magic 0x3150434C vêm de `components/boot_mgr/include/boot_request.h`. Salva as preferências antes de `esp_restart()`. Não escreve offsets de flash nem NVS. Sem contrato reconhecido, não executa o retorno. O retorno normal foi exercitado nas versões corrigidas; desde 0.1.13, a pausa também permite retornar diretamente, após persistir o save, liberar o cart e salvar preferências.

Estado de entrega/instalação deve ser registrado como **construído → enviado → recebido/hash → instalado → iniciado → retorno confirmado**, com evidências por etapa. A existência de uma imagem ou uma consulta ao launcher não avança os estados seguintes.

## Resultado de envio deste marco

`4000896` bytes enviados e relidos; SHA-256 `5d6c40c6eb26dc2efe22b38fa373f4f6baa02823d03d4d3b0df3beaa43fb944f`. Destino SD: `/sdcard/APP/tab5-emu-5d6c40c6eb26dc2e.bin`. O plano escolheu **slot 4/app4**, exigindo reorganização da flash e reinício. Naquele estágio, o ticket ainda não havia sido executado; o registro autorizado e as instalações posteriores estão descritos abaixo. Evidência: `artifacts/launcher-stage.json`.

O pareamento Web expira após 15 minutos, mas `web-start` pode devolver o código antigo enquanto o servidor já está ativo. O adaptador detecta HTTP 401 e renova somente o serviço Web por `web-stop`/`web-start` via console, antes do upload. Não registra o novo código/cookie. O teste dessa recuperação é simulado; o envio acima também confirmou seu uso no aparelho.

## Registro autorizado e primeiro boot

Depois da autorização explícita do usuário e backup integral verificado, o Launcher registrou app4 em 0x6F0000–0xAD0000 (4063232 bytes), sem mover os quatro apps anteriores. Essa primeira execução criou a reserva vazia e reiniciou; exigiu nova instalação no slot já registrado. Build dev2 SHA `2efdee172bc1319deb0721e260d1d6ee2ec5e2f050baf91d9aa69c29f8958425` foi instalado e o boot confirmado por leitura completa do hash de ELF via USB. Nesse build o retorno não foi validado; houve perda da USB ao consultar diagnóstico. Corretivo 0.1.1 acrescenta recuperação IRAM do mux PHY antes de `esp_restart_noos`, incluindo o caminho de pânico.

O Launcher não oferece overwrite direto da reserva ocupada. `tools/paludo.py update` prepara backup do binário anterior e remove somente tab5-emu, preservando dados externos e a reserva. Depois exige plano sem reorganização para a mesma reserva; falha de backup impede apagar o app. O novo binário já deve estar enviado e relido antes dessa remoção. Não usar OTA do emulink: ela pressupõe o updater ota_1 do upstream, incompatível com os demais apps do aparelho.

## Validação histórica — 0.1.5

4028112 bytes enviados, relidos e instalados no app4, SHA `fc82a0c7d021d9d2c68587173fc2a3b3dfdb30e25570108bc3c43cb109478227`; boot conferido pelo ELF completo. Retorno por touch na HOME fez a USB do Launcher enumerar antes de abrir a serial; status `0.8.0-rc.2`, `SW_RESET`. Descritores dos cinco apps e layout relidos idênticos ao pós-instalação. Novo boot no app4 confirmou novamente ELF e coredump vazio; saves GB/SNES sobreviveram ao reinício. Evidência sanitizada: `artifacts/hardware-verification-015.json`.

## Reserva e atualização dos perfis atuais

O `allocation_model()` arredonda a imagem solicitada a 64 KiB e também tenta encolher reservas maiores. Uma imagem pequena que cabe no espaço físico ainda pode exigir alteração da tabela. O adaptador agora compara o tamanho arredondado **antes** de remover o app, verifica possíveis compactações de outras reservas e recusa mudanças. Um ensaio pequeno de CPU N64 revelou essa particularidade; o plano foi recusado, o stable restaurado, sem execução da reorganização.

Na 0.1.13 stable, o app tem 4027408 bytes, SHA `ae73c4d6b72e4ac6d7a240456ec356e5144077d7c22098c046a5a7b36a9b00eb`, margem 35824 na reserva atual. Boot confirmou o ELF completo `6cf9d28c934b3ac55309c481706ffb90d3846be145681589c8321c280aa542eb`. O retorno direto da pausa foi observado antes de abrir a serial do Launcher, `SW_RESET`, com SRAM GBA incremental já persistida.

O serviço Web exige Wi-Fi conectado. Na espera de inicialização, o adaptador aceita somente o erro NOT_READY e faz uma única tentativa `wifi-retry` pelo comando real do Launcher, antes de upload ou flash. Não repete mutações de instalação após timeout ambíguo. Rede ausente é informada sem remover o app.

## Bloqueio do atualizador legado — 0.1.14

O serviço OTA upstream está desabilitado nesta integração. A auditoria final também encontrou o comando USB `ENTER_UPDATER` herdado, que selecionava `ota_1` escrevendo diretamente `otadata`; no layout real essa partição pertence a outro app. A 0.1.14 remove a função de escrita e responde que a atualização deve passar pelo Launcher do Paludo. O comando não foi executado nas versões anteriores durante esta auditoria. `tools/smoke_updater_guard.py` exige versão corrigida e ELF exato antes de enviar o comando.

O stable 0.1.14 confirmou o ELF `eea38372e6a1fa6e0b722e3d476d9d346c7210fd2b44f766b11504f178c454a2`, recusou o comando na HOME e no GBA sem reset e retornou ao Launcher pela pausa. A USB nativa foi observada antes de abrir a serial; status SW_RESET e leitura após novo boot confirmaram o incremento SRAM persistido. Os dois updates 0.1.14 conservaram os outros apps e todos os tamanhos de reserva.


## Marco 0.1.15

O stable final de 4040032 bytes foi enviado, relido por SHA e instalado na mesma reserva app4 de 4063232 bytes. Boot confirmou ELF `360a8d438ebfe5eb0bfc82ee7d3084b17d87a57bffd747ee3e37d073c5456c64`. O guard legado foi recusado na HOME e no GBA. O retorno direto da pausa foi observado pela enumeração da USB nativa antes da console; a leitura pelo Launcher confirmou SRAM GBA incrementada e persistida. Apps e layout foram comparados novamente e permaneceram iguais. As preferências/perfis originais e o estado preservado do usuário foram conferidos por releitura antes de voltar ao stable. A imagem RV32 recompilada é apenas build, separada da comparação do candidato anterior.
