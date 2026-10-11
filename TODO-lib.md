# TODO: renderizador OpenGL externo (a lib)

O que falta para desenhar o jogo com a lib OpenGL, em vez do renderizador por software. O que já
existe no host está em `host/renderer_api.hpp` (contexto, arquivos do mapa, camadas do mapa).

## Contexto e quadro

- [x] Contexto OpenGL 3.3 core único, do host (`host/gui.cpp`): janela `SDL_WINDOW_OPENGL`,
      vsync, ImGui pelo backend OpenGL3 (`#version 330 core`).
- [ ] A lib aceita a janela e o contexto do host (`gui_window()`, `gui_context()`) em vez de
      criar os seus, desenha no framebuffer 0 e **não** chama `SDL_GL_SwapWindow` (o host desenha
      o ImGui por cima e faz o swap em `gui_end()`).
- [ ] No fim do quadro a lib deixa o framebuffer 0 ligado. O backend OpenGL3 do ImGui salva e
      restaura o resto do estado que usa.
- [ ] Core profile: nada de pipeline fixo (`glBegin`, matrizes do GL); funções além do GL 1.1
      precisam de loader. O ImGui tem o loader interno dele, só para ele; a lib traz o seu
      (glad, epoxy...) ou carrega pelo `SDL_GL_GetProcAddress`.
- [ ] Encaixe em `run()` (`host/sdl_main.cpp`): onde hoje o quadro por software vira textura
      (`picture.upload` + `AddImage`), chamar a lib (entre `gui_begin()` e `gui_end()`, antes do
      `ImGui::Render`, ou com um callback de draw list), e não chamar `game_frame()`.
- [ ] Escolher o renderizador: flag (ex. `-r gl`) e um combo no seletor; o padrão continua o por
      software.
- [ ] Alternativas, se a lib não puder dividir o contexto: dois contextos na mesma janela com
      `SDL_GL_SHARE_WITH_CURRENT_CONTEXT` e `SDL_GL_MakeCurrent` + `glFlush` a cada troca, ou a
      lib desenhando num FBO que o host mostra como textura.

## Estado do jogo que a lib lê

- [x] Arquivos do mapa com os nomes reais (`map_files()`): map, col, maptex, textures, fog,
      smoke e, por carro carregado, `carN.dat` e `carN.pcx` (vazio se o carro usa textures.pcx).
- [x] Camadas do mapa (`map_layers()`): altura e tile, 256 x 256, em `[x | y << 8]`.
- [ ] O mapa muda durante o jogo: o gelo lascado baixa a altura de um ponto e o nibble baixo do
      tile. Reler as camadas a cada quadro (64 KB cada) ou comparar e atualizar só o que mudou.
- [ ] Carros: `cars[i].points` (posição 8.24 em células / 16.16 em unidades do mapa, mudam a
      cada tick: corpo mole, VBO dinâmico); `cars[i].model_ops` (polígonos decodificados: pontos
      em `G->car_proj`, cor, uv, sprites, rodas). Com z-buffer, SORT2/FACING/JUMP_* (ordem do
      pintor) podem ser ignorados; manter o descarte de faces de costas. `car.steer_used` para
      virar as rodas da frente; `car.ref_vertex()` é o ponto da câmera F1.
- [ ] Partículas: `G->particles` (`G->particle_bytes / sizeof(Particle)`), fumaça, faíscas e
      lascas de gelo, desenhadas como sprites (`G->smoke_sprite`).
- [ ] Texto: título, subtítulo e a linha "GAMBIARRA FOREVER!" (`hide-banner`), se quiser; pode
      ser pelo ImGui.

## O que o `game_frame()` faz e deixa de ser feito

- [ ] Som: chamar `engine_sound(G->player_car[0], 0)` a cada quadro de 60 Hz (e
      `engine_sound(G->player_car[1], 1)` com a tela dividida), como o original.
- [ ] Teclas tratadas no `game_frame()`, a tratar no host quando ele não for chamado:
      Tab / Q (`G->player_car[0/1]`: qual carro cada jogador segue, que também decide quem pula
      com 1/2 e o som do motor), `[` / `]` (gravidade, estado da física), F9 (tela dividida),
      F1–F6, F10 e o numpad (câmeras, que a lib substitui pela dela).
- [ ] O `keys[0]` (última tecla) é zerado pelo `game_frame()`; sem ele, quem tratar as teclas
      zera.

## Build

- [ ] `Makefile`: variável para a lib (ex. `make sdl GLR=caminho/da/lib`) com o `.h` e o `.a`, e
      o loader dela. O `terep2` sem a lib continua compilando sem dependência nova
      (`GL_LIBS`: `-lGL` no Linux, `-lopengl32` no Windows).
