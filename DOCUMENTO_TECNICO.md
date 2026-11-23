# PotAI — Documento técnico do Trabalho Final

**Disciplina:** PSI3542 — Sistemas Embarcados para IoT  
**Aplicação:** sistema de irrigação automática com inferência no ESP32, histórico na AWS e monitoramento no smartphone  
**Equipe:** José Luis La Torre Romero — 18475730  
**Referência reutilizada:** firmware e infraestrutura AWS da Aula 11 (pacote A10/A11)  
**Base deste texto:** proposta inicial da equipe, enunciado `PSI3542_Trabalho_Final_Aplicacao_Referencia_v01_DRAFT` e o código atual do repositório

A estrutura segue o modelo mínimo da seção 7 do enunciado (aplicação, dispositivo, arquitetura, firmware e aplicativo, testes). As seções de nuvem e de modelo de machine learning detalham os componentes pedidos para o relatório.

---

## 1. Aplicação e requisitos

### 1.1 Problema

Vasos em ambiente interno são regados por hábito, sem medida da umidade do solo nem do volume aplicado. O PotAI mede o ambiente e o solo, estima quantos mililitros aplicar segundo o tipo de planta e aciona uma bomba de baixa tensão. O histórico fica na nuvem. O celular consulta esse histórico. A decisão de volume permanece no ESP32: se a rede cair, a bomba continua com a última leitura válida.

### 1.2 Funções oferecidas

| Função | Onde acontece |
|---|---|
| Medir temperatura, umidade relativa e umidade do solo | ESP32 (DHT11 + sonda ADC) |
| Mostrar as grandezas, a planta, a recomendação da IA e o estado da bomba | OLED local, três páginas |
| Estimar mililitros com um Random Forest embutido na flash | ESP32, sem round-trip à nuvem |
| Ligar a bomba pelo tempo correspondente ao volume, com teto de duração, teto de umidade e intervalo mínimo entre regas | Relé no ESP32 |
| Publicar telemetria e cada irrigação concluída | MQTT com certificado X.509 para o AWS IoT Core |
| Guardar leituras (7 dias) e irrigações (90 dias) | DynamoDB, via Lambda |
| Consultar estado atual, gráficos e lista de irrigações | App Flutter (Android, Windows, web, iOS) |

### 1.3 Requisitos verificáveis

Os identificadores R1–R4 vêm da proposta inicial. O texto da coluna “Como verificar” é o da demonstração pedida pelo enunciado (seção 8).

| ID | O dispositivo ou a aplicação deve | Como verificar |
|---|---|---|
| R1 | Coletar temperatura, umidade relativa e umidade do solo e apresentá-las no painel | Valores visíveis nas páginas 0 e 1 do OLED e presentes no snapshot do firmware |
| R2 | Usar o modelo para decidir o volume e comandar a bomba quando as travas de segurança permitem | Variar solo e temperatura, observar o log `[IA]` e o acionamento físico do relé |
| R3 | Publicar telemetria e eventos de irrigação por MQTT e manter histórico consultável | Nova mensagem em `psi3542/<thing>/telemetry` e `/irrigation` gravada no DynamoDB e recuperável pela app (mais de um registro) |
| R4 | Permitir o monitoramento remoto da planta | App Início e Histórico com a última leitura, a recomendação e as irrigações das últimas 24 h |

### 1.4 O que mudou em relação à proposta inicial

A proposta descrevia um LCD 16×2, botões de navegação **e** acionamento manual da bomba, e um comando de irrigação enviado pelo aplicativo. A implementação atual é outra:

| Proposta | Implementação atual |
|---|---|
| LCD 16×2 | OLED SSD1306 128×64 por I2C (3,3 V). O texto de cada página cabe em duas linhas de 16 caracteres |
| Botões para navegar e ligar a bomba na mão | Um botão (GPIO 33 a GND) só avança a página. A bomba é automática |
| Comando de irrigação manual pelo celular (R4 da proposta) | A app só lê. O `POST /devices/{thingName}/command` da API ainda existe e publica `{"led": true\|false}` no tópico de comando da Aula 11. A app Flutter não chama essa rota |
| “Consumo de água” medido | `ml_delivered` é estimado por tempo de bomba × vazão configurada (`A10_PUMP_FLOW_ML_PER_MIN`, padrão 1200 ml/min). Não há sensor de vazão |

A operação local que altera uma saída física é a chave de porta herdada da Aula 10 (GPIO 27, ativo em baixo): o flanco, depois do debounce, acende ou apaga o LED do GPIO 2. O comando remoto de LED segue o mesmo GPIO, pela fila de comandos.

