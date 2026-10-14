# Regras para agentes (Claude, Codex ou outro)

Leia o `ROADMAP.md` antes de qualquer trabalho. Ele define as decisões fixas e a
etapa atual.

## Contexto

- O dono do projeto não é desenvolvedor. Explique em português, sem jargão,
  e separe sempre o que é **dado verificado** do que é **inferência**.
- Ele testa com câmeras Canon reais, no Windows (acesso via SSH) e no Mac
  (Apple Silicon). Nenhum agente consegue testar câmeras: só ele.

## Como trabalhar

1. Uma etapa do roadmap por vez, num branch `etapa-N-nome`.
2. Não mudar comportamento do app fora do escopo da etapa.
3. Antes de dizer que terminou: compilar no(s) sistema(s) afetado(s) e pedir que
   ele rode `docs/teste-manual.md`.
4. Atualizar o status no `ROADMAP.md` ao fechar a etapa.
5. Commit e push só com autorização explícita dele.

## Não fazer

- Não adicionar nem alterar arquivos em `third_party/` sem pedido explícito.
- Não colocar o SDK da Canon (headers, DLLs, framework) em lugar público.
- Não reintroduzir Windows 32-bit nem Mac Intel.
- Não começar interface gráfica nem suporte a Sony/Nikon antes da etapa
  correspondente.

## Organização do código

| Pasta | O que tem | Regra |
| --- | --- | --- |
| `src/core/` + `include/bullet_time.h`, `include/canon_controller.h` | Núcleo: câmeras, conexão, sequência de disparo, downloads | Não lê teclado nem escreve no terminal. Fala com a interface pelo `BulletTimeListener` (eventos) e pelo `Log()` (mensagens de texto). |
| `src/cli/` | Interface de terminal: menus, textos, leitura do teclado | Só chama o núcleo. Os textos da tela ficam aqui. |
| `src/gui/` + `assets/ui/` | Interface gráfica: janela nativa (biblioteca webview) com página HTML/CSS/JS | `main_gui.cpp` traduz os avisos do núcleo em eventos para a página e roda as câmeras numa thread própria. Visual em `assets/ui/themes/nocturne-neo.css` (variáveis no topo). `photo_store.cpp`: fotos guardadas na pasta de dados do app, prévias e salvar. `native_window_*.cpp`: seletor de pastas e confirmação ao fechar (Windows/Mac). |
| `src/platform/<sistema>/` + `include/platform/platform.h` | Tudo que muda entre Windows e Mac | Único lugar com APIs do sistema. |
| `resources/` | Ícone do app (`icon/`, gerado por `scripts/make_icon.py`) e o recurso de Windows que o embute no `.exe` (`windows/app.rc.in`) | Não editar os arquivos gerados à mão: mudar o script e rodar de novo. |

Os textos da tela são definidos pelo dono do projeto: manter exatamente os mesmos
emojis, espaçamentos e palavras ao mexer em `src/cli/` (ex.: `Option:` nos menus,
dois espaços depois de `⚠️`).

**A versão de terminal é a referência estável e não pode mudar de comportamento**
enquanto a interface gráfica é desenvolvida. Mudanças no núcleo precisam manter a
saída do terminal idêntica (conferir com o SDK falso).

## Onde as coisas estão

| Item | Caminho |
| --- | --- |
| Projeto no Mac | `~/Documents/canon-bullet-time` |
| Projeto no Windows | `C:\Users\ra-mo\source\repos\canon-bullet-time` |
| Pacotes do SDK (Mac) | `~/Downloads/ESDK` |
| Pacotes do SDK (Windows) | `third_party\*.zip` (fora do Git) |
| Repositório | https://github.com/astronaut954/canon-bullet-time |
