# Plataforma IoT para Monitoramento Inteligente de Ambientes

Projeto desenvolvido para o desafio técnico do processo seletivo de estágio em IoT —
Laboratório Vortex (UNIFOR).

> Substitua este parágrafo por 2-3 frases contando o "contexto fictício" que você deu
> ao projeto (ex: "Sistema de monitoramento para uma estufa de pesquisa botânica...").

---

## 1. Visão geral da arquitetura

```
[Sensores] → [ESP32] → Wi-Fi (HTTP) → [API REST / FastAPI] → [SQLite] → [Dashboard Web]
```

- **Firmware**: ESP32, simula leituras de temperatura, umidade e luminosidade,
  envia dados via HTTP POST em JSON a cada N segundos.
- **Backend**: API REST em FastAPI + banco SQLite.
- **Dashboard**: HTML/JS puro, consulta a API e atualiza automaticamente.

> Link do circuito no Wokwi: `[cole aqui]`

---

## 2. Como rodar o projeto

### Backend

```bash
cd Backend
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

A documentação interativa da API fica em `http://localhost:8000/docs`.

### Firmware (ESP32 / Wokwi)

> Descreva aqui: como abrir o projeto no Wokwi, ou como gravar no ESP32 real
> (Arduino IDE, board selecionada, bibliotecas instaladas).
> Não esqueça de trocar o IP/porta da API no código pelo IP da sua máquina na rede.

### Dashboard

> Descreva aqui: como abrir (ex: "abra o arquivo index.html no navegador"
> ou "rode `python -m http.server` dentro da pasta Frontend").

---

## 3. Endpoints da API

| Método | Rota | Descrição |
|---|---|---|
| POST | `/measurements` | Recebe uma nova medição do ESP32 |
| GET | `/measurements` | Lista as medições (parâmetro opcional `device`) |
| GET | `/devices` | Lista os dispositivos que já enviaram dados |
| GET | `/devices/{id}` | Detalhes de um dispositivo específico |

Exemplo de payload enviado pelo ESP32:

```json
{
  "device": "esp-mari-01",
  "temperature": 26.3,
  "humidity": 61,
  "luminosity": 420,
  "timestamp": "2026-08-06T10:00:00"
}
```

---

## 4. Estrutura do projeto

```
Projeto/
├── Firmware/
├── Backend/
├── Frontend/
├── docs/
├── video/
└── README.md
```

---

## 5. Diagrama elétrico

> Cole aqui um print do circuito montado no Wokwi, com a legenda dos pinos usados
> (ex: DHT22 no GPIO 4, LDR no GPIO 34, etc.).

---

## 6. Uso de Inteligência Artificial

> Esta seção é obrigatória (item 9 do desafio) e vale 10% da nota — preencha com
> honestidade e detalhe, é isso que está sendo avaliado, não o fato de ter usado IA.

### Ferramentas utilizadas
> Ex: Claude (Anthropic), para geração de código do backend e planejamento do cronograma.

### Prompts importantes
> Cole aqui os 2-4 prompts que mais impactaram o projeto. Não precisa ser todos,
> só os que geraram decisões relevantes (ex: escolha do FastAPI, estrutura do banco).

### Dificuldades encontradas
> Ex: "Não sabia como funcionava uma API antes deste projeto. Tive dificuldade em
> entender a diferença entre GET e POST no início, e em configurar o host 0.0.0.0
> para o ESP32 conseguir acessar o backend pela rede local."

### Como validei as respostas da IA
> Ex: "Testei cada endpoint manualmente com a documentação /docs do FastAPI antes
> de integrar com o ESP32. Rodei o código linha a linha para entender a lógica
> antes de aceitar, e pesquisei por conta própria os conceitos que não conhecia
> (ex: o que é SQL injection, por que usar '?' em vez de f-strings no SQL)."

### Reflexão crítica sobre o uso da IA
> Ex: "A IA acelerou muito o aprendizado de conceitos novos (API, FastAPI), mas
> as decisões de arquitetura e os testes finais foram feitos por mim. Prefiro usar
> IA como um tutor que explica o 'porquê', não como uma caixa-preta que só entrega
> código pronto."

---

## 7. Autor

> Nome, contato, link do vídeo de apresentação.