### 1.5 Diferenças em relação à Aula 11

A Aula 11 entrega um supervisor ambiental: DHT11, LED, chave, telemetria MQTT, uma tabela DynamoDB e um cliente web que lê a última leitura e envia o comando do LED.

O PotAI conserva esse caminho (Wi-Fi, SNTP, MQTT/TLS, regra de telemetria, Lambda de ingestão, API Gateway, comando do LED) e acrescenta:

- sonda de solo no ADC1, relé da bomba, buzzer e OLED;
- modelo Random Forest na flash e tarefa de irrigação;
- tópico `psi3542/<thingName>/irrigation`, segunda regra IoT e tabela `PSI3542Irrigation`;
- campos PotAI na telemetria (`soil_moisture_pct`, `plant_id`, `ml_predicted`, `pump_on`, `irrigation_count`), opcionais na ingestão para não rejeitar um firmware antigo;
- app Flutter com três destinos (Início, Histórico, Ajustes), no lugar do cliente web estático como interface principal. O `web_client` da Aula 11 permanece na pasta de nuvem.

O Thing, o certificado e a política IoT do dispositivo continuam fora da stack CloudFormation e não devem ser apagados ao remover a stack da Aula 11. A política do dispositivo precisa de `iot:Publish` também em `psi3542/<thingName>/irrigation`. Sem isso a telemetria chega e as irrigações não.

---

## 2. Dispositivo e painel

Todas as saídas são de 3,3 V. O firmware recusa o boot se um GPIO cair em 6–11 (flash), se uma saída cair em 34–39, ou se duas funções compartilham o mesmo pino. O padrão abaixo sai de `idf.py menuconfig` (`main/Kconfig.projbuild`).

| Elemento | Grandeza ou função | Interface | GPIO padrão |
|---|---|---|---|
| DHT11 | Temperatura (°C) e umidade relativa (%) | 1-Wire, saída | 4 |
| Sonda de solo (AO) | Umidade do solo, 0–100 %, linear entre `raw_dry` (3200) e `raw_wet` (1400), média de 16 amostras | ADC1, 12 bits, somente entrada. ADC2 não é usado com Wi-Fi | 34 |
| Relé da bomba | Carga CC de baixa tensão. Ativo em baixo. Desliga sozinho por `esp_timer`, com teto `A10_PUMP_MAX_ON_MS` (30 s) | Saída | 26 |
| Buzzer | Dois bipes ao ligar a bomba (`A10_PUMP_BEEP_COUNT`) | Saída | 25 |
| OLED SSD1306 128×64 | Painel. Endereço I2C 0x3C, 400 kHz. Se o OLED falhar no boot, o resto do firmware segue | I2C, SDA / SCL | 21 / 22 |
| Botão de página | Avança entre as três páginas. Pull-up interno, debounce 60 ms | Entrada a GND | 33 |
| Chave de porta | Operação local herdada: porta aberta acende o LED | Entrada, pull-up, ativo em baixo, interrupção em qualquer borda | 27 |
| LED | Indicador local e alvo do comando de nuvem `{"led": true\|false}` | Saída | 2 |

Leitura de solo com valor cru fora de 100–4090 é marcada inválida. Sem DHT11 válido e solo válido, ambos mais novos que `3 × período do sensor + 1 s` (período padrão 3 s, logo cerca de 10 s), a tarefa de irrigação não liga a bomba.

### Páginas do OLED

O botão percorre as páginas. Cada uma mostra duas linhas:

| Página | Linha 1 | Linha 2 |
|---|---|---|
| 0 — ambiente | `Temp: 25.0 C` (ou `--` se o DHT11 falhou) | `Hum.Amb: 58 %` |
| 1 — solo e planta | `Hum.Suelo: 28 %` | Nome comum da planta configurada (id 1–8) |
| 2 — decisão | `IA: 120 ml` | `Bomba:OFF Rieg:3` |

Há também um HTTP local na LAN, porta 80, só com Wi-Fi: `GET /health`, `GET /api/v1/status`, `GET /api/v1/telemetry/latest`. A app PotAI não usa essas rotas. Servem para depuração ao lado do dispositivo.

---

## 3. Arquitetura fim a fim

A app não fala com o ESP32. O ESP32 publica por MQTT. A app lê por HTTPS. A nuvem não calcula mililitros.

