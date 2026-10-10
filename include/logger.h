#pragma once

// Saída de mensagens de texto do núcleo do programa.
// Por padrão vai para o terminal; uma interface gráfica pode trocar o destino
// com SetLogSink, sem mudar o código que gera as mensagens.

#include <functional>
#include <iostream>
#include <string>

using LogSink = std::function<void(const std::string&)>;

inline LogSink& CurrentLogSink()
{
    static LogSink sink = [](const std::string& message) { std::cout << message << std::endl; };
    return sink;
}

inline void SetLogSink(LogSink sink)
{
    CurrentLogSink() = std::move(sink);
}

inline void Log(const std::string& message)
{
    CurrentLogSink()(message);
}
