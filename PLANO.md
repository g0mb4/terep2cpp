# Terep2 → C legível: análise e plano (histórico; o estado atual está no README.md)

## O que tem no projeto

| Caminho | O que é |
|---|---|
| `reasm/maincode.asm` | Disassembly (NASM, 16-bit real mode + regs 32-bit), ~9000 linhas, 124 funções com nomes do Ghidra (`FUN_1000_xxxx`, alguns já marcados PHYSICS/INIT/SHARED). |
| `memdumps/data.bin` | Dump do segmento de dados (62 KB): strings, nomes de arquivos, paleta, fonte, tabelas (seno etc.) e todas as variáveis globais. O código acessa tudo por offset fixo (`[0x5bba]`, `[0xdb10]`...). |
| `common/*.asm` | Macros e variáveis novas (temporários criados para tirar o uso de BP como registrador). |
| `win16/` | Host Win16 (Watcom): janela, timers de 120 Hz (física) e 60 Hz (render), teclado → scancode, StretchDIBits do framebuffer. |
| `previous_effort/` | Tradução mecânica 1:1 do asm para C++ (struct `cpu_ctx` com registradores, `MEM_WORD(0x...)`, gotos). Compila, mas faltava o host. |
| `track_file_samples/Asphalt` | Pista de exemplo: `map.pcx` (altura), `col.pcx` (cor + paleta), `textures.pcx`, `maptex.pcx`, `fog.dat`, `smoke.dat`, `car1..5.dat` (modelo/física de cada carro), `car*.pcx` (texturas). |

## Arquitetura do jogo (o que já deu para entender)

- **Init** (`f_init`): lê `sim.cfg` (opcional), aloca 4 segmentos de 64 KB (mapa de altura, cor do terreno, texturas, textura do mapa), carrega os PCX, `fog.dat` e `smoke.dat`, depois carrega até 5 carros. Cada `carN.dat` é copiado *inteiro* para o segmento de dados a partir de 0x5bd0; a tabela em 0x5bbc guarda o offset de cada carro. O arquivo do carro **é** a estrutura do carro: cabeçalho de 0x84 bytes (estado da física, câmera, ponteiro de textura) + lista de vértices de 0x1c bytes (posição 16.16 + velocidade, é um soft-body massa-mola, daí o "DEFORMERS") + molas + polígonos.
- **Física** (`FUN_timer_5680`, 120 Hz): para cada carro chama `0d2a`, `1004` e `48d0`, depois `500b`, `0bb5`, `0a3b` (palpite ainda não confirmado: entrada do teclado, molas/integração, contato com o terreno, colisão carro-carro).
- **Render** (`FUN_main_render`): 5 câmeras (F1–F5), split-screen (F9), terreno a partir do heightmap, carros como polígonos texturizados com backface culling, texto com fonte bitmap (detalhes do terreno e da fumaça ainda a confirmar).
- **Som**: escrita em registradores AdLib/OPL2 (porta 0x388), hoje stub.

## Como garantir que nada quebrou

Montei um host Linux headless para o `previous_effort` (implementa os serviços DOS usados: abrir/ler/seek/fechar/alocar). Ele roda a pista de exemplo, injeta teclas por script e grava o hash do framebuffer e da memória a cada frame (e PPM se quiser). Isso vira o **oráculo**: cada função reescrita precisa produzir exatamente os mesmos hashes.

## Plano de conversão

1. Base em C puro (sem `cpu_ctx`, sem gotos de assembly), mantendo o layout de memória original para comparação bit a bit.
2. Reescrever função por função, em ordem de baixo para cima (matemática → física → rasterizadores → render → init), trocando registradores por parâmetros e retornos e offsets por nomes.
3. Recuperar as estruturas: `Car` (cabeçalho + vértices + molas + faces), `Camera`, `Viewport`, cabeçalho PCX, tabelas globais, fonte.
4. No fim, trocar o "blob" do segmento de dados por globais tipadas e tabelas constantes extraídas, mantendo o teste de hash do framebuffer.
5. Hosts: teste headless (Linux) + host SDL2 opcional. O alvo é C99 portável (32/64-bit); **não** vou manter compatibilidade com Win16 16-bit, a não ser que você peça.
