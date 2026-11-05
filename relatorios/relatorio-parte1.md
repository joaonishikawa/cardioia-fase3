# CardioIA Fase 3 — Relatório da Parte 1: Edge Computing

**Integrantes:** Gustavo Trindade Soares (RM567848) e João Pedro Nishikawa Alves (RM562376)

## 1. Objetivo

Este protótipo simula um monitor cardíaco vestível que continua coletando sinais vitais mesmo sem internet. O processamento ocorre no próprio ESP32 (borda da rede), porque a perda de conexão não pode interromper o acompanhamento de um paciente cardiológico.

## 2. Circuito

| Componente | Pino | Função |
|---|---|---|
| DHT22 | GPIO 15 | Sensor 1: temperatura e umidade |
| Botão "Batimento" | GPIO 4 | Sensor 2: cada toque simula um batimento cardíaco |
| Chave deslizante | GPIO 5 | Define a variável booleana `wifiConectado` |
| LED "Alerta" | GPIO 2 | Alerta local, que funciona sem internet |

## 3. Fluxo de funcionamento

1. **Captura.** A cada 5 s, o firmware lê temperatura e umidade. O botão usa uma interrupção, para que nenhum toque se perca enquanto o programa tenta reconectar.
2. **Cálculo do BPM.** O valor é a média dos intervalos entre os últimos 6 toques. Toques com menos de 200 ms de diferença são ignorados (*debounce*). Sem toque por 6 s, o BPM vale 0, que significa "sem leitura". Uma pausa longa também encerra a janela de cálculo, para que toques antigos não distorçam o resultado.
3. **Decisão na borda.** O LED acende se a temperatura passar de 38 °C ou se o BPM ficar acima de 120 ou entre 1 e 49.
4. **Armazenamento.** Toda leitura válida entra primeiro no buffer local, esteja o dispositivo online ou offline. Assim existe um único caminho para os dados, e não um tratamento especial para a falta de rede.
5. **Envio.** Com a conexão ativa, a amostra mais antiga é enviada pelo `Serial.println` e por MQTT, e só depois é apagada do buffer.

## 4. Lógica de resiliência offline

A chave deslizante simula a conectividade. Na posição offline, `wifiConectado` fica `false`, o MQTT é desconectado e a coleta continua normalmente. Ao voltar para online, o firmware reconecta e esvazia o buffer na ordem de chegada (FIFO), com até 20 envios por segundo, sem bloquear a coleta. Cada mensagem informa `idade_s`, a idade da leitura em segundos, para que o dashboard coloque a amostra no horário em que ela foi medida. Falhas reais de Wi-Fi ou do broker têm o mesmo tratamento: a amostra só sai do buffer quando o `publish` é aceito.

## 5. Estratégia de armazenamento limitado

O buffer é circular e guarda **4.000 amostras**. Com uma leitura a cada 5 s, isso cobre **cerca de 5 h 33 min sem conexão**, em 64 KB de RAM. Esse período é suficiente para perdas de sinal comuns no dia a dia de um paciente, como deslocamentos ou uma noite em local com rede instável. Tentamos 5.000 amostras primeiro, mas a compilação falhou porque esse volume não cabe na memória estática do ESP32 (`region dram0_0_seg overflowed by 3080 bytes`).

Cada amostra ocupa 16 bytes, pois temperatura e umidade são guardadas em décimos de unidade, como inteiros de 16 bits. Com o buffer cheio, a amostra mais antiga é descartada e o contador `descartadas` aumenta. Priorizamos a leitura mais recente porque ela é a mais útil para decidir o atendimento.

## 6. Sobre o SPIFFS

O SPIFFS é opcional e não foi usado, pois é volátil nos simuladores, como explica o enunciado. O buffer fica na RAM, e toda leitura é impressa no Monitor Serial com a tag `[LEITURA]`, a alternativa de resiliência aceita pela atividade. A limitação é que uma queda de energia apaga os dados pendentes. Em um dispositivo físico, o buffer deveria ser gravado em SPIFFS ou em cartão microSD.

## 7. Testes realizados no Wokwi

| Teste | Resultado observado |
|---|---|
| Coleta com a chave em offline | As leituras continuaram; `pendentes` subiu a cada amostra |
| Volta para online com 10 amostras pendentes | Todas enviadas em ordem, de `seq` 7 a 16, sem perdas, com `idade_s` de 45 a 0 |
| Buffer cheio (versão de teste com capacidade 3) | `pendentes` parou em 3 e `descartadas` subiu; só as 3 últimas foram enviadas |
| Batimentos a cada 800 ms e a cada 400 ms | BPM medido perto de 75 e de 150, como esperado |
| Taquicardia offline e temperatura de 39,5 °C | O LED acendeu nos dois casos |

Nos testes de BPM, os toques foram gerados por um script temporizado, para manter o ritmo constante. A temperatura foi alterada pelo controle do DHT22 no próprio simulador.
