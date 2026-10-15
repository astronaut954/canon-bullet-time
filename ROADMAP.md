# Roadmap — Stillpoint (antes Canon Bullet Time)

Plano de desenvolvimento para levar o app de "Windows + terminal" para
"Windows + Mac", e depois para interface gráfica.

## Decisões fixas

| Tema | Decisão |
| --- | --- |
| Sistemas | Windows **x64** e macOS **arm64** (Apple Silicon). Sem Windows 32-bit, sem Mac Intel. |
| SDK Canon | **EDSDK 13.20.10** nos dois sistemas (a de Mac é universal, com `arm64` nativo). |
| Receita de build | **CMake**. O `.sln`/`.vcxproj` são apagados quando o CMake estiver validado. |
| Branches | `main` sempre funcionando. Um branch curto por etapa (`etapa-N-nome`). Nada de branch fixo por sistema. |
| Interface gráfica | Só depois do Mac funcionando (etapa 5). |
| Outras marcas (Sony, Nikon) | Depois. O código é organizado para aceitar outras marcas, mas nada é implementado sem câmera para testar. |

## Etapas

Status: `feito` · `em andamento` · `próximo` · `depois`

| # | Etapa | Resultado esperado | Status |
| --- | --- | --- | --- |
| 0 | Arrumação | Este roadmap, `AGENTS.md`, checklist de teste, README corrigido | feito |
| 1 | CMake + SDK 13.20.10 no Windows | Mesmo `.exe` de hoje, compilado pelo CMake, usando o SDK novo. Checklist passa com câmeras. | feito |
| 2 | Isolar código de Windows | Áudio, caminhos, console e funções da Microsoft atrás de interfaces de plataforma. Windows funciona igual. | feito |
| 3 | Versão Mac | SDK de Mac integrado, áudio e caminhos de Mac, checklist passa no Mac. Primeiro `.zip` para Mac (sem assinatura Apple por enquanto). | feito |
| 4 | Separar terminal da lógica | `main.cpp` vira só o menu; a lógica de câmera fica chamável por outra interface, com espaço para outras marcas. | feito |
| 4.1 | Diagnóstico para o usuário | Aviso de modelo fora da lista de compatibilidade (com nomes regionais), guia quando nenhuma câmera é encontrada, explicação dos erros mais comuns de conexão e disparo. | feito |
| 5 | Interface gráfica | Janela nativa com a interface em HTML/CSS (WebView2 no Windows, WebKit no Mac), usando o mesmo núcleo do terminal. Visual em `assets/ui/themes/nocturne-neo.css`. App de Mac (`.app`) e versão de janela nos pacotes. Checklist passou no Mac e no Windows (2026-10-14). | feito |
| 6 | Ícone e nome | Ícone do app no Windows e no Mac (câmera com o projétil no vidro da lente, cores da interface). Nome novo: Stillpoint — Bullet Time Photography (o app não ficará restrito à Canon). Funcionando no Mac e no Windows (2026-10-14). | feito |
| 7 | Instaladores | `.dmg` no Mac (arrastar para Aplicativos, fundo com chuva de código, Read Me em inglês sem Terminal) e instalador de Windows (Inno Setup, mesmo visual, versão de terminal opcional e desmarcada). Os zips continuam na versão. Testado no Mac e no Windows (2026-10-14). | feito |
| 8 | Atualização pelo app | Ao abrir, o app consulta a última versão no GitHub; botão "Update" no topo, janela com as novidades, download com andamento, conferência (tamanho e SHA-256) e instalação com reabertura. Não atualiza durante disparo ou conexão. Testado no Mac e no Windows (2026-10-14). | feito |
| 9 | Abertura | Tela de entrada com chuva de código verde aparecendo aos poucos e o logo com fade in; toda vez que o app abre; clique ou tecla pula. Testado no Mac e no Windows (2026-10-14). | feito |
| 10 | Exportação e tela cheia | "Save sessions" como **Photos** (original, ou convertidas para JPG, PNG, WEBP, TIFF, HEIC) ou **Sequence** (MP4, MOV, GIF, WEBP, AVIF; velocidade, loops, reverse loop, tamanho), configuração por sessão e "Apply to all"; abre a pasta no fim. Na tela cheia: Reverse loop e velocidade. Qualidade Standard / High / Maximum. Testado no Mac e no Windows (2026-10-15). Versão 2.5.0. | feito |
| 11 | Menu do app e Settings | Mac: menu na barra de cima (Stillpoint, Edit, View, Window, Help), com ⌘C/⌘V/⌘A nos campos de texto e ⌘Q. Windows: botão de menu no canto do app. About, Check for updates, Settings (tema e idioma), links de ajuda. Também: cancelar o bullet time e campos só com números inteiros. Testado no Mac e no Windows (2026-10-15). Versão 2.6.0. | feito |
| 12 | Configurações das câmeras pelo app | Canon: ver e igualar ISO, velocidade, abertura, compensação, balanço de branco (K), Picture Style e qualidade de todas as câmeras pelo app; o app pergunta a cada câmera o que ela aceita e mostra quais não dá para ajustar (e por quê). Preparado para outras marcas. | em andamento |
| 13 | Match colors | Ajuste automático simples de cor e brilho por câmera, a partir de uma câmera de referência, sem mexer nas fotos originais. Funciona com qualquer marca. | depois |

