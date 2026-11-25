# Prompt para Codex — Tab5-Emu, Launcher do Paludo e port experimental de Nintendo 64

Especificação preparada em 23/11/2026. Copie o conteúdo entre INÍCIO e FIM DO PROMPT para o Codex no projeto de destino. O mapeamento solicitado é **E/A/S/D em todos os consoles**, inclusive no analógico do Nintendo 64: no teclado físico do Tab5, W não ocupa a posição desejada para cima.

## INÍCIO DO PROMPT

Você deve implementar uma nova versão do **Tab5-Emu para o M5Stack Tab5**, usando o projeto `esp-cpp/tab5-emu` como base técnica e referência visual, com integração ao **Launcher do Paludo**, suporte ao teclado físico oficial por I²C e conteúdo externo em uma pasta **`Tab5-Emu` no microSD**. Desenvolva também um port real, incremental e experimental de um emulador de Nintendo 64 e investigue as quedas de desempenho do gpSP no Tab5.

Este pedido é de implementação: entregue código compilável, binários, pacote de dados do cartão, documentação e evidências de testes. Trabalhe em etapas utilizáveis. Não encerre apenas com planejamento ou uma demonstração visual. Tome decisões rotineiras e continue sem perguntar a cada etapa; registre decisões e impedimentos concretos.

### 1. Auditoria inicial e preservação do projeto

1. Leia as instruções do repositório e inspecione o workspace, branches, alterações locais e ferramentas disponíveis. Preserve o trabalho existente do usuário.
2. Examine `https://github.com/esp-cpp/tab5-emu`, seus submódulos, configuração de build, arquitetura, tela inicial, entrada, energia, vídeo, áudio, SD, atualização e licenças. Fixe commits reproduzíveis. Faça alterações organizadas sobre essa base ou num projeto derivado; não reescreva núcleos maduros sem motivo.
3. Localize a implementação real do Launcher do Paludo, suas configurações e o fluxo já utilizado para envio/instalação de apps. O nome pessoal não comprova um repositório público. Não confunda esse launcher com o updater do tab5-emu original.
4. Se houver somente o documento de requisitos do launcher, trate-o como especificação, não como protocolo implementado. As referências UNIZERSION/tab5-launcher e bmorcelli/Launcher ajudam na investigação, mas não comprovam os endpoints ou o layout do launcher pessoal.
5. Registre chip, revisão do Tab5, display/touch, flash, PSRAM, clock real, SDK/BSP, partições, slot disponível, versão do launcher e dependências. Confirme os valores no dispositivo quando acessível.
6. Compile primeiro a base sem alterações funcionais e registre tamanho, recursos existentes e comportamento. Não atribua à nova implementação defeitos anteriores sem comparação.
7. Crie uma matriz requisito → componente → etapa → teste → estado. Diferencie implementado, compilado, testado em host e validado fisicamente.

O foco obrigatório é SNES, Game Boy, Game Boy Color, Game Boy Advance e Mega Drive/Genesis, mais o desenvolvimento experimental de N64. Audite os demais sistemas já existentes na base e documente sua preservação em perfis opcionais. Não acrescente grandes ports de jogos não solicitados ao firmware padrão.

### 2. Objetivo de armazenamento e tamanho do firmware

Mantenha no binário apenas o código e os dados necessários para executar o aplicativo: inicialização, drivers usados, frontend, núcleos habilitados, parsers e uma interface mínima de recuperação. Coloque no microSD ROMs, capas, descrições, fontes adicionais, temas, imagens, traduções extensas, saves, estados, screenshots, catálogos, configurações detalhadas, relatórios e recursos volumosos.

O diretório público é **`/Tab5-Emu` na raiz do cartão**. Se o cartão for montado em `/sdcard`, o caminho interno será `/sdcard/Tab5-Emu`; isso não significa criar uma pasta `sdcard` dentro do cartão. Use um resolvedor central de caminhos; nenhum núcleo deve espalhar caminhos antigos fixos pelo código.

Regras obrigatórias:

