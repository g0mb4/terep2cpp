# terep2 em C++

Reescrita em C++ legível (estilo "C com classes") do **Terep2** (Nagymáthé Dénes, 1996–97, "-DEFORMERS- Atomic Vehicle
Simulation System"), feita a partir do disassembly em `terep2isolado/reasm/maincode.asm` e da
versão Win16. O comportamento é idêntico ao do original quadro a quadro: o framebuffer e o
estado do jogo batem byte a byte com o asm original (veja [Verificação](#verificação)). A
exceção é um bug corrigido de propósito (o do pulo que corrompe o título).

## Compilar e rodar

```sh
make            # terep2-headless: host de teste, sem dependências
make sdl        # terep2: versão jogável com SDL2 (precisa de libsdl2-dev)
make test       # roda os cenários gravados e compara com a referência
./terep2        # usa data/Asphalt
./terep2 outra/pista
./terep2 -s 4     # renderiza em 1280x800 (escala 1 a 8)
```

C++17 com extensões GNU (`__attribute__((packed))`, `-fwrapv` e `#embed`), então compila com g++ 15+ ou clang++ 19+. Testado em Linux x86-64 com os dois. O código não depende do tamanho de ponteiro, mas não testei em 32 bits. A física roda a 120 Hz e a tela a
60 Hz, como no port Win16. A janela tem escala 3x. Esc sai.

### Resolução maior

`-s N` (nos dois hosts) renderiza em 320·N x 200·N. A escala 1 é o original, byte a byte. Nas
escalas maiores muda só o desenho; o estado do jogo é idêntico em qualquer escala:

- a tela, os recortes e as tabelas de scanline crescem junto (`render_scale`, `screen_w()`,
  `screen_h()`; o framebuffer grande fica fora do segmento de dados);
- a projeção usa a posição 16.16 inteira, relativa ao olho, em vez das unidades inteiras do
  original (`view_transform_fine` em `scene.cpp`). Com unidades inteiras um carro perto da
  câmera pula vários pixels por vez, o que a 4x vira uma tremedeira bem visível. O espaço de
  câmera em inteiros continua sendo usado no plano próximo e nos testes de face;
- texto, sprites, rodas e os pixels do modelo do carro escalam pelo mesmo fator.

O resto (textura afim, Gouraud, pontilhado) é o rasterizador original, só mais fino.
[`docs/alta_resolucao.gif`](docs/alta_resolucao.gif) compara 1x ampliado e 4x.

### Controles

| Tecla | Ação |
|---|---|
| setas + 5 do teclado numérico | carro 1 (acelera, freia, esquerda, direita, freio de mão) |
| W Z A D S | carro 2 |
| I M J L K | carro 3 |
| T V F H G | carro 4 |
| 7 9 1 3 . (teclado numérico) | carro 5 |
| 1 / 2 | "pulo" do carro da tela 1 / tela 2 |
| F1–F5 | câmera: no carro, no carro suavizada, parada, perseguição, todos os carros |
| F6 | troca a câmera da tela 2 |
| F9 | tela dividida |
| F10 | neblina perto/longe |
| Tab / Q | troca o carro seguido pela tela 1 / tela 2 |
| [ ] | aumenta / diminui a gravidade |
| + − / * (numérico) | altura e distância da câmera de perseguição |

## Estrutura

```
src/            o jogo (não depende de SO nem de biblioteca)
  terep.hpp     tipos de valor (Vec3, Coord, Pos), estruturas (Car, CarVertex, CarSpring,
                Particle, ChaseCam, ProjVertex, PcxHeader...) e protótipos
  fixed.hpp     ponto fixo: Angle (0x10000 = 360°), Q15 (seno/cosseno, matrizes), multiplicações
  dataseg.def   mapa do segmento de dados original, campo a campo (fonte da verdade)
  dataseg_init.cpp  conteúdo inicial do segmento (tabelas, fonte, nomes, ajustes), no código
  font.pbm      a fonte (PBM texto, glifos de 5x5 um embaixo do outro), embutida com #embed
  dataseg.hpp   struct DataSeg gerada a partir do .def (tools/gen_dataseg.py)
  math.cpp      seno/cosseno por tabela, atan2, comprimento de vetor, matriz de rotação
  terrain.cpp   altura do terreno (2 triângulos por célula do heightmap)
  physics.cpp   corpo mole (métodos de Car): controles, rodas, molas, integração, colisão, partículas
  camera.cpp    as 5 câmeras
  raster.cpp    recorte de polígonos, plano próximo, preenchimento flat/Gouraud/texturizado
  scene.cpp     área visível, terreno por algoritmo do pintor, sprites e partículas
  car_draw.cpp  transformação dos vértices do carro e o interpretador do modelo (bytecode)
  text.cpp      fonte bitmap
  sound.cpp     registradores do AdLib (o host decide o que fazer com eles)
  car_file.cpp  formato do carN.dat: car_read, car_write
  load.cpp      PCX, mapa, tabelas de neblina/translucidez, carros, game_init
  game.cpp      um quadro (telas, texto, teclas) e o teclado
host/
  host.hpp      o que os hosts compartilham
  common.cpp    segmentos de 64 KB, criação do segmento de dados, passo de tempo, abrir arquivos sem caixa
  headless.cpp  host de teste: roda N quadros com teclas roteirizadas e imprime CRCs
  sdl_main.cpp  host SDL2
data/           a pista Asphalt e o data.bin (segmento de dados do original, só para os oráculos)
tests/          run_tests.sh, máscara do segmento de dados e referências (ref/*.txt = teclas, *.crc)
tools/          oráculos usados na verificação (abaixo), gerador do dataseg.hpp, gerador de roteiros
```

### Estilo C++

C++ só onde deixa o código mais claro; o resto continua no estilo clássico (funções livres,
`printf`, `memcpy`; da biblioteca padrão só `std::swap` e `std::vector` (nos carros); sem exceções, sem herança):

- **Tipos de valor para o ponto fixo.** `Vec3` (16.16 com sinal: velocidades, forças, normais)
  com `+ - += -= >>`; `Coord` (uma coordenada de posição, 8.24 em células sem sinal, com
  `cell()`, `units()`, `Coord - Coord` = distância com sinal) e `Pos` (três `Coord`, com
  `Pos + Vec3` e `Pos - Pos` = `Vec3`). Somar uma posição a outra não compila, o que já pegaria
  o tipo de confusão que existia entre int32 e uint32. `Angle` (0x10000 = 360°, dá a volta
  sozinho, compara e desloca com sinal como o original; `DEG45`, `DEG90`, `DEG180`) e `Q15`
  (seno/cosseno e matrizes; `Q15 * Q15`, `int16_t * Q15`, `neg_sat()`, `q31()`): `fx_sin`,
  `fx_cos` e `fx_atan2` usam esses tipos, assim como `build_matrix`, `rotate`, as câmeras
  (`Angles` com `roll`, `pitch`, `yaw`) e o volante (`Car::steer`). `.raw` dá o valor cru
  onde o original faz conta com ele (deslocamentos, produtos de 32 bits).
- **Métodos no carro.** `Car` é o cabeçalho do `carN.dat` com `vertices()`, `springs()` e a física
  como métodos (`car.update_springs()`, `car.jump(...)`); `CarSpring::type()/softness()`,
  `CarVertex::is_wheel()`, `Particle::ICE_CHIP` etc.
- **Referências, `bool`, `enum`, lambdas** no lugar de ponteiros de saída, flags int, `#define`
  e macros locais; nada de `goto`.
- `#pragma once` em todos os cabeçalhos; as structs do segmento de dados são trivialmente
  copiáveis, sem construtores.

Uma regra que pesa: **as estruturas que moram no segmento de dados são `packed`** (o layout tem
que bater com o segmento original) e o C++ não deixa ligar uma referência a um campo de struct
packed (`int &x = G->gravity` não compila). Nesses casos o código usa uma cópia e grava de volta,
ou um ponteiro. Chamar método em campo packed funciona normalmente. Os carros (`Car`,
`CarVertex`, `CarSpring`) e os tipos de valor (`Vec3`, `Coord`, `Pos`, `Angle`, `Q15`) são
structs normais.

### Por que ainda existe um "segmento de dados"

O original guarda tudo num segmento de 64 KB: a tabela de offsets da fonte dá a volta em 16
bits, os nomes dos arquivos são achados por offset e o bug do pulo escrevia no título. Por isso o
estado global continua numa `struct DataSeg` com o layout original, mas todo campo tem nome e
tipo (`G->gravity`, `G->chase_cam[1]`...). Os acessos por offset restantes (`ds_ptr`) ficam onde
o original realmente aritmetiza endereços: fonte, tabelas de nomes de arquivo, quads da fumaça.
Variáveis de rascunho do asm viraram variáveis locais. O que fica fora do segmento: os carros,
o teclado (`keys`), os segmentos de imagem (heightmap, mapa de cores, texturas, tela, textura de
cada carro) e o host.

### Carros

O original copia o `carN.dat` inteiro para dentro do segmento e usa o arquivo como a própria
estrutura do carro: o cabeçalho guarda o estado (direção, acelerador, rodas...), os pontos e as
molas são lidos por offset e o modelo é um programa cujo contador é um offset no segmento. Aqui
`car_read` (`src/car_file.cpp`) interpreta o arquivo para um `Car` com `std::vector` de pontos,
molas e o programa do modelo (o contador agora é um índice nesse vetor). `car_write` faz o
caminho inverso, e `cars_to_dataseg` monta uma cópia do segmento com os carros onde o original os
teria; o host de teste calcula o CRC sobre essa cópia, então a comparação com o original
continua byte a byte. O bug do título foi corrigido em `Car::jump`.

O conteúdo inicial do segmento, que o original lia do `data.bin`, é escrito por `dataseg_init()`
(`src/dataseg_init.cpp`): textos e nomes de arquivo como strings, a fonte lida de `src/font.pbm`
(embutido no executável com `#embed`), as tabelas de superfície, teclas e casco como arrays, a
tabela de seno calculada. A tabela de atan fica como constante porque não é um atan arredondado
exato. Sobras do DOS que nada lê (mensagens do XMS, uma sequência de som antiga) continuam lá,
para o segmento sair byte a byte igual ao original e os testes continuarem comparando com o asm.

## Verificação

Dois oráculos, os dois em `tools/`:

- **asm original** (`tools/asm_oracle`): o próprio `maincode.asm` montado com nasm e executado
  num emulador x86 16 bits (unicorn), com os serviços DOS emulados. É a verdade de referência.
  `sh tools/asm_oracle/build.sh ../terep2isolado saida/` e depois `python3 saida/run.py`.
- **previous_effort** (`tools/oracle`): a tradução C++ mecânica antiga, com um host Linux. É ~1000x
  mais rápida que o emulador e permite trocar subsistemas pelo código novo
  (`--new physics,render,init`), o que serviu para reescrever uma parte de cada vez.
  `bash tools/build_oracle.sh build/`.

Todos imprimem, por quadro, o CRC32 do framebuffer e do segmento de dados (com
`tests/dsmask.h` mascarando só lixo de registradores que o original deixa em variáveis de
rascunho). Resultados:

- cenário de 600 quadros que passa por todas as teclas (`tools/cenario.py`): novo =
  previous_effort = asm;
- 12 roteiros aleatórios de 3000 quadros (`tools/fuzz_script.py`): novo = previous_effort em
  todos, com g++ e com clang++;
- roteiros aleatórios 4 e 7 (3000 quadros cada) contra o asm: o 7 é idêntico. O 4 diverge a
  partir do quadro 1591, onde o original cai no bug do título, que aqui foi corrigido
  (explicado abaixo). Antes da correção ele batia com o asm em todo o segmento de dados.

`make test` repete o cenário e o roteiro aleatório 1 e compara com as referências gravadas.

## Coisas que descobri no caminho

Bugs e esquisitices do original que foram **mantidos** (estão comentados no código):

- **Pulo**: a busca do vértice mais baixo usa AX, que é a metade baixa do impulso, então a
  parte baixa do impulso acaba sendo a altura do último vértice (`physics.cpp`, `Car::jump`).
- Recorte de polígonos: as bordas esquerda/superior pulam a interseção quando o vértice
  anterior está sobre a borda; direita/inferior não. A interpolação de textura no recorte
  usa metade da precisão.
- A projeção dos pontos cortados pelo plano próximo usa o centro fixo (160,100), mesmo com a
  tela dividida.
- `find_ref_vertex` pega o número de iterações da parte fracionária do x do primeiro vértice.
- O cabeçalho PCX quase não é validado; um sexto carro leria o nome depois do fim da tabela.

Bug do original **corrigido**:

- **Título que se corrompe**: segurar 1/2 enquanto troca de carro (Tab/Q) empurra o
  `lowest_vertex` de um carro que nunca pulou, que vale 0 (um offset no segmento). O impulso é
  somado no offset 0x14 do segmento, que é o "H" de "VEHICLE" no título, e a letra vira lixo.
  Como as letras ≥ 128 leem a tabela da fonte fora do lugar, às vezes até depois do fim do
  `data.bin`, o desenho do título corrompido nem era igual entre builds. Agora esse carro
  procura o ponto mais baixo como num aperto novo, e o pulo funciona.

Diferença entre o port Win16 e o `previous_effort`: o comprimento da mola (`sqrt`) é
arredondado (CVTSS2SI) no binário, e o previous_effort truncava. Por isso ele divergia do original
depois de algumas centenas de quadros; o oráculo em `tools/oracle` já vem corrigido.

## O que não foi feito

- Não há build Win16/DOS; o alvo é C++ portável. O som é só a sequência de escritas no OPL2
  (`opl_write_hook`); o host SDL ainda não toca nada.
