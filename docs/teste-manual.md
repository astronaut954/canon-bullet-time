# Checklist de teste manual

Rodar com **pelo menos 2 câmeras** conectadas por USB, ligadas, com cartão.
Anotar o sistema (Windows/Mac), a versão/branch testada e os modelos de câmera.

## 1. Abertura
- [ ] O app abre sem erro de SDK ("Failed to initialize SDK" **não** aparece).
- [ ] Mostra a pasta de saída padrão (Downloads do usuário).
- [ ] As câmeras conectadas são detectadas automaticamente.

## 2. Câmeras (opções 1, 2 e 5)
- [ ] **1 – List Cameras** mostra todas as câmeras com nome e prefixo.
- [ ] **2 – Detect Cameras**: desconectar uma câmera, rodar de novo, ela some;
      reconectar, rodar de novo, ela volta.
- [ ] **5 – Reorder Cameras**: `test` dispara só a câmera atual.
- [ ] **5 – Reorder Cameras**: mudar o prefixo de uma câmera e confirmar a nova
      ordem na opção 1.

## 3. Pasta de saída (opção 4)
- [ ] Trocar para outra pasta; a nova pasta é usada no disparo seguinte.
- [ ] Enter sem digitar mantém a pasta atual.

## 4. BULLET TIME (opção 3)
- [ ] Timer de contagem com áudio (contagem + "it's bullet time").
- [ ] Todas as câmeras disparam juntas.
- [ ] Contador de download chega a `[N/N]` sem "Timeout reached".
- [ ] Na pasta de saída: uma pasta com data/hora, com **uma foto por câmera**,
      nomes começando pelo prefixo de ordem de cada câmera.
- [ ] Com **2 ou mais sessões** (`c` para configurar): uma pasta por sessão
      (`..._session_01`, `..._session_02`), todas completas.

## 5. Falha controlada
- [ ] Desconectar uma câmera e disparar: o app avisa a câmera perdida e não trava.

## 6. Saída
- [ ] **0 – Exit** fecha o app normalmente.

## 7. App com janela (Stillpoint)
- [ ] Abrir o app **com as câmeras já ligadas**: as câmeras conectam uma de cada
      vez; a que está esperando aparece como "Waiting for another camera", a que
      conecta mostra os segundos. O modelo aparece em destaque e o apelido (T7_1) embaixo.
- [ ] Enquanto uma câmera conecta, o disparo fica indisponível com o aviso
      "Available when … finishes connecting".
- [ ] Fechar o app enquanto uma câmera conecta: fecha na hora, sem travar.
- [ ] Abrir o app e **depois** ligar as câmeras: elas aparecem sozinhas.
- [ ] Sem nenhuma câmera: depois de alguns segundos aparece o guia "No cameras found".
- [ ] Desligar uma câmera: ela fica "Disconnected"; religar: volta conectada no
      mesmo lugar (mesmo nome e ordem), sem clicar em nada.
- [ ] Disparar depois de religar uma câmera: a foto dela também chega.
- [ ] **Restart camera connections** (no painel Activity, no rodapé): todas reconectam.
- [ ] **Reorder by photo**: tira uma foto de cada câmera (sem som e sem criar
      sessão); arrastar as miniaturas (ou "Move earlier/later") e **Save order**
      muda a ordem; o próximo disparo usa a nova ordem nos nomes dos arquivos.
- [ ] Sessões na tela principal: pastas à esquerda, a mais recente selecionada;
      carrossel no alto e a foto selecionada grande embaixo; setas ← → e espaço (play).
- [ ] **Reorder** numa sessão (na tela cheia): arrastar as miniaturas (ou usar as setas) e **Save
      order** renomeia as fotos da sessão na nova ordem (RAW e JPEG juntos).
- [ ] Fora da tela cheia, a sessão mostra só as fotos e, ao lado das miniaturas,
      "Delete session" e o botão de tela cheia (sem título nem quantidade de fotos).
- [ ] Sessão em **tela cheia** (botão sobre a foto ou tecla F; Esc sai): data da sessão,
      play/pausa, setas, **Reorder**, apagar uma foto (lixeira ou tecla Delete) e apagar a sessão.