- Não embuta capas, ROMs, BIOS proprietárias, wallpapers ou grandes arrays gerados a partir de arquivos.
- Não apresente código C/C++ como executável diretamente do microSD. Os núcleos compilados normalmente ocupam a flash do app; PSRAM, RAM interna e dados no cartão têm funções distintas.
- Não prometa carregar `.so`, `.bin` de núcleo ou código arbitrário do cartão sem um mecanismo real de carregamento, ABI, relocação, permissões de execução e gerenciamento de cache. Um arquivo de firmware no SD pode ser instalado pelo launcher; isso não o torna um módulo executável do frontend.
- Primeiro meça o custo de cada núcleo e recurso no mapa do linker. Use remoção de código não utilizado, dependências enxutas e perfis de build, sem sacrificar desempenho ou correção indiscriminadamente com otimização por tamanho.
- Se a combinação não couber no slot existente, forneça perfis como `stable` e `n64-experimental`, ambos reconhecidos pelo launcher e compartilhando o mesmo diretório de conteúdo. Prefira isso a alterar automaticamente bootloader ou partições.
- Mantenha pequena fonte de emergência e mensagens essenciais no firmware para orientar o usuário quando o cartão estiver ausente ou corrompido.
- Configuração volumosa fica no SD. Use NVS somente para o pequeno estado realmente necessário antes de montar o cartão ou para integração com a plataforma, em namespace próprio. Não apague a NVS global.
- Recursos externos têm versão e compatibilidade documentadas. Atualizar os arquivos distribuídos não pode sobrescrever ROMs, saves, metadados ou preferências do usuário.

### 3. Estrutura do cartão e biblioteca de jogos

Implemente esta organização inicial, ajustando detalhes somente com justificativa e documentação de migração:

```text
Tab5-Emu/
  assets/
    fonts/
    icons/
    themes/
    backgrounds/
    locales/
  config/
    settings.json
    input_profiles.json
    systems.json
  library/
    games.json
    covers/
    thumbnails/
  roms/
    snes/
    gb/
    gbc/
    gba/
    megadrive/
    n64/
  bios/
  saves/
  states/
  screenshots/
  cache/
  logs/
  benchmarks/
  docs/
```

Não duplique recursos compartilhados entre os perfis. Gere a estrutura vazia com exemplos livres; não distribua ROMs ou BIOS comerciais. Os saves e estados devem ser separados por sistema, identidade da ROM e, quando necessário, núcleo/versão. Duas ROMs com o mesmo nome de arquivo não podem compartilhar save por engano.

Crie catálogo próprio versionado, documentado em JSON Schema. O exemplo abaixo é uma proposta para esta implementação, e não uma alegação de formato nativo do upstream:

```json
{
  "schema_version": 1,
  "games": [
    {
      "id": "homebrew-gba-demo",
      "system": "gba",
      "title": "Demonstração livre",
      "rom": "roms/gba/demo.gba",
      "cover": "library/covers/demo-gba.jpg",
      "description": "Exemplo de descrição em português. Substitua pelos dados do seu jogo.",
      "year": 2026,
      "developer": "Autor do exemplo",
      "publisher": "Distribuição independente",
      "genre": ["Aventura"],
      "players": 1,
      "language": ["pt-BR"],
      "tags": ["homebrew"]
    }
  ]
}
```

Os caminhos do catálogo são relativos à raiz `Tab5-Emu`. Valide limites, extensões, tamanho dos campos, UTF-8, IDs repetidos e traversal. Prefira parser com memória limitada e índice/cache reconstruível para bibliotecas grandes. Arquivos sem metadados continuam aparecendo pelo nome, com capa alternativa; metadados inválidos de um jogo não derrubam toda a biblioteca.

Verifique o formato `metadata.csv` da versão consultada do upstream e forneça importador quando aplicável. Não invente colunas de descrição como se já fossem suportadas pelo original. Preserve os arquivos antigos e gere relatório de importação.

### 4. Interface semelhante ao tab5-emu original

Inspecione a UI real e suas imagens antes de implementar. Preserve sua organização reconhecível: seleção de sistemas, biblioteca, destaque do jogo, capa e acesso rápido às configurações. Acrescente descrições e recursos deste pedido mantendo a identidade do projeto e os créditos aplicáveis.

A HOME deve mostrar:

