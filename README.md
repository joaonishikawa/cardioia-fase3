# CardioIA — Fase 3: Monitoramento Contínuo (IoT na Saúde)

Protótipo de monitor cardíaco vestível: um ESP32 no Wokwi captura sinais vitais, guarda as leituras sem internet, envia por MQTT e alimenta um dashboard no Node-RED com alertas automáticos.

**Projeto no Wokwi:** https://wokwi.com/projects/479714028291723265

O projeto no Wokwi é público; por isso, essa cópia não traz nomes nem RMs. O código é igual ao de `firmware/sketch.ino`, exceto por esse cabeçalho e pelo autor no diagrama.

## Integrantes

| Nome completo | RM |
|---|---|
| Gustavo Trindade Soares | RM567848 |
| João Pedro Nishikawa Alves | RM562376 |
| Guilherme Filartiga | RM568034 |

## Entregáveis

| Atividade | Entregável | Arquivo |
|---|---|---|
| Parte 1 | Link do projeto no Wokwi | Topo deste README |
| Parte 1 e 2 | Código C++ comentado do ESP32 | `firmware/sketch.ino` |
| Parte 1 | Relatório (mínimo de 1 página) | `relatorios/relatorio-parte1.pdf` |
| Parte 2 | Export do dashboard | `node-red/flows.json` |
| Parte 2 | Prints da aplicação | `prints/` |
| Parte 2 | Relatório (mínimo de 2 páginas) | `relatorios/relatorio-parte2.pdf` |

## Como executar

**1. ESP32 no Wokwi.** Abra o link acima e clique no botão verde de play. Para criar o projeto do zero, use https://wokwi.com/projects/new/esp32 e copie `sketch.ino`, `diagram.json` e `libraries.txt`, da pasta `firmware/`.

**2. Dashboard.** Requer Node.js 18 ou superior.

```bash
cd node-red
npm install
npm start
```

Abra o painel em http://127.0.0.1:1880/ui. O editor do fluxo fica em http://127.0.0.1:1880.

## Como testar

| Ação no Wokwi | Resultado esperado |
|---|---|
| Clicar no botão vermelho em ritmo constante | O BPM aparece no gráfico (75 bpm é cerca de um toque a cada 0,8 s) |
| Clicar no DHT22 e subir a temperatura acima de 38 °C | LED físico aceso e ALERTA no dashboard, na leitura seguinte (até 5 s) |
| Clicar mais de duas vezes por segundo | ALERTA de taquicardia (acima de 120 bpm) |
| Deslizar a chave para a direita (offline) | A coleta continua, `pendentes` sobe e o painel mostra SEM DADOS após 20 s |
| Deslizar a chave para a esquerda (online) | `[SYNC]` no Monitor Serial e amostras no gráfico, no horário em que foram medidas |

## Decisões principais

- **Sensores:** DHT22 (temperatura e umidade) e botão que simula os batimentos.
- **Resiliência:** buffer circular de 4.000 amostras na RAM (cerca de 5 h 33 min offline); cheio, descarta a mais antiga.
- **SPIFFS:** opcional e não usado, pois é volátil nos simuladores, como explica o enunciado.
- **Limites de alerta:** BPM acima de 120, BPM entre 1 e 49, temperatura acima de 38 °C.
- **Grafana:** opcional e não usado.

> **Aviso:** o broker é público e não criptografado; use apenas dados fictícios. Projeto acadêmico, sem validação clínica.
