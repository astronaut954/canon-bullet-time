# Roadmap — Canon Bullet Time

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
| 2 | Isolar código de Windows | Áudio, caminhos, console e funções da Microsoft atrás de interfaces de plataforma. Windows funciona igual. | próximo |
| 3 | Versão Mac | SDK de Mac integrado, áudio e caminhos de Mac, checklist passa no Mac. Primeiro `.zip` para Mac (sem assinatura Apple por enquanto). | depois |
| 4 | Separar terminal da lógica | `main.cpp` vira só o menu; a lógica de câmera fica chamável por outra interface, com espaço para outras marcas. | depois |
| 5 | Interface gráfica | Usa a mesma lógica do terminal. | depois |

### Critério para fechar uma etapa

1. Compila nos sistemas que a etapa toca.
2. `docs/teste-manual.md` passa com câmeras reais no(s) sistema(s) afetado(s).
3. Branch volta para `main`; se fizer sentido, sai uma versão (`vX.Y.Z`).

## O que funciona em cada sistema

| Função | Windows | Mac |
| --- | --- | --- |
| Detectar / listar câmeras | sim | — |
| Disparo sincronizado (BULLET TIME) | sim | — |
| Download e organização das fotos | sim | — |
| Reordenar câmeras / prefixos | sim | — |
| Pasta de saída configurável | sim | — |
| Áudio de contagem | sim | — |
| Pacote de distribuição (`.zip`) | sim | — |

Atualizar esta tabela sempre que uma função mudar de status.

## Pendências conhecidas

- **EDSDK fora do repositório:** decidido manter o SDK da Canon só dentro do
  `.zip` das versões (o que os termos da Canon permitem). Os arquivos do SDK
  saíram do repositório; cópias antigas continuam no histórico do Git.

## Histórico de versões do SDK

| Onde | Versão em uso |
| --- | --- |
| Windows (máquina de build) | 13.20.10.6402 (desde a etapa 1) |
| GitHub | nenhum (SDK fica só na máquina de cada um) |
| Mac | — (pacote 13.20.10 disponível em `~/Downloads/ESDK`, entra na etapa 3) |