- Sistemas disponíveis, com N64 identificado como experimental quando efetivamente houver um port executável.
- Lista ou grade de jogos, capa, título e painel de descrição; busca, favoritos, recentes e filtro por sistema.
- Bateria e estado de alimentação/carregamento obtidos do hardware; indicador de cartão e teclado. Não invente porcentagem ou estado de carga. Se a porcentagem exigir estimativa, documente e calibre o método, incluindo filtragem.
- Atalhos para configurações e retorno ao Launcher do Paludo.
- Estados claros para cartão ausente, biblioteca vazia, ROM incompatível, BIOS necessária ausente e recursos externos de versão incorreta.

Inclua temas carregados do SD, brilho, volume, escala de vídeo, proporção, filtros opcionais e desligamento da tela por inatividade. Durante jogo ativo, não apague a tela automaticamente só porque não houve entrada durante uma cutscene; defina a política por estado e preferência. Distinga apagar o backlight, suspender emulação e desligar o dispositivo.

O primeiro toque/tecla ao acordar deve apenas despertar a tela. Não dispare uma ação involuntária. Use carregamento assíncrono de capas, cache limitado e miniaturas preparadas no computador. Não decodifique imagens grandes em callbacks da UI ou no loop de emulação.

O menu de pausa deve permitir continuar, reset do jogo, salvar/carregar estado, ajustar controles, vídeo/áudio, sair para a biblioteca e voltar ao launcher. Saves normais devem ser persistidos com integridade antes de sair. Escrita de estado deve verificar espaço e nunca destruir o último arquivo válido se falhar.

### 5. Teclado físico oficial e controles

O teclado oficial do Tab5 é um periférico **I²C**, não um teclado USB. Consulte o BSP e driver oficiais e confirme endereço, pinos e modo de leitura; as referências do A164 indicam endereço `0x6D` e conexão Ext.Port1. Prefira códigos de tecla e estado pressionado/solto a caracteres com repetição de texto.

Implemente um único serviço de entrada para teclado I²C, touch e controles USB suportados pela base. Ele produz estado por jogador e ações da interface. Não duplique drivers de barramento. ISR apenas sinaliza trabalho; leitura I²C ocorre na tarefa apropriada.

**Mapeamento padrão obrigatório, confirmado pelo usuário:**

| Função | Tecla |
|---|---|
| Cima / analógico para cima | E |
| Esquerda / analógico para esquerda | A |
| Baixo / analógico para baixo | S |
| Direita / analógico para direita | D |
| Ação superior, correspondente a X no SNES | I |
| Ação à direita, correspondente a A no SNES | O |
| Ação à esquerda, correspondente a Y no SNES | J |
| Ação inferior, correspondente a B no SNES | K |
| Start | T |
| Select, nos consoles que possuem esse botão | Y |

**Não use W como padrão de cima em nenhum console.** Não substitua EASD por WASD em exemplos, documentação ou arquivos gerados.

Para GB/GBC/GBA, use O=A e K=B por padrão; I/J podem ficar livres para remapeamento, sem impor turbo. Para Mega Drive, proponha J=A, K=B, O=C; no perfil de seis botões, acrescente teclas configuráveis sem perder as quatro teclas principais. Para SNES/GBA, disponibilize L/R em U/P como proposta inicial configurável.

Para N64, o perfil inicial deve cobrir todas as entradas, não apenas o analógico:

| Entrada N64 | Padrão proposto |
|---|---|
| Analógico | E/A/S/D |
| A / B | K / J |
| C cima / C esquerda / C baixo / C direita | I / U / O / P |
| L / R / Z | Q / R / F |
| D-pad cima / esquerda / baixo / direita | 8 / 4 / 5 / 6 |
| Start | T |
| Menu auxiliar do frontend | Y |

O N64 não tem Select. Y deve ser identificado como ação do frontend nesse perfil. Ajuste a proposta para disponibilidade real das teclas, mantendo EASD e IOJK como pedido e permitindo remapeamento completo. Documente a diferença entre botões A/B e os quatro C; não finja que quatro teclas cobrem todo o controle do N64.

Converta EASD em eixos analógicos digitais com rampa e intensidade máxima configuráveis. Normalize diagonais, neutralize direções opostas por política documentada e zere o estado ao desconectar/perder foco. Esse mapeamento não oferece a precisão de um analógico físico; controles USB com eixos reais devem conservar seus valores próprios.

Aceite diagonais, direção + múltiplos botões e Start/Select simultâneos quando o teclado permitir. Verifique o rollover e ghosting reais do A164. Se o modo de caracteres não expuser essas combinações, use o modo apropriado do driver. Se houver limitação física, registre exatamente quais combinações falham e ofereça remapeamento; não esconda a limitação atrás de debounce.