```text
 Sensores                         Wi-Fi + MQTT/TLS (certificado X.509)
 DHT11  ──┐
 solo   ──┼──► ESP32 ── inferência RF ── bomba / OLED / LED
 porta  ──┘         │
                    │  psi3542/<thing>/telemetry | irrigation | event | health
                    ▼
              AWS IoT Core
                    │  regra SELECT * + thingName, ingested_at_ms, trace
                    ▼
              Lambda ingest ──► DynamoDB
                                  PSI3542Telemetry     (TTL 7 dias)
                                  PSI3542Irrigation    (TTL 90 dias)
                                        ▲
                                        │ Query
              App PotAI ──HTTPS──► API Gateway ──► Lambda api
              (Flutter)                  │
                                         └── iot:Publish ──► psi3542/<thing>/command
                                              corpo {"led": true|false}, QoS 1
```

Três modos de dados, com o mesmo contrato HTTP:

| Modo | Quem responde | Origem dos dados |
|---|---|---|
| Demo | `DemoPotaiApi` dentro da app | Séries sintéticas, sem rede |
| API local | `02_cloud_aws/local_server.py`, porta 8080 | DynamoDB em memória. Não recebe o MQTT do ESP32 |
| AWS | API Gateway + Lambda | Telemetria e irrigações reais do dispositivo |

O identificador é único: `thingName` do IoT Core, `client_id` MQTT e `device_id` do JSON são o mesmo valor (`A10_THING_NAME`, padrão `psi3542-g01`). Tem de começar por `psi3542-`.

O id da planta (1–8) é a coluna `localname` do dataset. O mesmo número tem de coincidir em `03_model_ML/train.py`, no menuconfig (`A10_PLANT_ID`), em `02_cloud_aws/lambda_api/plants.py` e em `04_app_potai/lib/models.dart`.

### Tópicos MQTT

Prefixo fixo `psi3542/<thingName>/`.

| Tópico | Sentido | QoS padrão | Conteúdo |
|---|---|---|---|
| `telemetry` | dispositivo → nuvem | 0 | Leitura periódica |
| `irrigation` | dispositivo → nuvem | o da fila de publicação | Um mensagem ao **fechar** cada irrigação |
| `event` | dispositivo → nuvem | 1 | `door_open`, `door_closed`, `sensor_unavailable`, `sensor_recovered`, `pump_on`, `pump_off` |
| `health` | dispositivo → nuvem | 0 | Heap, motivo de reset, contadores. Período 15 s |
| `command` | nuvem → dispositivo | 1 | `{"led": true\|false}` |

O horário de gravação (`ingested_at_ms`, carimbo da regra IoT) é distinto do instante de aquisição (`sample_uptime_ms` / `pump_uptime_ms`, milissegundos desde o boot do ESP32). A app mostra o horário de ingestão.

### Cadeia até a tela Início

```text
sensor_task (3 s)
  → device_state
  → fila de publicação
  → mqtt_task publica psi3542/<thing>/telemetry
  → regra IoT acrescenta thingName, ingested_at_ms, message_trace_id
  → Lambda ingest valida e faz PutItem
  → app, a cada 5 s, GET /devices/{thing}/dashboard
  → online = última telemetria com idade ≤ 30 s
```

### Cadeia da irrigação

```text
irrigation_task (10 s)
  → predição na flash
  → se passa volume mínimo, teto de solo e cooldown: pump_start(duração)
  → ao desligar, publica pump_off e enfileira o registro
  → mqtt_task publica psi3542/<thing>/irrigation antes da telemetria pendente
  → regra com record_type = irrigation
  → tabela PSI3542Irrigation
  → GET /irrigations e o bloco irrigation do dashboard
```

`POST /command` responde HTTP 202 quando o IoT Core aceita a publicação. Isso não confirma que o LED mudou. A confirmação física é o GPIO e o log `[CLOUD][COMMAND]`.

### Pastas

```text
PROJETO/
├── 01_firmware_esp32/    ESP-IDF, FreeRTOS, projeto da Aula 10 estendido
├── 02_cloud_aws/         Lambdas, CloudFormation, servidor local, testes
├── 03_model_ML/          Treino e exportação do bosque para binário
├── 04_app_potai/         Cliente Flutter
└── DOCUMENTO_TECNICO.md  Este documento
```

---

## 4. Firmware (ESP32)

ESP-IDF e FreeRTOS. Entrada em `main/app_main.c`. Wi-Fi, pinos, planta, períodos e QoS saem do menuconfig. A bomba é inicializada desligada antes das tarefas. O Wi-Fi espera 8 s (`A10_LOCAL_BASELINE_MS`) para o painel local aparecer antes da associação.