- [ ] Rodapé (Activity): enquanto algo acontece (conectando, disparando, recebendo
      fotos, salvando), o texto aparece com reticências animadas.
- [ ] Desligar a T7 e depois desligar/religar a SL3: a SL3 volta sozinha e o app
      continua fechando normalmente; religar a T7: ela volta no mesmo lugar.
- [ ] Fotos em **RAW** (e RAW+JPEG): aparecem nas sessões.
- [ ] Fechar com sessões não salvas: pergunta se quer salvar ou apagar.
- [ ] Textos sem emojis em toda a janela (o terminal continua igual).
- [ ] Visual: estrutura do Nocturne (trilho verde à esquerda dos painéis, fontes
      Rajdhani e JetBrains Mono) com as cores do Neo Terminal (preto, verde, âmbar em
      andamento, vermelho para apagar). Sem seletor de temas no topo.
- [ ] Cantos retos em painéis, janelas, botões e etiquetas (nenhum canto cortado).
- [ ] "Save sessions" com mais de uma sessão: abre a lista com todas marcadas;
      "Select all" e "Unselect all" funcionam; sem nenhuma marcada não dá para salvar.
- [ ] Salvar só algumas: pergunta se quer apagar as não marcadas. "Delete them" apaga
      só depois de salvar as escolhidas; "Keep them" deixa as outras no app.
- [ ] Com uma sessão só: "Save sessions" vai direto para a escolha da pasta.
- [ ] Foto grande: abaixo do nome da câmera aparece o nome do arquivo com a extensão
      (com RAW+JPEG, os dois nomes).
- [ ] Mac: fechar o app várias vezes (com câmeras ligadas) sem aparecer relatório de erro.
- [ ] Câmera conectando há mais de 60 s: aparece o aviso com "Restart app"; o botão
      fecha e abre o app de novo, e as sessões não salvas continuam lá.
      Para testar o botão sem câmera presa: abrir o app com a variável STILLPOINT_TEST_RESTART=1
      (Windows/PowerShell: `$env:STILLPOINT_TEST_RESTART=1` antes de abrir; Mac: `STILLPOINT_TEST_RESTART=1 "./..."`).
- [ ] Enquanto uma câmera conecta, "Restart camera connections" fica desativado.
- [ ] Ícone (câmera com o projétil na lente): Windows — nos dois `.exe` no Explorer, na
      barra de título e na barra de tarefas do app de janela. Mac — no `.app` no Finder e
      no Dock com o app aberto (se o Finder mostrar o ícone antigo, mover o `.app` para
      outra pasta e de volta).
- [ ] Nome novo: janela, barra de tarefas/Dock e programas como "Stillpoint"; terminal
      mostra "😎 STILLPOINT"; pacote `stillpoint-vX.Y.Z-<sistema>.zip`.
- [ ] Sessões não salvas da versão antiga aparecem ao abrir o Stillpoint (a pasta
      "Canon Bullet Time" vira "Stillpoint").
- [ ] A versão (ex.: `v2.5.1`) aparece pequena ao lado de STILLPOINT, no topo.

## 8. Instaladores
- [ ] Mac: abrir o `.dmg`: janela com o fundo de chuva, Stillpoint à esquerda, Aplicativos
      à direita, Read Me embaixo; os nomes ficam sobre as placas verdes (modo claro e escuro).
- [ ] Mac: arrastar para Aplicativos e abrir; seguir o Read Me (Ajustes do Sistema →
      Privacidade e Segurança → "Abrir Mesmo Assim"); os nomes dos botões batem com o texto.
- [ ] Windows: abrir o `-setup.exe`: telas com as imagens de chuva; opções "atalho na área de
      trabalho" e "versão de terminal" desmarcadas; instala em Arquivos de Programas.
- [ ] Windows: Stillpoint no menu Iniciar abre e conecta as câmeras; com a opção marcada,
      "Stillpoint Terminal App" também aparece e funciona.
- [ ] Windows: desinstalar por Configurações → Aplicativos; as sessões guardadas continuam
      em `%LOCALAPPDATA%\Stillpoint`.

