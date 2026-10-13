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
| 2 | Isolar código de Windows | Áudio, caminhos, console e funções da Microsoft atrás de interfaces de plataforma. Windows funciona igual. | feito |
| 3 | Versão Mac | SDK de Mac integrado, áudio e caminhos de Mac, checklist passa no Mac. Primeiro `.zip` para Mac (sem assinatura Apple por enquanto). | feito |
| 4 | Separar terminal da lógica | `main.cpp` vira só o menu; a lógica de câmera fica chamável por outra interface, com espaço para outras marcas. | feito |
| 4.1 | Diagnóstico para o usuário | Aviso de modelo fora da lista de compatibilidade (com nomes regionais), guia quando nenhuma câmera é encontrada, explicação dos erros mais comuns de conexão e disparo. | feito |
| 5 | Interface gráfica | Janela nativa com a interface em HTML/CSS (WebView2 no Windows, WebKit no Mac), usando o mesmo núcleo do terminal. Visual ajustável em `assets/ui/themes/` (um arquivo por tema). | em andamento |

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
"Mix 1" (`nocturne-neo-1.css`, fontes do Nocturne) e "Mix 2" (`nocturne-neo-2.css`,
fonte e efeitos do Neo Terminal). Elas trocam no topo da janela. Quando ele escolher
uma, a outra e o seletor saem do app.

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
| Reordenar câmeras / prefixos | sim | a testar |
| Pasta de saída configurável | sim | a testar |
| Áudio de contagem | sim | sim |
| Pacote de distribuição (`.zip`) | sim | sim (sem assinatura Apple) |

Atualizar esta tabela sempre que uma função mudar de status.

## Pendências conhecidas

- **EDSDK fora do repositório:** decidido manter o SDK da Canon só dentro do
  `.zip` das versões (o que os termos da Canon permitem). Os arquivos do SDK
  saíram do repositório; cópias antigas continuam no histórico do Git.

- **Windows: conexão em paralelo não testada com câmeras.** Desde a branch
  `fix-feedback-conexao`, a sessão de cada câmera é aberta numa thread auxiliar
  (com COM inicializado, como pede o manual da Canon). Compila, mas ainda não
  foi testado com câmeras no Windows. Testar antes da próxima versão publicada.

## Histórico de versões do SDK

| Onde | Versão em uso |
| --- | --- |
| Windows (máquina de build) | 13.20.10.6402 (desde a etapa 1) |
| GitHub | nenhum (SDK fica só na máquina de cada um) |
| Mac (máquina de build) | 13.20.10.0 (desde a etapa 3) |
