# PSI3542 A11 — Rota prática do aluno: AWS Console + CloudShell

## Regra operacional

A rota principal **não exige instalação local da AWS CLI** e não exige configurar `aws configure` no computador do laboratório.

Use:

- **AWS Management Console** para navegar pelos serviços, criar/inspecionar a stack e observar logs/dados;
- **AWS CloudShell** somente quando um comando curto for mais simples que vários passos no Console;
- **ESP-IDF Terminal do VS Code** somente para o firmware ESP32;
- **navegador** para testar o GET e para usar o cliente Web.

## Etapa 0 — sua conta AWS da Aula 10 e Região

Entre na **sua conta AWS pessoal usada na Aula 10**, sem compartilhar credenciais. Confirme a conta no Console e selecione a **mesma Região** utilizada pelo ESP32. Cada aluno criará **uma stack `psi3542-a11` na própria conta**.

O Thing, certificado, IoT Policy e endpoint da Aula 10 devem continuar sendo utilizados; não crie um Thing novo para a A11.

## Etapa 1 — obter o endpoint IoT pelo Console

1. Abra **AWS IoT Core**.
2. Abra **Settings / Configurações**.
3. Localize o **Device data endpoint** da conta/região.
4. Copie o hostname e acrescente `https://` quando preencher o parâmetro `IotDataEndpoint` do CloudFormation.

Alternativa no CloudShell, sem instalar nada localmente:

```bash
aws iot describe-endpoint --endpoint-type iot:Data-ATS
```

## Etapa 2 — criar a infraestrutura pelo CloudFormation Console

1. Abra **CloudFormation**.
2. Selecione **Create stack / Criar pilha** → **With new resources (standard)**.
3. Em template, escolha **Upload a template file**.
4. Carregue `infra/cloudformation.yaml`.
5. Avance.
6. Nome da stack: `psi3542-a11`.
7. Parâmetro `IotDataEndpoint`: `https://<endpoint-copiado-do-IoT-Core>`.
8. Parâmetro `ClientOrigin`: use a origem HTTPS do cliente Web fornecida no Moodle/roteiro.
9. Mantenha os demais valores padrão.
10. Na etapa final, reconheça que o template cria recursos IAM. Se a conta não permitir criar IAM roles, consulte o professor; não utilize access keys locais nem mude para root.
11. Crie a stack e aguarde `CREATE_COMPLETE`.

## Etapa 3 — obter a URL da API

Na stack `psi3542-a11` **da sua conta**:

1. abra **Outputs / Saídas**;
2. copie `ApiBaseUrl`.

Não é necessário executar AWS CLI.

## Etapa 4 — verificar a Lambda de ingestão

Use o Console:

1. **Lambda** → `psi3542-a11-ingest`;
2. **Monitor** → logs no CloudWatch;
3. correlacione `seq` com a amostra observada no ESP32.

## Etapa 5 — verificar o DynamoDB

Use o Console:

1. **DynamoDB** → Tables → `PSI3542Telemetry`;
2. Explore table items;
3. procure o `thingName` e o `seq` observados.

## Etapa 6 — testar GET sem terminal local

Opção mais simples: cole no navegador:

```text
<API_BASE_URL>/devices/<THING_NAME>/telemetry/latest
```

A resposta deve ser JSON.

Alternativa no CloudShell:

```bash
curl -s "<API_BASE_URL>/devices/<THING_NAME>/telemetry/latest"
```

## Etapa 7 — cliente Web

Abra a URL HTTPS do cliente fornecida pela disciplina.

Preencha:

- `API Base URL`: valor `ApiBaseUrl` obtido nos Outputs da stack;
- `Thing Name`: o Thing Name usado na Aula 10.

Pressione **Atualizar telemetria**.

## Etapa 8 — comando Cloud-to-Device

Mantenha `idf.py monitor` aberto no **ESP-IDF Terminal do VS Code**.

No cliente Web:

1. pressione **LIGAR LED**;
2. observe a mensagem “aceito pelo backend”;
3. observe o log do ESP32;
4. observe o LED físico;
5. repita com **DESLIGAR LED**.

HTTP 202/“aceito pelo backend” não é confirmação de execução física no ESP32.

## O que não deve ser feito na rota principal

- **NÃO instalar AWS CLI no Windows**;
- **NÃO executar `aws configure`**;
- armazenar access key/secret key no computador do laboratório;
- executar CloudFormation a partir de PowerShell local;
- editar `config.js` do cliente para cada grupo;
- instalar Node.js ou Python para operar a infraestrutura AWS.

## Encerramento — custo, conta e computador compartilhado

1. No cliente Web, clique em **Limpar configuração desta aba**. Feche a aba e encerre a sessão no Console AWS do computador compartilhado.
2. Exclua apenas a stack **A11 `psi3542-a11` da sua conta**, após registrar evidências, e confira a remoção de Lambda, Rule, API, tabela e dois grupos de logs associados.
3. Não exclua Thing, certificado, IoT Policy ou outras configurações herdadas da A10.
4. Verifique o AWS Billing/Cost Explorer e, se disponível, configure um AWS Budget; uma conta pessoal pode ter cobrança mesmo em atividades didáticas.
5. A HTTP API deste laboratório não autentica o usuário; não a deixe publicada além da janela da prática.