Adicione tela de teste com estado de todas as teclas/eixos. Permita perfis globais, por console e por jogo em arquivos do SD; detectar conflitos e restaurar padrões. Mantenha acesso ao menu por touch/BOOT ou mecanismo equivalente da base, sem depender de uma combinação impossível no teclado.

### 6. Integração e envio pelo Launcher do Paludo

A entrega normal deve ser um **binário app-only compatível com o Launcher do Paludo**, enviado pelo fluxo real disponível nele. O pacote do cartão é separado. Não envie uma imagem merged como se fosse atualização simples do app.

Audite e documente antes do primeiro envio:

- Chip e revisão aceitos, tamanho máximo do app e slot de destino.
- Exigências de bootloader, flash/PSRAM, clock, tabela de partições e dados auxiliares.
- Canal efetivo de envio: WebUI/API, serial, diretório monitorado, catálogo ou outro recurso realmente implementado.
- Autenticação/pareamento, formato do manifesto, nomes/IDs dos apps, verificação de hash e confirmação de instalação.
- Método suportado de retorno ao launcher e política de atualização preservando dados.

Crie um adaptador de entrega separado do frontend, configurado por arquivo/variáveis locais. Não fixe IP, senha, token ou endpoint suposto. Gere o manifesto conforme o contrato encontrado; se precisar criar um formato próprio de distribuição, identifique-o como próprio e implemente a conversão necessária.

Verifique especialmente as exigências do tab5-emu original relacionadas a SDK, updater, bootloader e clock. Elas precisam ser conciliadas com a instalação existente. Não copie automaticamente seu procedimento de gravação completa, nem suponha offsets como `0x10000` ou slot `ota_1`.

Gere para cada marco executável:

1. `tab5-emu-<perfil>-<versao>.bin`, app-only.
2. `Tab5-Emu-sd-<versao>.zip`, cuja raiz contém a pasta `Tab5-Emu/`.
3. Metadados de versão, commit, tamanho, SHA-256, requisitos e compatibilidade de recursos.
4. Relatório de instalação e teste: construído, enviado, recebido, instalado e iniciado são estados distintos.

Quando o dispositivo e canal já configurado estiverem disponíveis, envie pelo launcher e confirme hash, instalação, inicialização e retorno. Mostre previamente o destino e o que será substituído; a atualização solicitada do app faz parte do trabalho. Preserve launcher, outros apps, dados e configuração. Mudança de layout, apagamento de dados ou substituição de bootloader não está implícita nessa atualização.

Se o código/protocolo/endereço do launcher estiver indisponível, continue a implementação e o empacotamento, liste a informação exata que falta e não declare envio concluído. Não substitua silenciosamente o fluxo pelo `idf.py flash`. O acesso externo ao SD também deve respeitar o método realmente suportado; não acesse o mesmo FAT simultaneamente por firmware e USB MSC.

### 7. Arquitetura e desempenho geral

Separe BSP/energia, entrada, filesystem, catálogo, UI, serviços de vídeo/áudio, adaptadores dos núcleos, persistência, diagnóstico e integração com launcher. Reaproveite a arquitetura do upstream quando adequada. Não inclua todo o RetroArch apenas para acessar um núcleo libretro se um adaptador pequeno resolver.

Use interfaces que permitam trocar o núcleo de GBA ou o backend de N64 sem reescrever a biblioteca. Defina init/load/reset/run/pause/input/audio/video/save/unload e liberação ordenada de recursos. Não faça o adaptador afirmar suporte a uma função inexistente no núcleo.

Documente distribuição entre os dois cores do P4, tarefas, afinidade, prioridades e dono de cada periférico. Meça antes de paralelizar; dois cores não tornam automaticamente o loop de um emulador paralelo.

Orce RAM interna, PSRAM, buffers DMA, framebuffers, texturas/capas, cache de ROM, estado do núcleo, áudio e JIT. Registre picos, maior bloco livre e fragmentação. Suspenda animações e decodificação de capas durante jogos se disputarem recursos importantes.

Trate PPA como acelerador das operações que ele realmente suporta; não o descreva como GPU 3D do N64. Vídeo deve preservar proporção e permitir resolução interna reduzida, escala inteira e opções econômicas. Não confunda taxa de atualização da tela com velocidade de emulação.

