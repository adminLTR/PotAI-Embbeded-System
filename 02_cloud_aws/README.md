# PSI3542 — Aula 11 — Software v04.14 DRAFT

Pacote de referência para a prática **Aplicação IoT fim a fim: ESP32, AWS IoT Core, Lambda, DynamoDB, API Gateway e cliente Web**.

## Operação adotada na Aula 11 v04.14

A rota do aluno passa a ser **Console-first**:

- nenhuma instalação local da AWS CLI;
- nenhum `aws configure` no computador compartilhado;
- CloudFormation criado pelo Console AWS mediante upload do template;
- AWS CloudShell somente para comandos pontuais, quando necessário;
- GET da API pode ser testado diretamente no navegador;
- cliente Web compartilhado recebe `API Base URL` e `Thing Name` pela própria interface;
- não é necessário editar `config.js` por grupo.

Consulte `student/ROTA_CONSOLE_CLOUDSHELL.md`.

## Contratos congelados

- Telemetria: `psi3542/<thingName>/telemetry`, QoS 0.
- Comando: `psi3542/<thingName>/command`, QoS 1, payload `{"led":true|false}`.
- IoT Rule: acrescenta `thingName=topic(2)`, `ingested_at_ms=timestamp()` e `message_trace_id=traceid()`.
- DynamoDB: tabela `PSI3542Telemetry`, PK `thingName`, SK `ingestionKey`.
- GET: `/devices/{thingName}/telemetry/latest`.
- POST: `/devices/{thingName}/command`.
- POST aceito: HTTP 202. Isso não confirma execução física no ESP32.
- Cliente: Web responsivo via HTTPS; sem MQTT direto, AWS SDK ou Cognito.

## Estrutura

```text
lambda_ingest/   Lambda acionada pela IoT Rule
lambda_api/      backend HTTP + publicação C2D
infra/           CloudFormation auto-contido
web_client/      HTML/CSS/JS estático compartilhável
student/         rota Console + CloudShell
qa/              eventos, testes de contrato e plano T1–T12
tests/           testes unitários locais de QA
professor/       provisionamento Console-first/CloudShell
```

## Validação local — somente desenvolvimento/QA

```text
python -m py_compile lambda_ingest/lambda_function.py lambda_api/lambda_function.py
pytest -q tests
python qa/check_contracts.py
node --check web_client/app.js
```

Esses comandos não fazem parte da rota prática do aluno.

## Limitações intencionais

- A API não autentica o usuário (`AuthorizationType: NONE`).
- O cliente Web precisa ser hospedado em uma origem HTTPS fixa durante a atividade, para que essa origem possa ser configurada explicitamente no CORS da HTTP API.
- O pacote não contém certificado, private key, endpoint real, URL real da API ou outros segredos.
- O firmware ESP32 A10 v07 DRAFT agora está incluído integralmente em `../01_firmware_esp32/`; as credenciais privadas do aluno permanecem locais. Gates experimentais ainda pendentes.
- A promoção para CANDIDATO exige QA em AWS real e ESP32 real.

## Regras da versão v04.2 — conta AWS individual

- Uma stack A11 por aluno, na sua própria conta AWS e na Região da Aula 10.
- Lambda de ingestão rejeita campos `valid` não booleanos, números inválidos/não finitos, `device_id` divergente de `thingName` e umidade fora de 0–100%.
- CloudFormation contém o código da Lambda sincronizado com `lambda_ingest/lambda_function.py`.
- CloudFormation cria dois CloudWatch LogGroups com retenção de 7 dias e remoção junto da stack.
- Cliente Web usa `sessionStorage`, mostra `valid` e oferece botão para limpar a configuração da aba.
- O professor ainda deve fornecer um cliente hospedado por HTTPS; a API usa `AuthorizationType: NONE` (exemplo didático de exposição temporária).
- Recursos A10 (Thing, certificados e IoT Policies) NÃO fazem parte da stack A11 e NÃO devem ser excluídos na limpeza A11.

## Rastreabilidade da versão
Esta distribuição está sincronizada em v04.14 DRAFT, sem mudanças funcionais no software cloud anteriormente identificado como v02.2 DRAFT. O registro anterior de mudanças foi arquivado em `99_controle_qualidade/historico/` do pacote Professor.

**Nota de pasta:** este README descreve a subpasta `02_cloud_aws`; o projeto ESP-IDF está em `../01_firmware_esp32/`. Os comandos locais de QA acima devem ser executados nesta subpasta cloud, apenas pelo professor.