Sem SSID, sem endpoint ATS válido ou sem certificado, o MQTT fica desligado naquele boot. Sensores, irrigação e HTTP local (se houver IP) continuam. Falha do modelo, do ADC de solo ou do OLED no arranque não derruba o restante.

### 4.1 Módulos

| Módulo | Função |
|---|---|
| `connectivity`, `wifi_adapter` | Associação Wi-Fi e estado `ONLINE` |
| `time_sync` | SNTP (`pool.ntp.org`). O MQTT não sobe sem hora |
| `mqtt_adapter`, `mqtt_service` | Cliente MQTT sobre TLS e os tópicos `psi3542/<thing>/<kind>` |
| `aws_iot_credentials` | Certificado e chave do dispositivo. Não entram no repositório |
| `dht11` | Temperatura e umidade relativa |
| `soil_moisture` | Percentual linear a partir do ADC |
| `device_state` | Snapshot compartilhado: leituras, bomba, última predição, contador de irrigações |
| `ml_model` | Random Forest na flash (`model/rf_model.bin`), inferência no lugar, sem cópia para a RAM |
| `irrigation` | Laço de decisão, temporização e registro |
| `pump` | Relé com desligamento por timer |
| `buzzer` | Aviso ao ligar a bomba |
| `oled_ssd1306`, `display_ui` | Painel e páginas |
| `event_logic` | Debounce da porta e eventos `door_open` / `door_closed` |
| `cloud_command` | Comando `{"led": true\|false}` |
| `telemetry` | JSON de telemetria e de irrigação |
| `health` | Snapshot de saúde (`a10-v07`) |
| `http_service` | API HTTP da LAN |

### 4.2 Tarefas

Não há uma tarefa por módulo. Aquisição, decisão, painel e comunicação são contextos separados.

| Tarefa | Núcleo | Prioridade | O que faz |
|---|---|---|---|
| `event_task` | 0 | 10 | Debounce da porta e eventos de abertura/fechamento |
| `sensor_task` | 1 | 5 | A cada 3 s: DHT11 + solo. Enfileira telemetria se o DHT11 é válido |
| `irrigation_task` | 1 | 4 | A cada 10 s: inferência e, se couber, bomba |
| `command_task` | 0 | 5 | Aplica o LED pedido pela nuvem |
| `mqtt_task` | 0 | 4 | Sobe SNTP e MQTT; publica saúde, irrigações, telemetria e eventos |
| `http_manager_task` | qualquer | 4 | Sobe ou desce o HTTP local conforme `CONNECTIVITY_ONLINE` |
| `ui_task` | qualquer | 3 | Atualiza o OLED a cada 500 ms; o botão avança a página |

A cadeia de conectividade, dentro de `mqtt_task`, é:

```text
Wi-Fi ONLINE → SNTP SYNCED → MQTT/TLS (X.509) ONLINE → SUBSCRIBE do tópico command
```

### 4.3 Filas, ISR e temporizadores

| Mecanismo | Uso |
|---|---|
| ISR de GPIO na porta | Qualquer borda acorda `event_task`. O nível só é aceito depois de 50 ms de silêncio |
| Fila de publicação (profundidade 6) | Telemetria e eventos. Se enche, descarta o item novo |
| Fila de comandos (profundidade 4) | LED. Se enche, descarta o comando novo |
| Fila de irrigações (profundidade 8) | Se enche, descarta a mais antiga e entra a nova |
| `esp_timer` da bomba | Desliga o relé ao fim da duração, mesmo que a tarefa de irrigação esteja ocupada |
| Mutex / cópia de `device_state` | As tarefas leem um snapshot. Não há seção crítica longa no laço de decisão |

Um `publish` que falha depois de tirar o item da fila não o recoloca.

### 4.4 Regra de irrigação

A cada período, e só se `ml_model_init` validou o binário:

1. Exige DHT11 válido, solo válido e ambas as amostras dentro da idade máxima.
2. Prediz mililitros: `ml_model_predict_ml(plant_id, solo_%, temperatura_°C)`.
3. Não rega se a predição é menor que 10 ml (`A10_IRRIGATION_MIN_ML`).
4. Não rega se o solo já está em ou acima de 60 % (`A10_IRRIGATION_SOIL_MAX_PCT`), mesmo que o modelo peça água.
5. Respeita 600 s de cooldown desde o fim da irrigação anterior.
6. Duração = `ml × 60 000 / vazão_ml_por_min`, cortada em 30 s.
7. Ao terminar, publica `pump_off` e enfileira o registro: ml preditos, ml estimados, duração, solo e temperatura de **antes** de regar.

