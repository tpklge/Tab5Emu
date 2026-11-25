# Bibliotecas grandes e índice no SD

O catálogo JSON continua sendo a fonte editável. Para bibliotecas grandes, gere no computador um índice reconstruível:

```sh
.venv/bin/python tools/library_index.py --root /caminho/do/cartao/Tab5-Emu
```

A ferramenta preserva ROMs, saves, metadados e preferências; escreve somente `cache/library-v1.t5i` após concluir a nova geração. Importa metadados válidos e descobre arquivos sem metadados nos diretórios dos cinco sistemas. N64 não entra enquanto não houver port jogável. O limite do gerador é 64 MiB de JSON e 100000 jogos. Falha de parsing não substitui o índice anterior. Entradas inválidas aparecem no relatório; não são tratadas como jogos válidos.

A HOME carrega até 64 jogos por página. Os botões inferiores mudam a página; a seleção de sistema, a busca, favoritos e recentes consultam o conjunto do índice. As consultas rodam no worker, com cancelamento quando muda a consulta, começa um jogo ou o cartão passa ao USB. A UI indica carregamento e desabilita Play até aplicar o resultado. As capas continuam com fila/cache limitados. Os caches de identidade e contagem de estados têm até 128 entradas cada, inclusive ao visitar muitas páginas. Não se monta a árvore JSON inteira nem uma lista de 100000 objetos na PSRAM.

A leitura verifica tamanho, versão, CRC32, campos, caminhos, extensões e UTF-8. Tamanho e CRC32 do JSON de origem também são conferidos: copiar arquivos por USB não depende de preservar timestamps FAT. CRC32 detecta corrupção acidental; não é assinatura de autenticidade. Um índice inválido/desatualizado não é sobrescrito pelo firmware: o aplicativo usa a descoberta anterior, limitada a 2048 entradas, e indica que o índice precisa ser reconstruído. Depois de adicionar/mudar conteúdo, gere o índice novamente e recarregue a biblioteca. Sem índice, o fluxo anterior permanece disponível.

## Formato do cache

Header de 64 bytes, little endian: magic `T5LIBI01` (8); versão u32=1; contagem u32; tamanho total u32; CRC32 do payload u32; tamanho do JSON u64; CRC32 do JSON em u64; 24 bytes reservados zero. Cada registro tem seis comprimentos u16 seguidos dos textos UTF-8: sistema (16 bytes), título (160), ROM relativa (240), capa relativa (240), descrição (2048), ID (80). O parser usa scratch de 1 KiB e rejeita registros fora dos limites. A página mantém até 64 registros; as estruturas da UI também são limitadas a essa página. O arquivo é percorrido em streaming para filtros e busca; não é um banco com busca indexada sublinear.

A API USB antiga `LIST_ROMS` conserva a lista da página carregada. Os campos `library` e `library_loading` em `settings` identificam índice, total e página; `settings library_page=N` seleciona uma página, contando de zero. Espere `library_loading=false` antes de consultar essa lista.

O protocolo 6 acrescenta `CATALOG_QUERY` (0x17): `offset NUL sistema NUL busca`, com offset decimal até 100000, sistema opcional e busca UTF-8 de até 128 bytes. A resposta tem `total`, `offset`, `roms` e `next`; use `next` para continuar, inclusive quando o limite do pacote fragmenta uma página de 64. As consultas usam o worker existente e não mudam o filtro/página da HOME. São canceladas ao iniciar jogo ou transferir o SD; não mantêm 100000 objetos em memória. `launch` resolve caminho/nome no catálogo completo, privilegiando correspondência exata, antes de postar a seleção na tarefa principal. Sem índice, consulta a biblioteca limitada já carregada. O CLI concede até 40 segundos ao launch; cada consulta tem limite próprio de 15 segundos, após sincronização com a tarefa principal (até 5 segundos). O helper concede 25 segundos ao pedido completo.

```sh
.venv/bin/python tools/emulator_usb.py catalog --offset 4928 --system gba --search acao
.venv/bin/python tools/emulator_usb.py launch 'Tab5-Emu/roms/gba/meu-jogo.gba'
```

O ensaio físico de 5000 entradas encontrou uma falha ao remover o último favorito: o callback de capa ainda usava índices da página anterior durante a troca para uma lista vazia. A transição agora invalida esses índices antes de reconstruir o filtro, e o acesso à capa confere também o tamanho da página nova. O teste de regressão inclui página vazia, CRC inválido e restauração do catálogo original.

Na retomada USB, o teste encontrou um erro de leitura que `std::ifstream` transformava em exceção interna, abortando o SDK sem exceções C++. O leitor do índice agora usa `fread/ferror`, distingue falha de I/O de índice inválido e faz uma tentativa adicional de leitura após a remontagem. Uma falha persistente preserva a página/cache e pede recarga, sem entrar no parser legado durante a indisponibilidade. Cancelamento conserva a exclusão do SD até o worker terminar.

A transferência ao computador suspende os leitores do firmware. Após ejetar pelo sistema operacional e desativar o drive, callbacks MSC READ10/WRITE10/Ready atrasados são recusados antes de acessar o cartão; o fechamento aguarda o callback em andamento e os barriers do driver drenam operações aceitas antes do mount FAT. Os contadores `msc_io` expõem recusas e ownership. Erros de I/O do índice usam stdio sem lançar exceções no SDK; preservam o modelo e desabilitam Play até uma leitura válida.
