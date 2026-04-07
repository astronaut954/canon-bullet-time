import os
from pathlib import Path

def gerar_arvore(diretorio, prefixo=""):
    # Lista o conteúdo e ordena (pastas primeiro, depois arquivos)
    itens = sorted(list(diretorio.iterdir()), key=lambda x: (x.is_file(), x.name.lower()))
    
    # Filtros opcionais: ignora pastas ocultas ou de cache
    itens = [item for item in itens if not item.name.startswith(('.', '__'))]
    
    total = len(itens)
    for i, item in enumerate(itens):
        e_o_ultimo = (i == total - 1)
        conector = "└── " if e_o_ultimo else "├── "
        
        print(f"{prefixo}{conector}{item.name}")
        
        if item.is_dir():
            # Define o recuo para a próxima subpasta
            proximo_prefixo = prefixo + ("    " if e_o_ultimo else "│   ")
            gerar_arvore(item, proximo_prefixo)

if __name__ == "__main__":
    # Define o caminho do diretório onde o script está localizado
    caminho_script = Path(__file__).parent.resolve()
    
    print(f"Estrutura de: {caminho_script.name}/\n")
    gerar_arvore(caminho_script)