Medição inválida não liga a bomba e não conserva um estado anterior de “deve regar”. A saída só existe enquanto o timer daquela irrigação está armado. Com `A10_IRRIGATION_ENABLED` desligado, o firmware mede e mostra, e a bomba nunca arma.

### 4.5 Mensagens

Telemetria:

```json
{
  "device_id": "psi3542-g01",
  "seq": 1,
  "temperature_c": 25.0,
  "humidity_pct": 58.0,
  "valid": true,
  "sample_uptime_ms": 1234,
  "soil_moisture_pct": 28.0,
  "soil_valid": true,
  "plant_id": 4,
  "ml_predicted": 120.0,
  "pump_on": false,
  "irrigation_count": 3
}
```

Irrigação (um registro por ciclo concluído):

```json
{
  "device_id": "psi3542-g01",
  "seq": 1,
  "plant_id": 4,
  "soil_moisture_pct": 28.0,
  "temperature_c": 25.0,
  "ml_predicted": 120.0,
  "ml_delivered": 118.0,
  "duration_ms": 7000,
  "pump_uptime_ms": 123456
}
```

---

## 5. Nuvem AWS

Não há servidor permanente. Duas funções Lambda sobem quando chega tráfego. A stack inteira está em `02_cloud_aws/infra/cloudformation.yaml`. O código é empacotado por `build_backend_zip.py` (`dist/ingest.zip`, `dist/api.zip`) e o CloudFormation o lê de um bucket S3. Runtime Python 3.12, 128 MB, timeout 10 s. Logs no CloudWatch com retenção de 7 dias, removidos com a stack.

```text
psi3542/+/telemetry   ──regra──► Lambda ingest ──► PSI3542Telemetry
psi3542/+/irrigation  ──regra──► Lambda ingest ──► PSI3542Irrigation
App ──HTTPS──► API Gateway ──► Lambda api ──Query──► as duas tabelas
Lambda api ──iot:Publish──► psi3542/<thing>/command
```

### 5.1 Regras IoT

As duas fazem `SELECT *` e acrescentam `thingName = topic(2)`, `ingested_at_ms = timestamp()` e `message_trace_id = traceid()`. A regra de irrigação acrescenta `record_type = 'irrigation'`. Sem `record_type`, a Lambda trata a mensagem como telemetria.

### 5.2 DynamoDB

Pagamento por pedido. Chave de partição `thingName` (string), chave de ordenação `ingestionKey` (string):

```text
{ingested_at_ms em 13 dígitos}#{seq em 10 dígitos}#{message_trace_id}
```

A consulta descendente devolve o mais novo primeiro. Um reenvio com o mesmo trace não duplica a linha (`attribute_not_exists(ingestionKey)`).

| Tabela | TTL `expires_at` | Conteúdo |
|---|---|---|
| `PSI3542Telemetry` | 7 dias | Leituras |
| `PSI3542Irrigation` | 90 dias | Irrigações fechadas |

A ingestão rejeita e não grava: `thingName` que não comece por `psi3542-`, `device_id` diferente do thing, `valid` que não seja booleano, número não finito, umidade fora de 0–100, e qualquer `record_type` desconhecido.

### 5.3 API HTTP

Sem autenticação (`AuthorizationType: NONE`). Serve à prática. CORS limitado à origem configurada na stack.

| Método e rota | Resposta |
|---|---|
| `GET /plants` | Catálogo (nome científico, dias entre regas, horas de luz, umidade ótima) |
| `GET /devices/{thingName}/dashboard` | Última leitura, planta, última irrigação, quantidade e ml das últimas 24 h, flag `online` |
| `GET /devices/{thingName}/telemetry/latest` | Última leitura. 404 `telemetry_not_found` se não há linhas |
| `GET /devices/{thingName}/telemetry/history?limit=` | Pontos, máximo 200, padrão 60, mais novo primeiro |
| `GET /devices/{thingName}/irrigations?limit=` | Irrigações, máximo 100, padrão 30 |
| `POST /devices/{thingName}/command` | Corpo exato `{"led": true\|false}`. 202 se o IoT Core aceita o publish |

`online` é verdadeiro se a última telemetria tem no máximo 30 s. Com o período de 3 s, 200 pontos cobrem cerca de 10 minutos. Para ver horas no gráfico é preciso aumentar o período do sensor no menuconfig. `thingName` inválido devolve 400 `invalid_thing_name`. Erro interno devolve 500 `internal_error`, sem stack trace ao cliente.