## 9. Atualização pelo app
- [ ] Abrir o app instalado (Windows: pelo instalador; Mac: em Aplicativos) com
      `STILLPOINT_TEST_VERSION=2.1.0`: aparece "UPDATE x.y.z" no topo (versão publicada).
- [ ] Clicar: janela com "You have 2.1.0 → new x.y.z", novidades (se houver) e "Update now".
- [ ] Com uma câmera conectando, "Update now" fica desativado com o aviso; libera ao conectar.
- [ ] "Update now": barra de andamento; "Cancel" volta para a janela anterior.
- [ ] "Update now" até o fim: o app fecha, instala (Windows pede permissão) e abre de novo
      na versão publicada; sessões não salvas continuam lá.
- [ ] Sem internet: o app abre normalmente, sem aviso.
- [ ] Cópia fora do lugar (Windows: zip; Mac: fora de Aplicativos): a janela oferece
      "Open download page".

## 10. Abertura
- [ ] Ao abrir o app: chuva de código verde aparecendo aos poucos, logo e título com fade in
      e, em ~5 s, o app; caracteres japoneses aparecem (não quadrados).
- [ ] Clique ou tecla durante a abertura: ela some na hora.
- [ ] As câmeras conectam normalmente durante a abertura.

## 11. Play na tela cheia
- [ ] "Reverse loop" marcado (padrão): o play vai e volta (1, 2, 3, 2, 1...).
- [ ] Desmarcado: o play recomeça do início (1, 2, 3, 1, 2, 3...).
- [ ] Botões − e + mudam a velocidade (0.5x a 2x), também com o play rodando; nos limites
      o botão fica desativado.

## 12. Abrir a pasta depois de salvar
- [ ] "Save sessions" com uma sessão: no fim abre a pasta dessa sessão (Finder/Explorador).
- [ ] Com várias sessões salvas de uma vez: abre a pasta escolhida, com todas dentro.

## 13. Salvar como fotos ou sequência

Sem câmeras, copie a sessão de teste (`docs/test-session`) para a pasta de fotos do app com
o app fechado:
- Mac: `mkdir -p ~/Library/Application\ Support/Stillpoint/Photos && cp -R docs/test-session/* ~/Library/Application\ Support/Stillpoint/Photos/`
- Windows (PowerShell): `New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\Stillpoint\Photos" | Out-Null; Copy-Item -Recurse -Force docs\test-session\* "$env:LOCALAPPDATA\Stillpoint\Photos\"`

Salvar tira a sessão do app; copie de novo para testar outra vez.

- [ ] "Save sessions" pergunta Photos ou Sequence.
- [ ] Photos: Original move as fotos como antes; PNG, JPG, WEBP, TIFF e HEIC criam os arquivos
      convertidos (Windows: HEIC desativado se faltar a extensão, com o motivo).
- [ ] Sequence: prévia toca na velocidade escolhida; o total muda com Speed, Loops e Reverse loop.
- [ ] MP4, MOV, GIF, WEBP e AVIF abrem e tocam (GIF/WEBP/AVIF repetem sem parar; MP4/MOV com o
      número de loops escolhido). 4K só no MP4/MOV.
- [ ] "Also save the photos" marcado: fotos e vídeos na mesma pasta; desmarcado: só os vídeos.
- [ ] "Same settings for all sessions" (ligado por padrão): mudar uma sessão muda todas. Desligado:
      cada sessão com a sua, "Different" em âmbar nas diferentes da aberta, "Apply to all sessions"
      copia; ligar de novo copia a configuração da sessão aberta para todas.
- [ ] Andamento aparece; "Cancel" para e as sessões não salvas continuam no app.
- [ ] No fim abre a pasta (uma sessão: a dela; várias: a escolhida).
- [ ] Name: Date + time ("2026-10-14 16.31.00"), Number (01, 02… continuando os que já existem na
      pasta) e Custom (com ou sem data, antes ou depois; nome repetido sem data ganha 01, 02…).
- [ ] Fotos salvas com nome curto ("01_SL3_1.JPG"); subpastas Photos/ e Sequence/ só quando há
      fotos e sequência juntas.
