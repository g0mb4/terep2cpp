# TODO

- [x] Parar de carregar o `data.bin`: colocar as constantes direto no código e inicializar o
      resto dinamicamente.
- [x] Parar de carregar os `carN.dat` direto na memória: interpretar o arquivo original e
      carregar de forma inteligente, para que as structs não precisem mais ser `packed`.
- [x] Tipos `Angle` (0x10000 = 360°) e `Q15` (seno/cosseno, matrizes), como `Pos`/`Vec3`, no
      lugar de `uint16_t`/`int16_t` soltos (`build_matrix`, câmeras, rodas).
- [ ] Quebrar o `DataSeg` em estruturas por assunto (jogo, câmeras, renderizador, tabelas).
      Depende dos dois primeiros itens.
- [ ] `make test` mais completo: os 12 roteiros, teste rápido em 4x com ASan/UBSan, build com
      g++ e clang; CI no GitHub Actions.
- [ ] Carregar outra pista sem fechar a aplicação (hoje o estado e os segmentos são montados
      uma vez só no `game_init`).
- [ ] Talvez: opção para corrigir o bug do pulo que corrompe o título (modo fiel continua o
      padrão). Os bugs do renderizador ficam como estão.

Fora do escopo daqui: som (feito em outro branch, só adaptar), widescreen (só no renderizador
novo).