O catálogo de cuidados (dias, luz, umidade ótima) vive em `plants.py`. O modelo não usa esses campos: só o id, a umidade do solo e a temperatura.

### 5.4 Servidor local

`python local_server.py` (ou `docker compose up --build`) carrega as mesmas Lambdas contra um DynamoDB em memória e um publicador IoT falso. Serve para a app e o telefone. Não substitui o caminho MQTT do ESP32.

| Cliente | URL | Thing |
|---|---|---|
| PC / Chrome | `http://127.0.0.1:8080` | `psi3542-g01` |
| Emulador Android | `http://10.0.2.2:8080` | `psi3542-g01` |
| Telefone na mesma Wi-Fi | `http://<IPv4 do PC>:8080` | `psi3542-g01` |

---

## 6. Aplicativo móvel

Flutter (SDK ^3.11), pacote `potai`, versão 1.0.0. Dependências de produto: `http`, `shared_preferences`, `fl_chart`. A app não abre MQTT, não usa o SDK da AWS e não envia o comando do LED.

```text
main.dart          MaterialApp e navegação de três destinos
controller.dart    PotaiController: ajustes, sondagem a cada 5 s, ChangeNotifier
api.dart           PotaiApi = HttpPotaiApi | DemoPotaiApi
models.dart        Dashboard, LatestReading, HistoryPoint, IrrigationRecord, PlantInfo
screens/           home_screen, history_screen, settings_screen
```

O controlador grava em `SharedPreferences` a URL base, o thing e o modo demo. Fora do demo, `HttpPotaiApi` usa timeout de 8 s. Cada ciclo pede `dashboard`, `telemetry/history` (100 pontos) e `irrigations` (30).

| Tela | O que mostra |
|---|---|
| Início | Planta (nome e emoji), umidade do solo com faixa seco / boa / úmida (limites 30 % e 65 %, só de apresentação), temperatura, umidade do ar, recomendação da IA em ml, última irrigação, ml e quantidade nas últimas 24 h, ficha de cuidado da espécie, indicador online |
| Histórico | Gráfico alternável de solo, umidade ambiente e temperatura, mais a lista de irrigações (ml preditos, ml entregues, duração, solo e temperatura anteriores) |
| Ajustes | URL, Thing Name, modo demo. A URL tem de ser `http://` ou `https://`. O thing tem de casar com `psi3542-...` |

O modo demo (`Ver com dados de exemplo`) dispensa backend e ESP32.

---

## 7. Modelo de machine learning

O treino ocorre fora do dispositivo. O ESP32 só executa o bosque já exportado. O objetivo é regressão: dadas a espécie, a umidade do solo e a temperatura, estimar `mililitros_riego`.

### 7.1 Dados

Arquivo `03_model_ML/dataset_sintetico_plantas_numerico.csv`: 2400 linhas, sintético e em boa parte gerado por regras. Partição 80/20 (`random_state=42`), estratificada por `localname`. Conjunto de teste: 480 linhas.

| Entrada | Significado |
|---|---|
| `localname` | Id da planta, inteiro 1–8 |
| `moisture` | Umidade do solo, % |
| `temperature` | Temperatura, °C |

Alvo: `mililitros_riego`.

| id | Nome | Nome científico |
|---|---|---|
| 1 | ajo | *Allium sativum* |
| 2 | geranio | *Pelargonium* |
| 3 | hierbabuena | *Mentha spicata* |
| 4 | menta | *Mentha* |
| 5 | orégano | *Origanum vulgare* |
| 6 | orquídea | *Orchidaceae* |
| 7 | rosa china | *Hibiscus rosa-sinensis* |
| 8 | tomate cherry | *Solanum lycopersicum* var. cerasiforme |

### 7.2 Candidatos e métricas

`train.py` treina três pipelines (one-hot do id + `StandardScaler` de umidade e temperatura) e grava `model_comparison.csv`. Números abaixo são os do arquivo presente no repositório, no conjunto de teste sintético. Não descrevem um vaso real.

| Modelo | MAE (ml) | R² | Inferência de 1 amostra | Estimador | Unidades | Papel |
|---|---|---|---|---|---|---|
| Ridge (α = 1) | 26,97 | 0,768 | 8,1 ms no PC | 0,62 KiB | 11 coeficientes | Linha de base linear, pequena |
| Random Forest (100 árvores) | 0,004 | 1,000 | 28,5 ms no PC | 95 KiB (pickle) | 900 nós | O que é exportado ao ESP32 |
| HistGradientBoosting | ~0 | 1,000 | 14,4 ms no PC | 187 KiB | 2656 nós | Referência de precisão, maior |