Use acesso ao SD em blocos e cache limitado. Não carregue uma ROM inteira por padrão se ela consumir a memória necessária para executar o núcleo. Detecte cartão removido/SD cheio, finalize operações com erro recuperável e preserve saves íntegros. Não formate o cartão automaticamente.

### 8. Investigação obrigatória do gpSP

Não parta da conclusão de que o gpSP é lento ou de que o Tab5 é insuficiente. Produza `docs/GBA_PERFORMANCE.md` e dados brutos no SD/artefatos de teste, distinguindo problema do port, configuração, núcleo, pipeline ou limite de hardware.

#### 8.1 Baseline e instrumentação

- Identifique versão/commit do gpSP, opções de compilação, BIOS/HLE usada, backend de CPU efetivamente ativo, caches e frameskip. Confirme por código e log se existe dynarec RV32 e se ele está sendo usado; uma opção genérica de dynarec pode cair no interpretador.
- Fixe hardware, clock normal suportado, SDK, flags, cartão, cena, ROM por hash, sequência de entrada e configuração. Não introduza overclock como solução inicial.
- Meça tempo de CPU emulada, vídeo/PPU, áudio, escala/rotação, cópia/apresentação, espera de sincronização, SD, JIT e tarefas concorrentes. Inclua uso dos dois cores, heap e latência de entrada quando mensuráveis.
- Registre frames emulados, frames desenhados, frames apresentados, repetidos/descartados, velocidade relativa do sistema, tempo de frame mediano/p95/p99, underruns e latências de leitura. FPS do painel sozinho não prova velocidade correta.
- Use a frequência real do sistema emulado, aproximadamente 59,73 Hz no GBA. Compare sequências temporais de duração conhecida para evitar “60 FPS” com lógica ou áudio desacelerados.
- Faça execuções repetidas com cache frio e quente, trechos leves/pesados e ROMs de tamanhos variados. Use homebrews/testes livres ou arquivos do usuário; não baixe ROMs comerciais para montar a suíte.

#### 8.2 Experimentos para isolar o gargalo

Compare, uma mudança por vez:

1. Base original e novo frontend, com mesmos parâmetros.
2. Emulação sem apresentação de vídeo, depois resolução nativa, depois escala para o display. Bypassar apresentação não significa remover a PPU que afeta o estado da máquina.
3. Áudio normal e saída nula mantendo a temporização emulada; verifique se a espera no áudio limita indevidamente o loop.
4. Cache de ROM/residência em PSRAM e streaming do SD, usando somente tamanhos que caibam no orçamento.
5. Interpretador e dynarec RV32, quando disponível e correto, incluindo pausas de compilação e invalidação.
6. Mudanças pequenas em filas, afinidade, buffers, sincronização e trabalho de UI, preservando correção.

Não obtenha ganho omitindo instruções, alterando a velocidade do jogo ou escondendo quedas com contador incorreto. Frameskip pode ser opção do usuário, sempre separado da velocidade de emulação.

#### 8.3 Candidatos e decisão de núcleo

Investigue primeiro o fork gpSP do **HowBoyAdvance**, que documenta backend RISC-V para outro aparelho com ESP32-P4. Ele é candidato técnico, não um port pronto para Tab5. Compare implementação, licença, bugs, caches, dependências de hardware e compatibilidade do SDK antes de reutilizar.

Investigue também forks RV32 atuais do gpSP quando encontrados em fontes primárias, sem confundir repositório ou roadmap com backend funcional. **mGBA** é candidato para comparação de correção e eventual substituição; não presuma que seu interpretador será mais rápido. Avalie outra opção portátil somente se houver justificativa após essas comparações.

Se o gargalo estiver no frontend, no SD ou no vídeo/áudio, corrija-o antes de responsabilizar o núcleo. Se o limite estiver no backend do gpSP e outro backend/núcleo oferecer resultado melhor, construa um port comparável e teste no Tab5. Não basta recomendar sua troca.

Selecione o vencedor por velocidade emulada, consistência dos frames, áudio, correção, memória, tamanho do app e compatibilidade. Verifique regressões com testes e jogos representativos; JIT rápido com falhas de execução não é melhoria concluída. Mantenha fallback quando útil.

