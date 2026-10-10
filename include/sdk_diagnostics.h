#pragma once

// Diagnóstico para orientar quem usa o programa: compatibilidade de modelos
// e explicação curta dos erros mais comuns do SDK da Canon.

#include <string>

// true se o modelo está na lista de compatibilidade da Canon usada pelo app
// (considera os nomes regionais: Rebel T7 = EOS 2000D = Kiss X90, etc.).
bool IsOnCompatibilityList(const std::string& modelName);

// Frase curta com o que fazer diante de um erro do SDK; "" se não houver dica.
std::string DescribeConnectionError(long error);
std::string DescribeShotError(long error);
