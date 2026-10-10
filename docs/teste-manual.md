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
- [ ] Abrir o app **com as câmeras já ligadas**: cada câmera aparece conectada
      assim que fica pronta (a rápida não espera a lenta).
- [ ] Abrir o app e **depois** ligar as câmeras: elas aparecem sozinhas.
- [ ] Sem nenhuma câmera: depois de alguns segundos aparece o guia "No cameras found".
- [ ] Desligar uma câmera: ela fica "Disconnected"; religar: volta conectada no
      mesmo lugar (mesmo nome e ordem), sem clicar em nada.
- [ ] Disparar depois de religar uma câmera: a foto dela também chega.
- [ ] **Restart camera connections** (no painel do log): todas reconectam.
- [ ] Fotos em **RAW** (e RAW+JPEG): aparecem na aba Sessions.
- [ ] Fechar com sessões não salvas: pergunta se quer salvar ou apagar.

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
