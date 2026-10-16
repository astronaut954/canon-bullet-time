# Caixa de disparo por cabo (Stillpoint Trigger Box)

Dispara as câmeras pela entrada de controle remoto (padrão Canon E3 / RS-60E3), com um
ESP32 e um optoacoplador por canal, para a sincronia e os intervalos não dependerem do USB.
O guia completo de montagem, com lista de compras e custos, está em
`docs/hardware/caixa-de-disparo.pdf` (gerado por um script fora do repositório; as figuras
estão em `images/`).

- `firmware/stillpoint_trigger/stillpoint_trigger.ino`: programa do ESP32 (Arduino IDE; placa
  "ESP32 Dev Module"; biblioteca "Adafruit MCP23017 Arduino Library"). Ainda não testado em
  hardware.
- Canais: 1 e 2 direto nos GPIO 25 e 26; 3 em diante em módulos MCP23017 (16 por módulo,
  endereços 0x20 a 0x27).
- Protocolo serial (115200, uma ordem por linha): `PING`, `PULSE ms`, `FIRE 1,2,5-8|ALL`,
  `PLAN 1-6:0,7-12:1000`, `GO`, `CLEAR`, `STOP`, `TEST n`. Respostas `OK`, `ERR motivo`, `DONE`.
