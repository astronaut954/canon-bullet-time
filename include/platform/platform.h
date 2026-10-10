#pragma once

// Funções que dependem do sistema operacional.
// Cada sistema tem sua própria implementação em src/platform/<sistema>/.
// O restante do programa usa apenas estas funções, nunca APIs do sistema direto.

#include <ctime>
#include <filesystem>

namespace Platform
{
    // Prepara o terminal para exibir acentos e emojis (UTF-8).
    void InitConsole();

    // Pasta onde está o executável do programa.
    std::filesystem::path ExecutableDirectory();

    // Pasta "Downloads" do usuário (ou a pasta atual, se não for possível descobrir).
    std::filesystem::path DownloadsFolder();

    // Converte um horário para a hora local do computador.
    std::tm LocalTime(std::time_t t);

    // Processa eventos do sistema por até `milliseconds` (0 = só os pendentes).
    // No Mac, o SDK da Canon entrega eventos das câmeras por aqui; no Windows não faz nada.
    void ProcessEvents(int milliseconds = 0);

    // true se o usuário digitou algo no terminal (sem esperar). Usado para cancelar esperas.
    bool InputAvailable();
}
