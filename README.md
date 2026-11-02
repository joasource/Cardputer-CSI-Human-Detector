# Cardputer Detector de Humanos

Este mini cyber deck detecta pessoas usando Wi-Fi. Sem câmera. Sem sensor infravermelho. Sem câmera térmica. Só Wi-Fi e esta tela de radar profundamente amaldiçoada.

Ele usa Channel State Information (CSI), ou seja, o mesmo Wi-Fi que o seu roteador já está espalhando por aí, para detectar quando há gente se movendo. O corpo humano é feito principalmente de água, e a água bagunça as ondas de rádio de um jeito bem mensurável. Então a gente mede.

O projeto todo custa cerca de US$ 50. O Cardputer ADV custa US$ 30, pegue [duas telas TFT destas por US$ 20](https://a.co/d/0bZbfTqO), arrume alguém com uma impressora 3D e pronto: você tem um radar de humanos portátil com cara de totalmente fora de controle. Estou muito, muito feliz com o resultado.

> **Versão em português (pt-BR)** com melhorias e tradução completa por **Joaquim Ferreira** — [joaquimfsneto@gmail.com](mailto:joaquimfsneto@gmail.com).

---

## Como funciona

Os sinais de Wi-Fi batem em tudo dentro de um cômodo: paredes, móveis, portas, o seu gato e esse estranho saquinho de carne que você chama de corpo. O CSI captura a *impressão digital* desses reflexos. Quando algo se mexe, essa impressão digital muda. Este firmware acompanha essas mudanças e as usa para detectar presença e movimento em tempo real.

Tudo roda no próprio Cardputer ADV. Sem sensor externo, sem segundo dispositivo. O ESP32-S3 tem dois núcleos: um cuida do sensoriamento e o outro das telas. Tudo é autocontido.

---

## Hardware

| Placa | Função |
|-------|--------|
| M5Stack Cardputer ADV (ESP32-S3 / StampS3) | Interface do console + sensor CSI |

Uma [tela TFT ILI9341](https://a.co/d/0bZbfTqO) externa é ligada ao conector GPIO do Cardputer ADV para o escopo do radar.

---

## Telas

As duas telas funcionam ao mesmo tempo mostrando coisas diferentes (não é um clone, é conteúdo realmente distinto):

- **Em cima, ILI9341 externa:** escopo de radar estilo PPI. Varredura giratória com rastro de fósforo. Quando o movimento ultrapassa o limiar, desenha os blips de contato; o raio vem do RSSI, e o brilho e o tamanho vêm da intensidade do movimento.
- **Embaixo, tela embutida:** IP do WiFi, indicador de status, faixa CONTATO / LIVRE, gráfico de movimento rolante, valor do limiar e dicas de teclas.

---

## Calibração e limiar

O radar mede o movimento **em relação à sua sala vazia**, então ele calibra primeiro:

- Na inicialização (e depois de escolher uma rede WiFi) as telas mostram **CALIBRANDO** com uma barra de progresso por cerca de 13 s: 5 s para você se afastar e depois 8 s aprendendo como é a sala em silêncio. Fique parado ou saia da sala enquanto roda. Aperte `c` (ou **CAL** no celular) para refazer quando quiser, por exemplo depois de mover o Cardputer.
- Se ficar em "aguardando tráfego WiFi", ainda não há quadros CSI no canal; ele termina assim que algo na rede transmitir.
- Depois disso, movimento 0 % significa "tão quieto quanto a sala vazia". O **limiar** (`,` / `/`, ou LIM− / LIM+ no celular) é a linha do gráfico: tudo acima dela é um contato, mantido por 10 s. O padrão de 35 % equivale a cerca de 3x o ruído da sala vazia; aumente se aparecerem falsos contatos, diminua para captar movimentos menores. O limiar é salvo e sobrevive a reinicializações.

---

## Sem tela externa? Use o celular

O Cardputer também serve o escopo do radar como uma página web, então o seu celular pode ser a segunda tela. Não precisa instalar app: é só uma página no navegador do celular.

1. Confirme que o celular está no **mesmo WiFi** do Cardputer.
2. Depois da inicialização, a tela embutida mostra um **QR code**. Escaneie com a câmera do celular (ou aperte `q` para trazê-lo de volta).
3. Você também pode digitar o IP mostrado na barra de título, por exemplo `http://192.168.1.42/`, ou tentar `http://radar.local/`.

O celular desenha o escopo completo sozinho (varredura PPI e a vista 3D vaporwave, toque no escopo para alternar), além do gráfico de movimento, dos botões de limiar e calibração, da troca de paleta e de um som de sonar opcional. Só alguns números por quadro trafegam pelo WiFi (~15 atualizações/s via Server-Sent Events), então fica fluido.

Se você não tem a TFT externa, abra o menu de ajustes (`` ` ``) e abaixe **PAINEL EXT** até **DESL**. O Cardputer então para de desenhar o escopo de cima e libera o buffer de ~86 KB.

Quer mexer na página sem gravar o firmware? Abra `web/index.html` direto do disco no navegador (ou acrescente `?demo` na URL) e ela roda com dados falsos.

---

## Gravar o binário pronto (mais fácil)

Não quer lidar com o PlatformIO? Baixe o `.bin` mais recente na [página de Releases](../../releases) e grave pelo navegador com o [ESP Web Flasher](https://esptool.spacehuhn.com/):

1. Conecte o Cardputer ADV por USB-C
2. Abra [esptool.spacehuhn.com](https://esptool.spacehuhn.com/)
3. Clique em **Connect** e selecione o dispositivo
4. Envie o `.bin` da última release no endereço `0x0`
5. Pronto

Na primeira inicialização ele busca redes, deixa você escolher uma e pede a senha. As credenciais ficam salvas no dispositivo, então ele não pergunta de novo.

---

## Compilar a partir do código

Requer o [PlatformIO](https://platformio.org/).

```sh
pio run -e cardputer-radar-csi -t upload
```

---

## Controles do teclado

| Tecla | Ação |
|-------|------|
| `c` | Calibrar: aprender a sala vazia (~13 s, fique parado ou se afaste) |
| `,` | Diminuir o limiar de detecção (passos de 5 %, salvo) |
| `/` | Aumentar o limiar de detecção (passos de 5 %, salvo) |
| `.` | Alternar o escopo de cima entre PPI e vista 3D |
| `q` | Mostrar o QR code / URL da vista no celular |
| `` ` `` | Abrir o menu de ajustes |

As telas do Cardputer usam texto sem acentos (a fonte embutida não os renderiza); a página do celular usa português completo.

---

## Estrutura do projeto

```
├── platformio.ini          Configuração de build
├── partitions.csv          Tabela de partições OTA dupla de 8 MB
├── include/
│   ├── radar_link.h        Parser de protocolo, estado, histórico de 240 amostras de movimento
│   ├── ext_panel.h         Driver do painel ILI9341 externo
│   └── web_view.h          Mini servidor HTTP + SSE não bloqueante para a vista no celular
├── web/index.html          Página da vista no celular (embutida no firmware na compilação)
└── src/main.cpp            Interface de radar com duas telas, callbacks CSI, tratamento de teclas
```

---

## Notas de desenvolvimento

- `ARDUINO_USB_CDC_ON_BOOT=1`: `Serial` é a porta USB-C.
- Plataforma: `espressif32@6.12.0`, placa `m5stack-stamps3`, framework Arduino.

---

## Roteiro

1. Estimativa de direção com duas antenas para blips direcionais no escopo do radar
2. Registro de eventos de presença com persistência opcional em cartão SD

---

## A parte assustadora

Quase nada pode ser feito para se proteger disso. O Wi-Fi atravessa paredes, pisos e praticamente qualquer outra coisa, porque esse é o objetivo do Wi-Fi. Essa tecnologia promete muito para coisas como detecção de queda de idosos e monitoramento hospitalar, mas as implicações para a privacidade são reais.

Fiz isto para mostrar que você não precisa de um contrato de US$ 60 bilhões com o governo. Basta US$ 50 e um pouco de dedicação para construir algo que faz o que parecia impossível.

Estamos de acordo com isso? Deixe um comentário no vídeo e me conte.

---

Melhorias e tradução para pt-BR: **Joaquim Ferreira** — [joaquimfsneto@gmail.com](mailto:joaquimfsneto@gmail.com)