- [ ] Pasta com o mesmo nome já existe: aviso com Keep both / Replace / Cancel.
- [ ] Replace: salve uma sessão como MP4 + GIF; depois salve de novo com o mesmo nome só como WEBP e escolha Replace. A pasta deve ficar só com o WEBP (o MP4 e o GIF antigos somem).
- [ ] Quality (Standard / High / Maximum): aparece na sequência e nas fotos em JPG, WEBP e HEIC
      (não em Original, PNG e TIFF). Salve a mesma sessão em Standard e em Maximum e compare:
      Maximum gera arquivos maiores e o WEBP em Maximum é sem perda.
- [ ] Com RAW: Original salva os arquivos RAW intactos; JPG converte a partir da imagem grande do RAW.
- [ ] Fechar o app com a janela de salvar aberta: aviso com Delete and close / Continue saving.
- [ ] Fechar o app durante o salvamento: Stop and close / Close when done.

## 14. Menu do app e Settings
- [ ] Mac: barra de cima com Stillpoint, Edit, View, Window e Help.
- [ ] Mac: ⌘C, ⌘V e ⌘A funcionam no campo de nome *Custom* da janela de salvar.
- [ ] Mac: ⌘Q fecha o app; com sessões não salvas, aparece o mesmo aviso do botão de fechar.
- [ ] Mac: ⌘H esconde; ⌘M minimiza; ⌃⌘F tela cheia da janela.
- [ ] Windows: botão de menu (≡) no canto direito da barra; no Mac ele não aparece.
- [ ] About: nome, versão, licença; Licenses e GitHub abrem as páginas.
- [ ] Check for updates: sem versão nova, "Stillpoint is up to date"; sem internet, aviso de erro.
- [ ] Settings (⌘, no Mac, Ctrl+, no Windows): tema Nocturne Neo e idioma English marcados; Done fecha.
- [ ] Help, Compatible cameras, What's new e Report a problem abrem as páginas do GitHub.
- [ ] Esc fecha o menu, o About e o Settings.
- [ ] Countdown, Sessions e Download wait: só aceitam números inteiros (letras, ponto e sinal
      não entram); no máximo 3 dígitos; vazio ou abaixo do mínimo volta ao padrão ao sair do
      campo; setas ↑ ↓ somam ou tiram 1.
- [ ] Cancelar o bullet time (com câmeras): Countdown 10 e Sessions 3.
  - [ ] Na contagem: botão Cancel (ou Esc) para na hora; nada é disparado e não aparece sessão vazia.
  - [ ] Entre sessões: "Cancel bullet time" (o botão de disparo) depois da 1ª sessão; a 2ª não
        começa e a 1ª fica no app.
  - [ ] Esperando as fotos: "Cancel bullet time"; as fotos que chegaram ficam na sessão.
  - [ ] Atividade mostra "Bullet time cancelled"; o botão volta a "It's bullet time".
  - [ ] Terminal app: sem mudança (não tem cancelamento).

## 15. Reconexão (2.6.1)
- [ ] Com as câmeras conectadas, desligue e religue uma delas: ela volta sozinha (Activity com
      "Show technical details" mostra "USB: Canon camera plugged in or turned on").
- [ ] Repita com a outra câmera e depois com as duas ao mesmo tempo.
- [ ] Desligue uma câmera, espere ela aparecer como desconectada, clique em "Restart camera
      connections" e religue: ela volta.
- [ ] Disparo normal depois de reconectar: fotos chegam das duas câmeras.

## 16. Camera settings (etapa 12)
- [ ] Painel Cameras: número de câmeras ao lado do título; com várias câmeras só a lista rola e o
      cabeçalho fica fixo.
- [ ] Arrastar uma câmera para outra posição muda a ordem (01, 02...), igual às setas.
- [ ] "Camera settings" abre a tabela com os valores reais de cada câmera (confira na própria câmera).
- [ ] Mudar ISO, velocidade, abertura, Picture Style e qualidade de uma câmera: a câmera muda
      na hora (confira no visor/tela dela).
- [ ] Ponha uma câmera em disparo contínuo (ou timer) na própria câmera: o app volta ela para
      disparo único sozinho (Activity: "... was set to single shooting"). Se não conseguir, o
      painel mostra "Not on single shot".
