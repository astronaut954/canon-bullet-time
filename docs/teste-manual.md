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

## 7. App com janela (Canon Bullet Time GUI)
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
- [ ] **Reorder** numa sessão: arrastar as miniaturas (ou usar as setas) e **Save
      order** renomeia as fotos da sessão na nova ordem (RAW e JPEG juntos).
- [ ] Sessão em **tela cheia** (botão ao lado de "Delete session" ou tecla F; Esc sai):
      play/pausa, setas, **Reorder** e apagar uma foto (lixeira ou tecla Delete).
- [ ] Desligar a T7 e depois desligar/religar a SL3: a SL3 volta sozinha e o app
      continua fechando normalmente; religar a T7: ela volta no mesmo lugar.
- [ ] Fotos em **RAW** (e RAW+JPEG): aparecem nas sessões.
- [ ] Fechar com sessões não salvas: pergunta se quer salvar ou apagar.
- [ ] Textos sem emojis em toda a janela (o terminal continua igual).

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