O R² próximo de 1 nos modelos de árvore é esperado: o alvo foi gerado por regras, e uma árvore reproduz essas regras. Em medições de um vaso real a folga entre Ridge e floresta deve diminuir. O pickle do scikit-learn não é gravado na flash. `estimator_kb` e o número de nós são só um custo relativo no PC.

A floresta foi a escolhida para o bordo: erro desprezível neste dataset e exportação direta para um percurso de nós, sem o pré-processamento do pipeline.

### 7.3 Exportação para o ESP32

`export_model_esp32.py` converte `model_random_forest.pkl` em `01_firmware_esp32/components/ml_model/model/rf_model.bin`:

1. Descarta o pré-processamento. Cada limiar volta ao domínio original (planta, % de solo, °C). O ESP32 não faz one-hot nem padronização.
2. Quantiza entradas em décimos (×10, int16) e folhas em Q8.8 (`ml = payload / 256`, até 255,99 ml).
3. Cada nó ocupa 4 bytes, percorrido em pré-ordem. O filho esquerdo é o nó seguinte. Só se guarda o índice do direito.
4. Confere o binário contra o pipeline original antes de o escrever.

Cabeçalho little-endian: magic `A10M`, versão 1, número de árvores, número de nós, `leaf_scale`, `input_scale`, CRC16-CCITT do corpo. Tipos de nó: 0 umidade, 1 temperatura, 2 igualdade de planta, 3 folha.

`ml_model_init` valida magic, versão, tamanho e CRC. Se falha, a irrigação automática fica desligada e o log pede para rodar de novo o script de exportação. A inferência lê o binário na flash, sem cópia para a RAM.

`--n-trees` no script de exportação permite gravar um subconjunto das árvores quando o binário completo não cabe.

---

## 8. Testes, resultados e limitações

O enunciado pede resultados efetivamente obtidos. Este repositório não contém o registro de uma demonstração física já realizada (fotos, logs de bancada ou captura da app contra a AWS real). A tabela abaixo separa o que o código já cobre do que ainda precisa ser anotado na apresentação. Uma linha só pode ser dada como verificada depois da evidência correspondente.

### 8.1 Verificação mínima da demonstração

| Teste do enunciado | Requisito | O que o código prepara | Evidência a registrar na apresentação |
|---|---|---|---|
| Aquisição e painel | R1 | `sensor_task` lê DHT11 e solo; páginas 0 e 1 do OLED mostram T, UR e solo, ou `--` se a amostra é inválida | Foto ou vídeo do OLED com os três valores, e o log `[LOCAL]` correspondente |
| Atuação | R2 | `irrigation_task` prediz, aplica as travas e arma o relé por `esp_timer`. Buzzer anuncia o início | Solo seco abaixo do teto: log `[IA] ... -> N ml` e relé ligado pelo tempo calculado. Solo ≥ 60 % ou leitura inválida: bomba permanece desligada |
| Operação local | painel | Botão GPIO 33 troca de página. Chave GPIO 27 acende o LED | Botão percorre as três páginas. Abrir a chave muda o LED |
| Integração e histórico | R3 | Publicação MQTT, duas regras, duas tabelas, `GET /history` e `GET /irrigations` | Pelo menos duas linhas recuperadas na tela Histórico, com horário de ingestão |
| Monitoramento remoto | R4 | App consulta `dashboard` a cada 5 s e marca online se a leitura tem ≤ 30 s | Tela Início com planta, solo, temperatura, ml e estado online |
| Comando remoto | núcleo da seção 2 do enunciado | A API aceita `POST /command` e a `command_task` escreve o LED. A app Flutter não expõe o botão | Ver limitação L1. Se for demonstrado, usar a rota HTTP e distinguir o 202 da mudança física do GPIO |

### 8.2 Testes automatizados já presentes no repositório

Estes testes não substituem a bancada. Cobrem o contrato, com DynamoDB e IoT simulados em memória.

| Alvo | Onde | O que exercita |
|---|---|---|
| Ingestão e API | `02_cloud_aws/tests/test_potai_backend.py` | Validação de campos, gravação de telemetria e irrigação, dashboard, histórico, comando |
| Servidor local | `02_cloud_aws/tests/test_local_server.py` | O mesmo contrato HTTP sem AWS |
| Widget da app | `04_app_potai/test/widget_test.dart` | Arranque da interface Flutter |

