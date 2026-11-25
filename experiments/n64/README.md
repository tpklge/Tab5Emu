# Ensaio R4300 para ESP32-P4

Este é um ensaio de integração do **interpretador puro real do Mupen64Plus**, não um emulador N64 jogável. Não há boot de cartucho, PIF, RSP/RDP, VI/framebuffer, áudio, TLB funcional ou scheduler de interrupções neste harness. Funcionalidades ausentes encerram o ensaio com motivo explícito. A exceção BREAK registra Cause/EPC e termina o teste; não executa um handler de console.

A base está em `mupen/UPSTREAM.json`; fontes e avisos originais estão preservados. `probe/pure_interp_probe.c` deriva do `pure_interp.c` desse commit, acrescentando somente um hook que conta instruções realmente interpretadas e impõe limite de 4096. Os acessores usam o layout original. CP1/CP2 são compilados do upstream; o teste cobre somente transferência de registradores CP1, sem declarar FPU validada.

`tools/build_n64_probe.py` gera um programa próprio 0BSD de 92 bytes: operações e shifts de 64 bits, HI/LO, load/store de 64 bits em ordem big endian, desvio com delay slot, registrador zero, transferência CP1 e BREAK. A expectativa é independente da saída do núcleo. No host foram interpretadas 22 instruções e os resultados conferiram. O programa não contém BIOS/logo/código de jogo e **não é embutido no app**: é enviado pelo computador à USB após o firmware identificar-se.

Build app-only, a partir da raiz do repositório com ESP-IDF 6.1 ativado:

```sh
python3 tools/build_n64_probe.py
idf.py -C experiments/n64 -B build-n64-probe app
```

O diretório `build` também foi usado num ensaio local; é saída gerada, não conteúdo da distribuição. Nunca use `flash`, `app-flash`, bootloader ou tabela do build neste Tab5. Instalação e restauração do stable são feitas pelo Launcher, na mesma reserva app4. `project_name` permanece `tab5-emu` para identificar o app pertencente a este projeto.

A USB nativa envia `TAB5N64_READY` com versão, ELF completo, revisão do chip, clock e SHA esperado. O host envia uma única linha `N64P <hex-do-arquivo>`. O firmware exige tamanho e SHA exatos, executa o núcleo, publica `TAB5N64_RESULT` e retorna ao Launcher pelo contrato já verificado. Sem programa válido, encerra após prazo de 30 segundos e também retorna. Não altera SD, saves ou preferências.

Este ensaio ainda preserva as tabelas de TLB/cache do layout upstream, apesar de não utilizá-las; o tamanho de `r4300_core` é medido, não estimado como RAM disponível para um console completo. Próximos marcos exigem memória/dispositivos/timers reais, testes mais amplos de CPU/CP0/TLB/FPU, RDRAM e boot livre, VI e renderização antes de controle/áudio/jogos. A implementação não declara suporte a ROMs N64 na HOME.

## Perfil integrado e prova física

A 0.1.9 executou o mesmo programa no P4: 22 instruções, Cause 36, EPC 0x80000058 e core de 13635168 bytes. O firmware standalone pequeno não chegou a executar no aparelho: o Launcher exigiria encolhimento da reserva e o plano foi recusado.

O perfil integrado atual é `bash tools/build.sh n64-cpu-probe`, com programa no SD em `Tab5-Emu/benchmarks/tab5-n64-cpu-probe.mips`. Usa o frontend e diagnóstico USB normais, escreve relatório por versão/ELF e compartilha conteúdo. GB/GBC é omitido somente nesse perfil para respeitar a reserva existente; stable conserva os cinco sistemas maduros. O ensaio é headless e não representa suporte a jogos N64.

Além do caso positivo, o host verifica explicitamente tamanho inválido, orçamento de instruções, idle loop dependente de scheduler, acesso fora da RAM e TLB. Esses casos devem falhar com motivo, sem executar um dispositivo fictício.