### Etapa 5: mudanças pedidas para a interface (registradas em 2026-10-10)

Feitas em 2026-10-11 (aguardando teste no Mac), junto com a limpeza dos textos
(sem emojis e sem cara de terminal; o terminal continua igual):

1. **Nome da câmera:** o modelo (ex.: "Canon EOS Rebel T7") em destaque e o
   apelido (`T7_1`) em segundo plano — o inverso de hoje.
2. **Reordenar por foto:** botão junto aos controles de ordem. Tira uma foto de
   cada câmera e mostra um carrossel de miniaturas no alto, cada uma com o nome
   da câmera e o número na ordem; arrastar as miniaturas muda a ordem. A foto
   selecionada fica destacada e aparece maior logo abaixo do carrossel.
3. **Sessões na tela principal:** sem aba separada. Acima de "Cameras" e
   "Bullet time", a navegação por pastas fica à esquerda (cada pasta
   identificada pela sessão) e as fotos aparecem no mesmo modelo do "Reordenar
   por foto" (carrossel no alto, foto selecionada maior abaixo).

Design system [Neo Terminal UI](https://github.com/Dyltom/neo-terminal-ui) aplicado em
2026-10-11 (aguardando teste no Mac): a biblioteca é React + Tailwind, então os tokens
(cores, fonte Share Tech Mono, brilho, cantos retos) e os estilos dos componentes foram
passados para CSS simples (em `assets/ui/themes/neo-terminal.css`, que saiu em 2026-10-13), sem mudar o funcionamento da página.
O "flicker" e a vinheta do efeito CRT ficaram de fora para não atrapalhar a conferência
das fotos.

Em 2026-10-12 entraram, para comparação, [Nocturne](https://github.com/starside-io/nocturne)
e [CYBERCORE CSS](https://github.com/sebyx07/cybercore-css). Em 2026-10-13 o dono do
projeto escolheu a estrutura do Nocturne com as cores do Neo Terminal. Os três temas
originais saíram do app e ficaram duas variações dessa mistura em `assets/ui/themes/`:
"Mix 1" (fontes do Nocturne) e "Mix 2" (fonte e efeitos do Neo Terminal). No mesmo
dia ele escolheu o Mix 1, sem os cantos cortados do Nocturne (cantos retos). Ficou um
único visual, `assets/ui/themes/nocturne-neo.css`, e o seletor de temas saiu do app.

Aplicativo de Mac (2026-10-14): `scripts/build_release.py` monta o `.app` (na época `Canon Bullet Time.app`, hoje `Stillpoint.app`)
(programa de janela em Contents/MacOS, SDK em Contents/Frameworks, assets em
Contents/Resources, Info.plist, assinatura local). O programa acha os assets por
Platform::ResourcesDirectory e o SDK por @executable_path/../Frameworks. Testado no
Mac em 2026-10-14 (abre com dois cliques; a versão de terminal fica em "Terminal
version/"). Próxima versão: v2.0.0.

### Etapa 6: ícone e nome (iniciada em 2026-10-14)

- **Ícone:** escolhido o conceito "E" (câmera vista de perto, lente ocupando o ícone,
  projétil reto dentro do vidro com as ondas de ar e a chuva de código). Gerado por
  `scripts/make_icon.py` em `resources/icon/`; tamanhos pequenos (até 48 px no Windows,
  32 px no Mac) usam uma versão simplificada. Windows: embutido nos dois `.exe`
  (`resources/windows/app.rc.in`, ID 32512, que a janela também usa). Mac: `AppIcon.icns`
  no `.app` (`scripts/build_release.py`). Funcionando nos dois sistemas (2026-10-14).
- **Nome:** **Stillpoint**, com o complemento "Bullet Time Photography" em lugares
  estratégicos (repositório `stillpoint-bullet-time-photography`, descrição do GitHub,
  título do README) para facilitar a busca. Decidido em 2026-10-14.
  Programas: `Stillpoint` (janela) e `Stillpoint Terminal App` (terminal); no Mac,
  `Stillpoint.app` (identificador `io.github.astronaut954.stillpoint`); pacotes
  `stillpoint-vX.Y.Z-<sistema>.zip`. A pasta de dados passa a ser `Stillpoint`; a antiga
  (`Canon Bullet Time`) é renomeada na primeira abertura. Nomes internos do código
  também mudaram (`cbt_core` → `stillpoint_core`, variáveis `CBT_*` → `STILLPOINT_*`,
  ex.: `STILLPOINT_TEST_RESTART`). Funcionando nos dois sistemas (2026-10-14).

### Etapa 7: instaladores (iniciada em 2026-10-14)

Visual aprovado em 2026-10-14 (fundo do `.dmg`, imagens do instalador de Windows e Read Me).
- **Mac:** `scripts/build_release.py` cria `stillpoint-vX.Y.Z-macos-arm64.dmg` com o
  dmgbuild (instalado sozinho em `build/release-tools` na primeira vez): Stillpoint.app,
  atalho para Aplicativos e `Read Me.pdf`, fundo em `resources/installer/`. Primeira
  abertura sem Terminal: Ajustes do Sistema → Privacidade e Segurança → "Abrir Mesmo Assim".
- **Windows:** `resources/installer/stillpoint.iss` (Inno Setup 6.5.2 ou mais novo) gera
  `stillpoint-vX.Y.Z-windows-x64-setup.exe`: instala em Arquivos de Programas, atalho no menu
  Iniciar, atalho na área de trabalho e versão de terminal opcionais (desmarcados), desinstala
  por Configurações → Aplicativos sem apagar as fotos guardadas.
- `scripts/publish_release.py` envia o zip e o instalador de cada sistema.
- 1º teste no Mac (macOS 26): o `.dmg` abriu com os ícones nas posições certas, mas sem o
  fundo. Causa conhecida: o Finder do macOS 26.2+ ignora o fundo quando o `.DS_Store` tem o
  registro "pBBk", que o dmgbuild grava até a 1.6.6 (a 1.6.7 corrige, mas exige Python 3.10+;
  o do Mac é 3.9). Corrigido no script: o registro é desligado e o `.dmg` pronto é conferido
  (falha se o fundo não estiver certo). Nomes sem extensão (".app", ".pdf") na janela.
- 2º teste no Mac: fundo aparecendo. A janela ficou baixa: a altura do `.dmg` inclui a
  barra de título (~32 pt) e a barra de caminho do Finder (~28 pt), que cortavam o nome do
  Read Me. Janela e fundo passaram de 500 para 560 pt de altura.
- 3º teste no Mac (placas centralizadas) e teste no Windows (instalador): aprovados em 2026-10-14.

### Etapa 8: atualização pelo app (iniciada em 2026-10-14)

Telas aprovadas em 2026-10-14 (aviso no topo, janela com novidades, andamento, espera
durante disparo/conexão, aviso "Updated to ..." ao reabrir).
- **Versão:** arquivo `VERSION` (lido pelo CMake e gravado no app). `build_release.py`
  confere que a tag é igual a ele e que o app em `build/` foi compilado com ele.
- **Consulta:** curl do próprio sistema (`api.github.com/.../releases/latest`); a página
  compara as versões e escolhe o arquivo (`-setup.exe` ou `.dmg`).
- **Download:** só de `github.com/astronaut954/stillpoint-bullet-time-photography/releases/download/`,
  para a pasta temporária; confere tamanho e SHA-256 (o GitHub informa o "digest" de cada arquivo).
- **Instalação:** Windows: instalador em modo silencioso (pede permissão de administrador);
  um PowerShell escondido espera o instalador terminar e abre o app de novo (não depende
  do instalador baixado). Mac: script espera o app fechar, troca o `.app`
  pelo do `.dmg` (com volta atrás se falhar) e abre de novo. Só quando dá: Windows instalado
  pelo instalador; Mac em `.app` numa pasta com escrita. Senão, abre a página de download.
- **Novidades:** `docs/release-notes/vX.Y.Z.md` (obrigatório para publicar), mostradas na janela.
- **Teste sem versão nova:** `STILLPOINT_TEST_VERSION=2.1.0` faz o app se apresentar como
  antigo e oferecer a versão publicada. `STILLPOINT_NO_UPDATE_CHECK=1` desliga a consulta.
- 1º teste no Windows (2026-10-14): instalou, mas não reabriu. Causa: a reabertura estava
  no instalador novo, e o teste instala a v2.2.0 (instalador antigo). Corrigido como acima.
- Verificado aqui: fluxo da página (aviso, janela, andamento, cancelar, falha, instalar,
  modo sem atualização automática, espera com câmera conectando) num navegador; código de
  Windows compila (MinGW); script do Mac testado com ferramentas simuladas.
- Atualização simulada (STILLPOINT_TEST_VERSION) aprovada no Windows e no Mac pelo dono do
  projeto em 2026-10-14. Junto: sombra da contagem em verde escuro e correção da linha de
  status abaixo de "It's bullet time".
- Primeira atualização real (2.3.0 → 2.4.0, publicada em 2026-10-14) funcionou no Mac e no
  Windows, segundo o dono do projeto.

### Etapa 9: abertura (iniciada em 2026-10-14)

Prévia aprovada em 2026-10-14; aparece toda vez que o app abre. Chuva desenhada num canvas
(`app.js`, início), logo e título por cima (`index.html` e `nocturne-neo.css`). O ícone da
página fica em `assets/ui/images/` e é embutido ao abrir (`__IMAGE:arquivo__`, como as fontes).
As câmeras começam a conectar enquanto a abertura roda. Versão 2.4.0.
Testada e aprovada no Mac e no Windows em 2026-10-14.

### Critério para fechar uma etapa

1. Compila nos sistemas que a etapa toca.
2. `docs/teste-manual.md` passa com câmeras reais no(s) sistema(s) afetado(s).
3. Branch volta para `main`; se fizer sentido, sai uma versão (`vX.Y.Z`).

### Etapa 10: exportação (iniciada em 2026-10-14, fechada em 2026-10-15)

Telas aprovadas em 2026-10-14. Decisões do dono do projeto: na sequência, as fotos originais
vão junto por padrão; GIF, WEBP e AVIF repetem sem parar (os loops ficam dentro do MP4/MOV);
sai junto com o Reverse loop, a velocidade e o "abrir a pasta" (v2.5.0).
- Código comum em `src/gui/exporter.cpp` (GIF próprio com paleta única e pontilhado; WEBP com
  libwebp; AVIF com libavif + libaom; MP4/MOV com minimp4). Bibliotecas em `cmake/export.cmake`.
- Por sistema (`include/platform/media.h`): H.264 pelo VideoToolbox (Mac) e Media Foundation
  (Windows); HEIC pelo ImageIO (Mac) e WIC (Windows, só com as extensões HEIF e HEVC).
- Tamanhos: 720p, 1080p, 4K (cabem em 1280×720, 1920×1080, 3840×2160); GIF/WEBP/AVIF até 1080p.
- Nomes (decididos em 2026-10-14): pasta e arquivos com Date + time ("2026-10-14 16.31.00",
  padrão), Number (01, 02... continuando os da pasta) ou Custom (data opcional, depois do nome por
  padrão; nome repetido sem data ganha numeração). Fotos salvas sem o número do cartão
  ("01_SL3_1.JPG"). Subpastas Photos/ e Sequence/ só com fotos e sequência juntas. A janela de salvar só fecha
  depois de salvar. "Same settings for all sessions" ligado por padrão;
  desligado, as sessões diferentes da aberta ganham "Different". Aviso de pasta já existente e
  ao fechar o app com a janela de salvar aberta ou durante o salvamento. Replace apaga o que
  havia na pasta antiga e deixa só o novo salvamento.
- Qualidade (decidida em 2026-10-14): Standard / High (padrão) / Maximum, por sessão, nas fotos
  convertidas com perda (JPG 85/95/100, WEBP 80/92/sem perda, HEIC 0.75/0.9/1.0) e na sequência
  (MP4/MOV por taxa de bits, `Export::VideoBitrate`; WEBP 75/90/sem perda; AVIF 60/75/90, este
  com cor 4:4:4). PNG e TIFF são sempre sem perda; GIF sempre 256 cores (sem mudança, decisão do
  dono). RAW convertido continua usando a imagem grande guardada dentro do RAW (sem revelação, para
  não pesar); a qualidade total do RAW fica no "Original".
- Teste no Linux sem câmeras: `tests/export` (gera todos os formatos da sessão de teste).
- Verificado aqui: todos os formatos comuns gerados e conferidos (ffprobe/Pillow); telas testadas
  num navegador; código de Windows compila (MinGW). Testado pelo dono do projeto no Mac e no
  Windows (2026-10-15), com a sessão de teste.
- Pendente (decidido em 2026-10-14): conferir a qualidade com uma sequência real em RAW de várias
  câmeras (Standard / High / Maximum), quando houver uma sessão assim. Não bloqueia a v2.5.0.

### Etapa 11: menu do app e Settings (iniciada e fechada em 2026-10-15)

Decisões do dono do projeto (2026-10-15): itens do menu do Mac aprovados, com Settings; no
Windows, botão de menu só no Windows (no Mac fica a barra de cima); a versão no título saiu antes,
sozinha, na v2.5.1. Motivo extra: sem menu, o Mac não aceitava ⌘C, ⌘V e ⌘Q (confirmado pelo dono).
- Mac: `NativeWindow::InstallAppMenu` (`src/gui/native_window_mac.cpp`). Quit passa pela mesma
  pergunta do botão de fechar. Itens do app viram o evento "menu" para a página.
- Windows: botão no canto da barra (`#menuBtn` em `assets/ui/index.html`), mesmos itens; Ctrl+,
  abre o Settings.
- Settings: tema (cada arquivo de `assets/ui/themes/`; hoje só o Nocturne Neo) e idioma (hoje só
  English). Gravados em `<dados do app>/settings.txt`; valem ao abrir o app de novo ("Restart
  now"). A tradução para português é uma etapa à parte.
- Corrigido (2026-10-15): a lista de temas mostrava temas de teste antigos que ficaram na pasta
  de build do Mac (Mix, Nocturne original...); escolher um deles deixava a tela toda errada. Agora
  só aparece o arquivo que começa com `/* stillpoint-theme: ... */`, um tema inválido volta ao
  padrão sozinho, e cada compilação recopia `assets/ui` do zero. Os pacotes publicados não tinham
  esses arquivos (o `build_release.py` copia do projeto).
- Junto (pedido em 2026-10-15): Countdown, Sessions e Download wait só aceitam números inteiros
  (o Mac deixava digitar letras), até 999.
- Junto (aprovado em 2026-10-15): cancelar o bullet time. Na contagem (botão Cancel e Esc; nada
  dispara e a pasta vazia sai), entre sessões e esperando as fotos (o botão de disparo vira
  "Cancel bullet time"; as fotos que chegaram ficam). Núcleo: `BulletTime::RequestCancel()` e o
  aviso `OnSequenceCancelled`; a contagem passou a esperar em passos de 0,1 s. O terminal não
  chama o cancelamento, então a saída dele não muda.
- Verificado aqui: código de Mac e de Windows compila (Mac com cabeçalhos simulados, Windows com
  MinGW); telas testadas num navegador. Testado pelo dono do projeto no Mac e no Windows
  (2026-10-15), com câmeras, incluindo o cancelamento.

### Correção 2.6.1: câmeras que não reconectavam (2026-10-15)

Dado do dono do projeto (Mac): câmeras desligadas e religadas não voltavam, nem com "Restart
camera connections"; só reiniciando o app. No código: uma câmera que cai (ou falha 3 vezes) fica
"posta de lado" até o SDK avisar que uma câmera foi conectada; se o aviso não chega, ela fica de
lado para sempre, e o botão não limpava essa lista. Correção: o botão limpa a lista, e uma câmera
Canon que aparece de novo no USB conta como esse aviso. Só o app de janela usa essa parte. Testado no Mac e no
Windows pelo dono do projeto (2026-10-15).

### Etapa 12: configurações das câmeras pelo app (iniciada em 2026-10-15)

Decisões do dono do projeto (2026-10-15): começar pela Canon, preparado para outras marcas;
o app precisa indicar quais câmeras não dá para ajustar. Dado verificado na documentação do
EDSDK 13.20.10: ISO, Av, Tv, compensação de exposição, balanço de branco, temperatura de cor
(2800–10000 K), ajuste fino do balanço, Picture Style e qualidade são de leitura e escrita, sem
restrição por modelo; cada câmera informa os valores aceitos (EdsGetPropertyDesc). O modo (M,
Av...) não muda pelo SDK em câmeras com disco de modos; nos modos automáticos as configurações
de disparo não podem ser mudadas. EOS M, M2, M3, M5, M6, M10 e M100 não aceitam disparo remoto.
Modo de foco: só leitura (MF vem da chave da lente; mudar o modo AF só foi testado pela Canon na
EOS R5). Compensação de exposição não vale no modo M (fica sem opções).

Tela aprovada em 2026-10-15: botão "Camera settings" no painel Cameras; tabela com uma linha por
câmera e a linha "All cameras" (balanço de branco nela: os predefinidos que todas aceitam e, se
todas aceitarem, os valores em Kelvin; a SL3 e a T7 não oferecem Kelvin); colunas ISO, Shutter, Aperture,
Exp. comp., White balance, WB fine-tune, Picture Style, Quality e Focus (só leitura, âmbar
com AF ligado); Drive saiu da tabela (decisão de 2026-10-15): o app põe toda câmera em disparo único
sozinho (único silencioso também vale) e, se a câmera não aceitar, avisa ("Not on single shot"); mudanças vão para a câmera na hora; legenda num ícone (?); "Copy from camera";
avisos no painel ("Settings differ", "Settings on the camera only"). No painel Cameras: cabeçalho
fixo, só a lista rola, e arrastar para reordenar.
- Teste rápido (pedido em 2026-10-15): botão "Test shot" no Camera settings dispara todas as
  câmeras uma vez e mostra as fotos lado a lado (← → para comparar). São fotos temporárias, as
  mesmas do "Reorder by photo": apagadas ao fechar a janela ou no próximo teste.
- Ajuste automático (decidido em 2026-10-15: igualar à média de todas, cor e brilho; refeito no
  mesmo dia: é o próprio Test shot que ajusta, e mexe em tudo que faz as câmeras ficarem
  parecidas). Caixa "Match automatically" ao lado do Test shot (ligada por padrão; não há mais
  botão "Auto match"). Com ela, o Test shot (`src/gui/auto_match.cpp`):
  1. sem foto, põe a mesma velocidade, abertura, ISO e compensação em todas (o valor mais perto
     da média que todas aceitam) e o mesmo Picture Style (o mais comum; nunca Auto, que muda a
     cada cena; só Auto vira Standard);
  2. foto 1, como estão: a média da cor e do brilho de todas é o alvo;
  3. se o balanço de branco está em Auto ou diferente entre elas: todas em Daylight sem ajuste
     fino, foto 2, e daí um balanço comum que deixa a cor perto do alvo (Kelvin se todas
     aceitarem; senão o predefinido mais perto: Daylight 5200 K, Shade 7000, Cloudy 6000,
     Tungsten 3200, Fluorescent 4000; troca de predefinido uma vez, se a foto seguinte mostrar
     que outro fica mais perto);
  4. cor pelo ajuste fino do balanço (Canon: 1 passo azul/âmbar ≈ 5 mireds; no SDK, AB −
     azul/+ âmbar; GM: a documentação diz − verde/+ magenta, mas na T7 +5 aparece como G5 no
     menu da câmera — conferido em 2026-10-15, então o app usa − magenta/+ verde). O ajuste
     automático não depende disso: confere pela foto o sentido de cada câmera e, se a cor andou
     para o lado contrário, inverte (enquanto não confere, anda no máximo 3 passos; uma foto a
     mais quando inverte). Simulação com 30% das câmeras invertidas: resultado igual ao normal. e brilho pelo ISO (em M) ou pela compensação (outros
     modos); foto de novo e corrige pelo quanto a câmera respondeu. No máximo 5 fotos.
  Mostra as últimas fotos e um resumo do que mudou em cada câmera. Medida conferida aqui com
  cópias tingidas de uma foto (âmbar +0,100 esperado, +0,107 medido; +1/3 de stop esperado, +0,35
  medido). Simulação aqui com câmeras falsas (300 cenários sorteados: sensores, luz de 2900 a
  7000 K, balanço em Auto ou Daylight): diferença típica entre câmeras caiu de ~6 para ~0,8
  passo em azul/âmbar e de ~1 para ~0,2 stop no brilho. Isso é a conta, não câmera de verdade.
  Limite: ISO anda de 1/3 de stop, então sobra até ~1/6 de stop de diferença.
  **Pendente (2026-10-15):** no primeiro teste com câmeras de verdade (Mac, SL3 + T7) o resultado
  "deixou muito a desejar" (dono do projeto). Corrigir depois, com fotos do teste para analisar.
- Layout do Test shot (decidido em 2026-10-15, opção A entre três comparadas): sem a prévia
  grande; só a fileira de miniaturas, maiores e com a foto inteira. Clicar numa miniatura abre a
  foto grande por cima da janela (← → passam de câmera, Esc ou Close fecham). "Hide photos"
  recolhe a fileira; um novo Test shot abre de novo.
- Núcleo: `include/camera_settings.h` (dados iguais para qualquer marca) e
  `src/core/camera_settings_canon.cpp` (Canon). Com a janela aberta, a página lê as câmeras de
  novo a cada 3 s (mudanças feitas na própria câmera aparecem sozinhas). O aviso do SDK de
  "configuração mudada" foi tirado: no Mac, o app fechou ao desligar as câmeras com ele ligado
  (falha dentro do EdsGetEvent; a causa é suspeita, não confirmada). O app confere o disparo
  único antes de cada bullet time e de cada Test shot.
- Verificado aqui: código compila (cabeçalhos simulados do SDK); telas testadas num navegador com
  dados simulados (5 e 16 câmeras). Falta: testar com câmeras de verdade no Mac e no Windows.

## Futuro (não planejado ainda)

- **Outras marcas (Sony, Nikon).** Viável marca a marca, com câmera para testar.
  Sony (site oficial, conferido em 2026-10-15): SDK gratuito, Windows 11 x64 e macOS 14.1+,
  cerca de 35 modelos recentes (α1, α9 II/III, α7 IV/V, α7R IV/V, α7S III, α7C/C II/CR,
  α6700, FX3/FX30/FX2, ZV-E1, ZV-E10 II...); modelos antigos comuns (a6000 a a6600, α7 III)
  ficam de fora; exige o firmware mais recente. A lista exata de configurações por modelo
  está no manual do SDK (não lido).
  Nikon (site oficial, conferido em 2026-10-15): um SDK por modelo (D300 a D850, Df, Z5,
  Z6/6 II, Z7/7 II, Z8, Z9; Z6 III e Z50 II não aparecem). **O SDK de controle remoto só
  controla uma câmera por vez** — bloqueia o bullet time com várias Nikon. No Mac com Apple
  Silicon, os módulos rodam pelo Rosetta 2 (o Stillpoint é só Apple Silicon: precisaria de
  um programa auxiliar).
- **Pesquisar a libgphoto2** (biblioteca aberta, várias marcas via PTP/USB) como
  alternativa a um SDK por marca. Pontos a verificar: suporte no Windows e
  precisão do disparo sincronizado.
- **Vários SDKs da Canon ao mesmo tempo:** descartado. Os SDKs antigos não rodam em
  64 bits / Apple Silicon e versões diferentes não convivem no mesmo programa.

## O que funciona em cada sistema

| Função | Windows | Mac |
| --- | --- | --- |
| Detectar / listar câmeras | sim | sim |
| Disparo sincronizado (BULLET TIME) | sim | sim |
| Download e organização das fotos | sim | sim |
| Reordenar câmeras / prefixos | sim | sim |
| Pasta de saída configurável | sim | sim |
| Áudio de contagem | sim | sim |
| Pacote de distribuição (`.zip`) | sim | sim (sem assinatura Apple) |

Atualizar esta tabela sempre que uma função mudar de status.

## Pendências conhecidas

- **EDSDK fora do repositório:** decidido manter o SDK da Canon só dentro do
  `.zip` das versões (o que os termos da Canon permitem). Os arquivos do SDK
  saíram do repositório; cópias antigas continuam no histórico do Git.

- **Windows: conexão em paralelo não funcionava (testado em 2026-10-14).** A sessão
  aberta numa thread auxiliar abria, mas os comandos seguintes davam erro 2 e a câmera
  "parava de responder". Corrigido: no Windows a sessão abre na própria thread das
  câmeras (Platform::SdkSessionsInWorkerThreads). Testado em 2026-10-14 com SL3 e
  T7: conexão, disparo, sessões, USB, fechar e "Restart app" funcionando; versão de
  terminal também testada e funcionando. "Restart app" também testado no Mac, dentro
  do `.app` (STILLPOINT_TEST_RESTART=1 mostra o botão sem câmera presa). Texto antigo:
  **Windows: conexão em paralelo não testada com câmeras.** Desde a branch
  `fix-feedback-conexao`, a sessão de cada câmera é aberta numa thread auxiliar
  (com COM inicializado, como pede o manual da Canon). Compila, mas ainda não
  foi testado com câmeras no Windows. Testar antes da próxima versão publicada.

- **T7 às vezes conecta sem aparecer "Connecting" (Mac, registrado em 2026-10-13).**
  A T7 leva cerca de 42 s para conectar. Na maioria das vezes a janela mostra
  "Connecting" durante esse tempo, mas às vezes não mostra nada e, depois dos
  42 s, ela aparece conectada. Ainda não investigado. Hipóteses (não confirmadas):
  1. a demora acontece antes de o app saber que existe uma câmera: a listagem do
     SDK (`EdsGetCameraList` / `EdsGetDeviceInfo`) trava enquanto a T7 liga, e só
     a abertura da sessão (`EdsOpenSession`) é mostrada como "Connecting";
  2. a conexão vem por um caminho que não usa a lista de conexões pendentes (a
     primeira conexão ao abrir o app ou "Restart camera connections"), e a tela
     não recebe atualização enquanto ela dura;
  3. a T7 está na fila atrás de outra câmera e aparece como "Waiting for another
     camera" em vez de "Connecting".
  Dado de 2026-10-13 (T7 e SL3 ligadas antes de abrir o app): a SL3 conectou em
  1 s; nada no log por 40 s; depois a T7 apareceu e conectou em 1 s. Ou seja, a
  espera acontece antes da abertura da sessão (hipóteses 1 ou 2 abaixo, não a 3).
  Para separar: o app agora registra nos detalhes técnicos a lista de câmeras que
  o SDK devolve (quando muda), procuras e leituras de câmera que demoram 1 s ou
  mais, e os avisos do sistema de "câmera conectada". Guardar o log de uma vez em
  que acontecer.
  **Resultado (log de 2026-10-13, 21:22):** a lista do SDK teve só a SL3 de 21:22:12
  a 21:22:53; nenhuma procura nem leitura demorou. Às 21:22:53 o macOS avisou "câmera
  conectada", a T7 entrou na lista e conectou em 1 s. Confirmada a hipótese 1: nesses
  ~40 s a T7 ainda não existe para o SDK (nem para o macOS), então o app não tem como
  mostrá-la pelo SDK. Possível solução, ainda não decidida: olhar a lista de aparelhos
  USB do sistema (IOKit no Mac, SetupAPI no Windows) e mostrar "uma câmera Canon está
  ligando" quando há um aparelho Canon no USB que o SDK ainda não lista. Teste
  antes de implementar: o app agora registra "USB Canon devices" (só no Mac; IOKit)
  nos detalhes técnicos, para comparar com "SDK camera list".

- **T7 não reconecta depois de desconectar (Mac, registrado em 2026-10-13).** Depois
  que a T7 se desconecta, às vezes ela só volta reiniciando o app (ou o Mac).
  Dado do log de 21:44: religada, a T7 voltou com a mesma "porta" da SL3 conectada
  (`SL3@2:connected T7@2:connected`) e o app a tratou como já conectada. Corrigido
  em 2026-10-13: cada entrada é identificada por modelo + porta. Também: uma
  tentativa que falha rápido (a T7 deu 8195, "sessão não aberta") volta às tentativas
  normais em vez de esperar outro aviso de conexão. Testado no Mac (2026-10-14).
  Às vezes só voltava reiniciando o Mac (sem explicação); segundo o dono do projeto, não
  aconteceu mais até 2026-10-14 (depois das correções de reconexão e da detecção por USB).
  Se voltar: pedir o log de Activity com "Show technical details" ligado.
  Teste do USB (1º log): "USB Canon devices: 0" o tempo todo, mesmo com as duas
  câmeras conectadas; a busca foi refeita (todos os aparelhos USB, filtrando a Canon,
  e com o total de aparelhos no log). 2º log (22:00): agora as câmeras aparecem no
  USB ("Canon Digital Camera"), sempre no mesmo segundo que no SDK quando o SDK está
  livre; o endereço USB é fixo por entrada física (T7 0x120000, SL3 0x110000 nas
  duas vezes). Às 22:01 a SL3 foi desligada enquanto a T7 conectava (43 s) e o app
  só soube no fim (erro 97 ao configurar). A leitura do USB passou a rodar também
  enquanto uma câmera conecta, para medir quanto antes o USB mostra a mudança.
  3º log (22:08): às 22:08:29 o USB já mostrava as duas câmeras e o SDK só a SL3; a
  T7 entrou no SDK às 22:09:11 (42 s depois). Implementado em 2026-10-13: câmeras
  que o USB mostra e o SDK ainda não aparecem como "Connecting" (no lugar de uma
  câmera que caiu, ou como "Canon camera" nova) e não bloqueiam o disparo das outras.
  Mac: IOKit. Windows (2026-10-13): SetupAPI, mesmos critérios (Canon VID_04A9 com
  interface de classe 06); compila (verificado com MinGW); testado com câmeras no Windows (2026-10-14).
  Depois: mostra o tempo de espera e o modelo. O nome no USB é igual para todas
  ("Canon Digital Camera"), mas o código do produto USB muda por modelo; o app
  aprende qual código é qual modelo (quando sobra um de cada ao comparar USB e SDK)
  e guarda em `usb-models.txt`, na pasta de dados do app.

- **Conexão que nunca termina (registrado em 2026-10-13).** O SDK da Canon não deixa
  cancelar uma conexão em andamento e bloqueia tudo enquanto ela dura; se ela nunca
  terminar, o disparo nunca libera. Feito: "Restart camera connections" desativado
  durante uma conexão; depois de 60 s, aviso com o botão "Restart app" (abre o app de
  novo e fecha este; fotos não salvas ficam no disco). Testado no Mac (2026-10-13): desligar a T7
  no meio da conexão faz a conexão falhar e libera o disparo; o aviso diz isso. Se travamentos acontecerem de
  verdade com frequência: passar a comunicação com as câmeras para um programa
  auxiliar, que o app pode encerrar e abrir de novo sozinho, sem fechar a janela.

## Histórico de versões do SDK

| Onde | Versão em uso |
| --- | --- |
| Windows (máquina de build) | 13.20.10.6402 (desde a etapa 1) |
| GitHub | nenhum (SDK fica só na máquina de cada um) |
| Mac (máquina de build) | 13.20.10.0 (desde a etapa 3) |