Comando local do backend, a partir de `02_cloud_aws`: `python -m unittest discover -s tests -v`. Comando da app: `flutter test` em `04_app_potai`.

### 8.3 Limitações conhecidas

| ID | Limitação |
|---|---|
| L1 | A app não emite comando nem ajuste de parâmetro. O enunciado (seção 2) pede pelo menos um. O contrato HTTP do LED existe; a tela não o usa. A irrigação manual pedida na proposta também não está na app nem no botão do OLED |
| L2 | O dataset é sintético. As métricas de `model_comparison.csv` não medem um vaso real |
| L3 | `ml_delivered` é tempo × vazão de menuconfig. Sem calibração da bomba real, o “consumo” diverge do volume no vaso |
| L4 | A API não autentica o chamador (`AuthorizationType: NONE`). Quem tem a URL lê os dados e pode publicar o comando do LED. Adequado à prática, inadequado à exposição na internet |
| L5 | Certificados, chave privada, endpoint e URL real da API não estão no repositório, de propósito |
| L6 | `local_server.py` não recebe o MQTT do ESP32. Ligar a app nele não prova o caminho do dispositivo |
| L7 | Saúde e eventos sobem por MQTT. A app PotAI não os consulta. A ingestão atual persiste telemetria e irrigações |
| L8 | Com período de 3 s, o histórico da API (teto de 200 pontos) cobre cerca de 10 minutos |
| L9 | O volume máximo de uma folha quantizada é 255,99 ml. Predições acima disso saturam |

### 8.4 Energia e segurança — melhorias futuras

A Aula 12 é optativa. O firmware não implementa novos modos de baixo consumo. O que já existe, e o que ficaria para uma versão seguinte:

**Já presente**

- Autenticação do dispositivo no IoT Core por certificado X.509 e MQTT sobre TLS. O MQTT não sobe sem hora sincronizada.
- Relé com desligamento por hardware timer e teto de 30 s por ciclo. A bomba nasce desligada.
- Teto de umidade do solo e cooldown, para não repetir a rega antes de o sensor reagir.
- Leitura inválida não aciona a bomba.
- Credenciais fora do repositório. Thing e política IoT fora da stack que se apaga no fim da disciplina.
- Rejeição na ingestão de `device_id` divergente, umidade fora da faixa e números não finitos.

**Ainda em aberto**

- Autenticação da API (IAM, Cognito ou uma chave por equipe) e CORS restrito à origem real da app.
- A política IoT do dispositivo deve continuar só com os tópicos `psi3542/<thingName>/*` dela, sem `iot:*` amplo.
- Deep sleep entre amostras, desligando Wi-Fi e OLED, é incompatível com o MQTT sempre conectado desta PoC. Um desenho posterior publicaria, dormiria e acordaria no período do sensor (30–60 s), com a bomba concluída antes do sono.
- Calibrar `raw_dry`, `raw_wet` e a vazão no hardware real, e trocar o dataset sintético por medições desse vaso antes de tratar o R² atual como desempenho de campo.
- Expor na app um comando cujo efeito físico seja observável (LED já suportado pela API, ou uma irrigação manual com as mesmas travas de duração), para fechar L1 e o item “comando remoto” da demonstração.

---

## Referências

- Empresa Brasileira de Pesquisa Agropecuária. Perguntas e respostas. Embrapa. Água na agricultura. <https://www.embrapa.br/en/agua-na-agricultura/perguntas-e-respostas>
- Santos, T. L., Rocha, L. O., Melo Neto, R. R., Faccioli, G. G., de Sousa, I. F., & Brito, M. E. B. (2026). Low-cost sensors and microcontrollers as irrigation management tools. *Revista Brasileira de Engenharia Agrícola e Ambiental, 30*(1), e284824. <https://doi.org/10.1590/1807-1929/agriambi.v30n1e284824>
- Togneri, R., dos Santos, D. F., Camponogara, G., Nagano, H., Custódio, G., Prati, R., Fernandes, S., & Kamienski, C. (2022). Soil moisture forecast for smart irrigation: The primetime for machine learning. *Expert Systems with Applications, 207*, 117653. <https://doi.org/10.1016/j.eswa.2022.117653>
- PSI3542. Trabalho Final — Aplicação de referência, v01 DRAFT (outubro de 2026). Núcleo funcional, entregas e modelo do documento técnico.
- Material de aula reutilizado: firmware ESP-IDF A10 v07 e pacote de nuvem A11 (IoT Core, Lambda, DynamoDB, API Gateway).
