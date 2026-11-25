# Núcleos no microSD — perfil `sd-cores`

O perfil `sd-cores` reúne GB/GBC, GBA, Mega Drive, SNES e N64 no mesmo aplicativo. O `stable` continua como padrão de compilação e mantém GBA interpretado, sem RV32/JIT. A corrupção de heap do backend GBA experimental continua pendente; seu gerenciador e diagnósticos não são alterados por esta implementação.

Os núcleos são vinculados junto com o firmware e seus blocos de código são extraídos para `Tab5-Emu/cores/<núcleo>-<SHA-256>.bin`. Ao abrir um jogo, somente o núcleo necessário é lido para a região de código compartilhada em PSRAM. Isso estende o mecanismo de overlays que já carregava os mesmos blocos a partir da flash. A interface e o carregador continuam no aplicativo e iniciam sem os arquivos de núcleos.

Cada bloco é específico daquela compilação. O firmware contém seu SHA completo, tamanho e endereço de vínculo; arquivo ausente, truncado, alterado ou de outra compilação é recusado antes da execução, e a biblioteca mostra o motivo. Não basta renomear um núcleo obtido de outro emulador. Atualize o aplicativo e seus arquivos de núcleos juntos. Não apague a geração anterior antes de validar a nova: os nomes por hash permitem conservar as duas.

```sh
bash tools/build.sh sd-cores
# A imagem deve manter o arredondamento da reserva app4, inclusive se diminuir.
# Ajuste TAB5_SD_RESERVE_BYTES no build antes de instalar, se necessário.
python3 tools/check_sd_cores_artifact.py build-sd-cores --report /tmp/sd-cores-image.json
```

Saídas: `build-sd-cores/tab5-emu.bin` e `build-sd-cores/sd/Tab5-Emu/cores/`. O arquivo `manifest.json` identifica os cinco módulos (GB/GBC compartilham um). O gate compara cada arquivo com o descritor efetivamente embutido no ELF e recusa qualquer alteração da reserva app4 de 4063232 bytes. A instalação continua pelo Launcher, somente app4; não usa bootloader, tabela, rádio ou reorganização.

Na transição para N64, a arena de ROM dos núcleos maduros é liberada antes de criar o console e reservada novamente após sua destruição. Os 4 MiB de bits auxiliares do RDP existem apenas enquanto N64 está ativo. O prescaler VI de desktop, que ocupava 1,6 MiB sem uso no port, deixa de ser inicializado; o scanout próprio 320×240 continua responsável pelo vídeo. Não se transfere o framebuffer/RDRAM para o SD: essas memórias precisam permanecer na PSRAM durante o jogo.

Os custos restantes incluem o interpretador MIPS, o RSP e o rasterizador por software. Carregar código do SD resolve armazenamento; não transforma o cartão em GPU. A versão também habilita tabelas de despacho do compilador para N64, executa seu código no overlay em PSRAM e mantém o pequeno conjunto de registradores/DMEM/IMEM em RAM interna. A RDRAM e os demais buffers continuam em PSRAM. Ganhos de velocidade exigem comparação física da mesma cena e contadores de VI, sem trocar o ritmo emulado por contagem artificial.

A barra de paginação fica acima do rodapé, com setas ocultas em listas de uma página. O status usa uma linha limitada e os botões C do N64 não se sobrepõem. Os testes lógicos são acompanhados de captura física do layout e troca entre núcleos na validação da entrega.

## Candidato 0.2.2-sd

Acrescenta gráficos Daedalus HLE no módulo N64 e preroll de áudio GBA. Atualize todos os módulos junto com o aplicativo: o endereço de vínculo pode mudar mesmo nos núcleos sem alterações. A 0.2.2-sd foi instalada pelo Launcher no app0 após a atualização do Launcher pelo usuário; o catálogo estava vazio e a alocação foi gerenciada pelo próprio instalador. O ELF e os cinco módulos foram conferidos, os seis sistemas passaram em 13 ciclos e o coredump ficou vazio. A prova detalhada abaixo continua pertencendo à versão anterior; a nova está em `artifacts/daedalus-hardware-verification-v0.2.2.json`. Veja [N64_PORT.md](N64_PORT.md) e [AUDIO.md](AUDIO.md).

## Validação física 0.2.1-sd

A imagem de 4030000 bytes, SHA `7737f7675e5a994f8b140b5a780a787fc844af59cae8e2085db08666c33e591c`, foi instalada pelo Launcher. O GET_INFO confirmou ELF `2f36109e31bb87b94dfe27fbcd088e1164c4bf20764b74bcfc36d13ccb0e5d4e`. Reservas e descritores dos outros apps permaneceram iguais. Os módulos também foram relidos integralmente pelo USB e conferidos por SHA.

Os catálogos mostraram GB 11, GBC 15, GBA 16, SNES 11, MD 17 e N64 22 entradas (92 no total). Os seis sistemas executaram diagnósticos próprios em 13 ciclos, incluindo trocas N64→GBA/GB/SNES/MD. O módulo GBC truncado foi recusado antes da execução e sua restauração exata permitiu novo boot. N64 conferiu A e EEPROM 6→7 com backup. Coredumps permaneceram vazios.

Na comparação física de cenas SM64 por faixas de listas RDP, o título passou de cerca de 2,06 para 2,19 VI/s e o rosto de 1,08 para 1,16 VI/s. Ganho aproximado de 6–7%, em uma execução de cada versão, sem inferir desempenho geral. A nova versão chegou à seleção de arquivo por Start. Continua sem velocidade normal ou áudio contínuo; gameplay não foi certificado. Evidência sanitizada: `artifacts/sd-cores-hardware-verification.json`.