Saves normais podem exigir conversão e validação entre núcleos. Save states normalmente são específicos de núcleo/versão: mantenha namespaces distintos e backup, sem tentar abrir estados incompatíveis como se fossem intercambiáveis.

### 9. Construção incremental do port de Nintendo 64

O objetivo é executar emulação local no Tab5. Faça pesquisa e desenvolvimento reais, com código incremental e resultados medidos. Não substitua isso por streaming de um PC, vídeo gravado, uma entrada vazia no menu ou port nativo de um jogo como Super Mario 64.

#### 9.1 Seleção e prova arquitetural

Examine Mupen64Plus, ParaLLEl-N64 e DaedalusX64, incluindo interpretadores, renderizadores e licenças. Escolha uma base adequada ao **ESP32-P4 RV32**, memória e ausência de uma GPU 3D convencional. Núcleo que exige OpenGL/Vulkan não vira renderizador por software só por compilar parte do projeto.

Verifique suporte real da arquitetura de recompilação: backend x86, ARM, AArch64 ou RV64 não é backend RV32. Se não houver dynarec apropriado, comece com interpretador/interpretador com cache. Implemente um backend RV32 apenas após validar interpretação, identificar o gargalo e estimar seu custo. Reutilização é preferível a escrever um emulador inteiro do zero.

Faça orçamento explícito: RDRAM de 4/8 MiB conforme configuração, estados de CPU/RSP, memória de texturas, vídeo/áudio, cache de ROM e eventual cache JIT, além da aplicação. Não suponha que 32 MB de PSRAM sejam memória livre para ROM. ROMs maiores precisam de estratégia real de leitura/cache ou limitação anunciada.

#### 9.2 Marcos técnicos obrigatórios

1. **Build e execução headless:** integre o núcleo escolhido ao ESP-IDF, remova dependências de desktop não necessárias, carregue ROM de teste e execute instruções com logs limitados. Use testes/homebrews de licença conhecida.
2. **Correção de CPU/memória:** valide operações de 64 bits no host RV32, endianness, exceções, temporizadores, interrupções, CP0 e TLB/MMU no escopo requerido. Não silencie instruções não implementadas.
3. **Primeiro vídeo:** implemente VI/framebuffer e conecte a saída ao vídeo do Tab5; prove saída produzida pelo emulador. Depois avance para RDP/RSP e renderização compatíveis com a base escolhida.
4. **Controle e áudio:** integre PIF/entrada, EASD→analógico, demais botões, saída de áudio e sincronização. Defina a cobertura HLE/LLE de RSP e microcódigos; não prometa compatibilidade total por suportar um único microcódigo.
5. **Execução interativa:** execute pelo menos um homebrew e busque um jogo representativo fornecido pelo usuário, registrando boot, menus, cena jogável, falhas, velocidade e memória. Caso não atinja jogo comercial, documente o marco realmente alcançado.
6. **Persistência e estabilidade:** implemente os saves requeridos pelos títulos suportados, encerramento limpo, troca de ROM e recuperação de erro. EEPROM/SRAM/FlashRAM/Controller Pak devem ter cobertura declarada. Save state só aparece como disponível depois de implementado e validado.
7. **Otimização medida:** reduza resolução interna, trabalho do renderizador, cópias e custo de memória antes de otimizações de risco; avalie HLE e cache. Dynarec RV32 requer testes de geração de código, permissões de execução, coerência de cache, código automodificável e limites de memória na versão real do SDK.

Crie `docs/N64_PORT.md` com núcleo/commit, arquitetura, módulos implementados, testes, orçamento, gargalos e próximos marcos. Crie tabela por ROM/hash, região, configurações, velocidade emulada, FPS apresentado, áudio, estabilidade e erros. Um jogo cuja lógica gera 20/30 FPS não é automaticamente lento por não produzir 60 imagens diferentes; meça velocidade e temporização adequadas ao título.

Não prometa full speed nem compatibilidade geral antes das medições. Se surgir um impedimento técnico substancial, entregue o protótipo real, seus testes e os dados que o demonstram, além de uma rota fundamentada para continuar. Isso deve constar como pendência ou limitação, nunca como port concluído. Preserve o build estável dos outros consoles enquanto continua o desenvolvimento experimental.

### 10. Documentação para capas, descrições e uso

