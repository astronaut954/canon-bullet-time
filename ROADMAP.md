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
| 9 | Abertura | Tela de entrada com chuva de código verde aparecendo aos poucos e o logo com fade in; toda vez que o app abre; clique ou tecla pula. | em andamento |

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

### Etapa 9: abertura (iniciada em 2026-10-14)

Prévia aprovada em 2026-10-14; aparece toda vez que o app abre. Chuva desenhada num canvas
(`app.js`, início), logo e título por cima (`index.html` e `nocturne-neo.css`). O ícone da
página fica em `assets/ui/images/` e é embutido ao abrir (`__IMAGE:arquivo__`, como as fontes).
As câmeras começam a conectar enquanto a abertura roda. Versão 2.4.0.

### Critério para fechar uma etapa

1. Compila nos sistemas que a etapa toca.
2. `docs/teste-manual.md` passa com câmeras reais no(s) sistema(s) afetado(s).
3. Branch volta para `main`; se fizer sentido, sai uma versão (`vX.Y.Z`).

## Futuro (não planejado ainda)

- **Outras marcas (Sony, Nikon).** Viável marca a marca, com câmera para testar.
  Sony: SDK oficial gratuito, Windows 11 x64 e macOS 14.1+, ~33 modelos recentes.
  Nikon: confirmar como o SDK atual é distribuído antes de decidir.
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