- [ ] "All cameras": mudar um valor muda em todas. White balance: lista os predefinidos que todas
      aceitam (Daylight, Cloudy...); os valores em Kelvin só aparecem se todas aceitarem Kelvin
      (a SL3 e a T7 não aceitam).
- [ ] WB fine-tune (B/A e M/G) muda na câmera.
- [ ] Copy from camera: todas ficam iguais à escolhida.
- [ ] Câmera com o disco fora do M (ex.: Auto): linha com o aviso para girar para M; no painel,
      "Settings on the camera only". Girando para M, a linha volta a ter as opções sozinha.
- [ ] Câmera com valor diferente: âmbar na tabela e "Settings differ" no painel.
- [ ] Lente em AF: Focus em âmbar; em MF: "MF".
- [ ] Mudar algo direto na câmera com a janela aberta: a tabela atualiza sozinha em até ~3 s
      (ou com "Read again").
- [ ] Test shot com "Match automatically" desligado: dispara todas as câmeras (sem contagem e sem
      som) e mostra uma fileira de miniaturas, uma por câmera, na ordem; a tabela fica bloqueada
      enquanto fotografa. As fotos do teste não viram sessão e somem ao fechar a janela.
- [ ] Clicar numa miniatura abre a foto grande por cima da janela; ← → passam de câmera; Esc (ou
      Close, ou clicar fora da foto) fecha só a foto grande; Esc de novo fecha a janela.
- [ ] "Hide photos" recolhe a fileira e vira "Show photos"; um novo Test shot mostra a fileira aberta.
- [ ] Test shot com "Match automatically" ligado (padrão), câmeras com balanço de branco em Auto e
      valores diferentes (ex.: velocidade 1/200 numa e 1/250 na outra): tira até 5 fotos sozinho
      ("Photo 2 of up to 5 · Finding a common white balance"...), e no fim mostra as fotos e o
      resumo do que mudou em cada câmera (velocidade, ISO, White balance, WB fine-tune, Picture
      Style). Confira na tela de cada câmera que os valores mudaram. As fotos finais devem
      parecer mais iguais entre si do que antes (cor e brilho).
- [ ] Rodar o Test shot de novo logo depois: muda pouco ou nada ("Nothing to change").
- [ ] Primeira vez com um modelo: aparecem as fotos "Calibrating the cameras (blue/amber)" e
      "(green/magenta)"; no fim, o ajuste fino das câmeras não ficou no extremo (A9/G9). Na
      vez seguinte essas duas fotos não aparecem. (Para calibrar de novo: apagar
      camera-calibration.txt na pasta de dados do app.)
- [ ] Câmera com o disco fora do M durante o ajuste: ela aparece no resumo como não ajustada.
- [ ] Com ISO em Auto e "Match automatically" ligado: antes de começar aparece "ISO is on Auto"
      com Cancel e Continue anyway.
- [ ] Durante o Test shot o botão vira "Cancel". Cancelar no meio (inclusive nas fotos de
      calibração): para, mostra "Test shot cancelled" e as configurações das câmeras voltam
      como estavam antes (conferir na tabela e na câmera). Sem ajuste: só para.
- [ ] WB fine-tune: "G" (verde) e "M" (magenta) batem com o que a câmera mostra no menu.
      Conferido (2026-10-15): T7 e SL3 (G5 no app = G5 no menu WB Shift/BKT).
- [ ] Câmera de modelo ainda não conferido: pôr só G5 pelo app (lista da direita do WB
      fine-tune) e ver no menu WB Shift/BKT da câmera se aparece G5. Anotar o modelo e o
      resultado em "Observações" abaixo; se aparecer M5, o modelo é invertido.
- [ ] O (?) mostra a legenda ao passar o mouse; Esc fecha a janela.
- [ ] Disparo normal depois de mudar as configurações; terminal app sem mudança.
- [ ] Com o app aberto (com e sem a janela Camera settings aberta), desligar as câmeras: o app
      não fecha sozinho; elas aparecem como desconectadas e voltam ao religar.

## Resultado