Escreva em português, com exemplos copiáveis e sem exigir conhecimento interno do código:

- `README.md`: instalação, atualização pelo launcher, pacote do cartão, primeiro uso e perfis.
- `docs/ADICIONAR_JOGOS.md`: onde copiar ROMs por console, extensões aceitas, atualização da biblioteca, nome e hash, BIOS quando necessária e mensagens de erro.
- `docs/CAPAS_E_DESCRICOES.md`: passo a passo completo para adicionar capa e descrição, exemplo de catálogo válido, caminhos relativos, caracteres acentuados, formatos/dimensões/limites medidos e recuperação de metadados inválidos.
- `docs/CONTROLES.md`: tabelas EASD/IOJK/TY, N64 completo, remapeamento e combinações verificadas no teclado real.
- `docs/LAUNCHER_PALUDO.md`: contrato encontrado, build compatível, envio, confirmação, atualização dos recursos e retorno/recuperação.
- `docs/ARQUITETURA.md`, `docs/MEMORIA_E_FLASH.md`, `docs/GBA_PERFORMANCE.md`, `docs/N64_PORT.md`, `docs/TESTES.md`, `docs/LIMITACOES.md` e `docs/PROGRESSO.md`.

Forneça ferramenta de computador, preferencialmente Python, para criar/validar catálogo, localizar arquivos ausentes, converter `metadata.csv` quando aplicável e gerar miniaturas mantendo proporção. Fixe dimensões e tamanho máximo após medir o orçamento do frontend. A ferramenta não deve exigir baixar capas de um serviço específico, credenciais externas ou editar código para cada jogo.

Inclua um tutorial: copiar uma ROM, colocar uma capa, preencher título/descrição, validar, inserir cartão e atualizar biblioteca. Inclua exemplo de jogo sem capa e exemplo com acentos. Descreva como atualizar um pacote sem apagar dados pessoais. Copie a documentação de uso para `Tab5-Emu/docs/` no pacote final.

### 11. Etapas de execução e entregas

| Etapa | Resultado concreto | Verificação mínima |
|---|---|---|
| 0 — Auditoria e baseline | Projeto upstream reproduzível; contrato do launcher e orçamento | Compilação, mapa do linker e matriz de compatibilidade |
| 1 — Plataforma e HOME | UI real semelhante à base, SD externo, bateria e teclado I²C | Boot com/sem SD, teste de teclas e atualização pelo launcher quando disponível |
| 2 — Consoles existentes | SNES, GB/GBC, GBA e Mega Drive integrados à nova estrutura | Carregar/jogar/salvar/sair, vídeo, áudio e regressão |
| 3 — Biblioteca documentada | Capas, descrições, configurações, ferramenta e pacote SD | Catálogo válido/inválido, imagens ausentes e tutorial reproduzível |
| 4 — GBA medido e melhorado | Relatório gpSP, correções e port alternativo se justificado | A/B reproduzível e comparação de correção/desempenho |
| 5 — N64 inicial | Port compilável e execução headless/primeiro vídeo | Testes reais de CPU/memória e ROM livre |
| 6 — N64 interativo | Vídeo, controle, áudio e saves no escopo demonstrado | Medições por título e limitações verificadas |
| 7 — Consolidação | Pacotes versionados, documentação e integração final | Instalar, iniciar e retornar pelo Launcher do Paludo |

Ao final de cada marco executável, compile e gere os artefatos. Envie-os pelo launcher quando o ambiente configurado permitir, confirmando o resultado. Não confunda o arquivo de uma etapa antiga com o build que acabou de testar. Registre versão/commit/hash por entrega.

Etapas organizam o trabalho; não significam parar indefinidamente após cada uma. Se o trabalho atravessar sessões, atualize `docs/PROGRESSO.md` com estado real, comandos que funcionaram, testes pendentes e próxima ação concreta.

### 12. Testes e critérios de aceite

Faça testes de host nos parsers, catálogo/caminhos, perfis de controle, conversão de formatos e integração simulada quando houver contrato conhecido. Para emulação, use testes livres, comparação determinística quando possível e verificação de regressões. Não adicione testes superficiais que apenas repetem a implementação.

No dispositivo, verifique ao menos:

