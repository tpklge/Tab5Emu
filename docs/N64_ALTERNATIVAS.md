# Alternativas para N64 no Tab5

A versão física 0.2.1-sd usa Mupen64Plus puro, CXD4 e Angrylion em software. O candidato 0.2.2-sd acrescenta o porte gráfico HLE do Daedalus, ainda sem validação física; detalhes em [N64_PORT.md](N64_PORT.md). O resultado físico anterior foi de cerca de 1–2 VI/s em cenas de SM64. Não há promessa de que trocar apenas o nome do núcleo produza velocidade normal no ESP32-P4.

| Base | Possível contribuição | Trabalho necessário no Tab5 |
|---|---|---|
| [DaedalusX64](https://github.com/DaedalusX64/daedalus) | Arquitetura voltada também a PSP/3DS/Vita e processamento gráfico HLE | Adaptar frontend, memória e renderizador. Backends das plataformas originais não equivalem ao P4; não há port RV32 pronto validado aqui |
| [Mupen64Plus RSP HLE](https://github.com/mupen64plus/mupen64plus-rsp-hle) | Tratar tarefas conhecidas de áudio/gráficos em nível alto e reduzir instruções RSP | A parte gráfica exige um processador de display lists/renderizador HLE; não é substituição direta por Angrylion, que recebe comandos RDP |
| [ParaLLEl-RDP](https://github.com/Themaister/parallel-rdp) | Renderização precisa acelerada por GPU | Exige Vulkan compute, incluindo Vulkan 1.1 e recursos de armazenamento de 8/16 bits. O BSP deste projeto não fornece esse ambiente; não é alternativa direta para o Tab5 |

A direção de maior potencial é HLE para tarefas conhecidas com um renderizador leve e fallback para o caminho atual. O protótipo gráfico Daedalus já compila com renderizador nativo e executa cenas no host; CPU, áudio e dispositivos continuam nos componentes anteriores. O caminho LLE continua como referência de regressão. A integração híbrida ainda exige medição e validação no P4.

As fontes primárias acima e seus arquivos de build foram consultados em 24/11/2026. Licenças devem continuar preservadas por componente, sem atribuir uma licença única a toda a combinação.