| Data | Sistema | Branch/versão | Câmeras | Passou? | Observações |
| --- | --- | --- | --- | --- | --- |
| 2026-10 | Windows | v1.3.0 | Canon SL3 + Canon T7 | sim | Teste de referência, antes da migração |
| 2026-10-10 | Windows | etapa-1-cmake (CMake + EDSDK 13.20.10) | Canon SL3 + Canon T7 | sim | Checklist completa |
| 2026-10-10 | Windows | etapa-2-plataforma (camada de plataforma) | Canon SL3 + Canon T7 | sim | Teste reduzido: detecção, pasta Downloads, áudio, 2 sessões, desconexão |
| 2026-10-10 | Mac (M5, macOS 26) | etapa-3-macos | Canon SL3 + Canon T7 | sim | Primeiro teste no Mac: detecção, áudio, BULLET TIME, download |
| 2026-10-10 | Windows | etapa-3-macos | Canon SL3 + Canon T7 | sim | Conferência após mudanças da etapa 3: compila e detecta as câmeras |
| 2026-10-10 | Mac (M5, macOS 26) | fix-feedback-conexao | Canon SL3 + Canon T7 | sim | Reconexão da T7: progresso na conexão, espera automática, SL3 mantida conectada |
| 2026-10-10 | Windows | fix-feedback-conexao | — | não testado | Só compilado; teste com câmeras dispensado pelo dono do projeto |
| 2026-10-10 | Mac + Windows | etapa-4-core | Canon SL3 + Canon T7 (Mac) | sim | Compila nos dois; Mac detecta as câmeras. Saída idêntica à versão anterior em teste simulado (SDK falso) |
| 2026-10-10 | Mac + Windows | etapa-diagnostico | Canon SL3 + Canon T7 (Mac) | sim | Mac: guia sem câmeras, detecção sem aviso de compatibilidade, T7 dispara sem cartão (configurada para isso). Windows: compila |
| 2026-10-14 | Mac + Windows | etapa-5-interface | Canon SL3 + Canon T7 | sim | Versão de janela e de terminal nos dois; app de Mac (.app) com dois cliques; "Restart app" testado nos dois |
| 2026-10-14 | Mac + Windows | etapa-6-icone-e-nome | — | sim | Ícone e nome Stillpoint funcionando nos dois (confirmado pelo dono do projeto) |
| 2026-10-14 | Mac + Windows | etapa-7-instaladores | — | sim | `.dmg` (fundo, posições, Read Me) e instalador de Windows aprovados pelo dono do projeto |
| 2026-10-14 | Mac + Windows | etapa-8-atualizacao | Canon T7 (Mac) | sim | Atualização simulada (2.1.0 → 2.2.0) aprovada pelo dono do projeto nos dois sistemas |
| 2026-10-14 | Mac + Windows | etapa-9-abertura | — | sim | Abertura aprovada pelo dono do projeto nos dois sistemas |
| 2026-10-14 | Mac + Windows | v2.4.0 | — | sim | Primeira atualização real pelo app (2.3.0 → 2.4.0) funcionou nos dois sistemas |
| 2026-10-15 | Mac + Windows | etapa-10-exportacao | — (sessão de teste) | sim | Exportação (fotos e sequência), Replace e qualidade aprovados pelo dono do projeto; qualidade com RAW real fica pendente |
| 2026-10-15 | Mac + Windows | etapa-11-menu | Canon SL3 + Canon T7 | sim | Menu do Mac, botão de menu do Windows, About, Settings, campos numéricos e cancelamento do bullet time aprovados pelo dono do projeto |
| 2026-10-15 | Mac + Windows | fix-reconexao (2.6.1) | Canon SL3 + Canon T7 | sim | Câmeras religadas voltam sozinhas; Restart camera connections reconecta todas; resto sem mudança (aprovado pelo dono do projeto) |
| 2026-10-15 | Mac (+ Windows compilado) | etapa-12-configuracoes (2.7.0) | Canon SL3 + Canon T7 | sim | Camera settings, All cameras, Test shot, Match automatically com calibração e Cancel aprovados pelo dono do projeto no Mac; G/M conferido na SL3 e na T7 |