- Boot com cartão íntegro, ausente, incompatível e recursos faltantes; UI de recuperação continua legível.
- HOME utilizável por teclado e touch; bateria corresponde à leitura real; temas/brilho/volume persistem.
- E+A, E+D, S+A, S+D; movimento com O/K/I/J; Start/Select e teclas de ombro; limites de rollover registrados.
- Conectar/desconectar teclado, trocar jogo, pausar, acordar tela e retornar ao launcher sem teclas presas.
- Carregar ROMs de cada sistema, saves normais, estados suportados, erro em ROM inválida e troca repetida de núcleo sem vazamentos.
- Capas grandes/inválidas, catálogo com acentos e nomes com espaços, biblioteca numerosa e atualização de cache.
- SD cheio, remoção durante acesso e gravação interrompida sem sobrescrever o último save íntegro. Não faça ensaios destrutivos com os únicos saves do usuário.
- Instrumentação de GBA separa velocidade emulada de imagens apresentadas e mantém comparabilidade dos testes.
- N64 executa código do núcleo e produz resultados reais; recursos ausentes estão desabilitados ou identificados.
- Pacote app-only cabe no destino; hash corresponde ao recebido; recursos corretos são carregados; launcher e outros apps permanecem funcionais.

Sem hardware conectado, avance em código, build, testes de host e empacotamento, mas marque os testes físicos como pendentes. Não invente medições, screenshots do dispositivo ou sucesso de instalação.

### 13. Entrega final exigida

Entregue repositório organizado, dependências/commits fixados, patches pequenos, builds reproduzíveis e licenças/créditos. Respeite as obrigações de distribuição dos núcleos e disponibilize o código correspondente quando exigido. Não atribua licença única a todo o projeto sem auditar componentes.

Forneça binários app-only, ZIP do microSD, hashes, documentação, resultados de testes e relatório final curto com:

1. Recursos efetivamente disponíveis e comandos para reproduzir o build.
2. Tamanho do app e orçamento de memória medidos.
3. O que foi enviado/instalado/iniciado pelo Launcher do Paludo, com evidência.
4. Conclusão fundamentada sobre gpSP: gargalo identificado, correção ou núcleo alternativo, ganhos e regressões.
5. Estado real do N64: marcos alcançados, títulos/testes, desempenho e pendências.
6. Instruções para adicionar ROMs, capas e descrições e para remapear controles.

**Resultado desejado:** ligar o Tab5 pelo Launcher do Paludo, abrir uma HOME semelhante ao tab5-emu original, navegar e jogar com o teclado I²C usando EASD/IOJK/TY, manter o conteúdo em `/Tab5-Emu` no microSD, ter GBA com desempenho investigado e melhorado com evidências e um port de N64 construído progressivamente, com suas capacidades demonstradas de forma honesta.

## FIM DO PROMPT

## Referências para a implementação

Confirme os branches e commits atuais antes de implementar. As referências abaixo orientam a pesquisa e não afirmam que todos os recursos solicitados já existam nelas.

- [tab5-emu — base principal](https://github.com/esp-cpp/tab5-emu)
- [M5Stack — hardware Tab5](https://docs.m5stack.com/en/core/Tab5)
- [M5Stack — teclado oficial Tab5](https://docs.m5stack.com/en/tab5/Tab5_Keyboard)
- [M5Stack — exemplos do teclado](https://docs.m5stack.com/en/arduino/projects/tab5/tab5_keyboard)
- [HowBoyAdvance — gpSP com backend RISC-V para Tanmatsu](https://github.com/Irak4t0n/HowBoyAdvance)
- [gpSP — referência libretro](https://github.com/libretro/gpsp)
- [mGBA — candidato a avaliação](https://github.com/mgba-emu/mgba)
- [Mupen64Plus — candidato N64](https://github.com/mupen64plus/mupen64plus-core)
- [ParaLLEl-N64 — candidato N64](https://github.com/libretro/parallel-n64)
- [DaedalusX64 — candidato N64](https://github.com/DaedalusX64/daedalus)
- [ESP-IDF — OTA no ESP32-P4](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/ota.html)
- [ESP-IDF — tabelas de partições](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/partition-tables.html)

O documento anterior `prompt_launcher_tab5.md` descreve objetivos do launcher pessoal; o contrato de integração desta implementação precisa ser obtido do código/configuração efetivos. Não há endereço de rede, endpoint, slot ou offset pessoal presumido neste prompt.
