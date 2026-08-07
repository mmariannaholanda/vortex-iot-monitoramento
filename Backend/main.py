# main.py — Backend do desafio Vortex IoT
# Framework: FastAPI (rápido de aprender, gera documentação automática em /docs)
# Banco: SQLite (um arquivo só, zero configuração de servidor de banco)

from fastapi import FastAPI, HTTPException
# FastAPI  -> a classe principal que cria nossa "aplicação" web
# HTTPException -> usamos pra devolver erros HTTP corretos (ex: 404 quando o device não existe)

from pydantic import BaseModel
# BaseModel -> serve pra descrever o "formato" dos dados que a API espera receber.
# O FastAPI usa isso pra validar automaticamente o JSON que chega (se faltar um campo, ele já recusa sozinho).

import sqlite3
# Biblioteca padrão do Python pra conversar com bancos SQLite — não precisa instalar nada extra.

from datetime import datetime
# Usamos pra gerar o timestamp de quando a medição foi salva no servidor
# (independente do timestamp que o ESP32 mandou, é bom ter os dois).
from fastapi.middleware.cors import CORSMiddleware

# ---------------------------------------------------------------------------
# 1. CONFIGURAÇÃO DO BANCO DE DADOS
# ---------------------------------------------------------------------------

DB_NAME = "vortex.db"
# Nome do arquivo do banco. Ele vai ser criado sozinho na primeira execução, na mesma pasta do script.


def get_connection():
    # Função auxiliar que abre uma conexão nova com o banco toda vez que precisamos ler/escrever.
    # SQLite lida melhor com conexões curtas (abrir, usar, fechar) do que uma conexão só ficando aberta.
    conn = sqlite3.connect(DB_NAME)
    conn.row_factory = sqlite3.Row
    # row_factory = sqlite3.Row permite acessar colunas pelo NOME (ex: linha["temperature"])
    # em vez de só pela posição (ex: linha[1]) — deixa o código bem mais legível.
    return conn


def init_db():
    # Essa função roda UMA VEZ quando o servidor sobe, e garante que a tabela existe.
    # "CREATE TABLE IF NOT EXISTS" não dá erro se a tabela já tiver sido criada antes.
    conn = get_connection()
    conn.execute("""
        CREATE TABLE IF NOT EXISTS measurements (
            id INTEGER PRIMARY KEY AUTOINCREMENT,  -- ID interno do banco, gerado sozinho
            device TEXT NOT NULL,                  -- identificador do ESP32 (ex: "esp-mari-01")
            temperature REAL,                      -- REAL = número com casas decimais
            humidity REAL,
            luminosity REAL,
            device_timestamp TEXT,                 -- timestamp que o PRÓPRIO ESP32 mandou
            server_timestamp TEXT NOT NULL          -- timestamp de quando o SERVIDOR recebeu (mais confiável)
        )
    """)
    conn.commit()
    # commit() é obrigatório no SQLite pra realmente salvar a alteração no arquivo do banco.
    conn.close()
    # Sempre fechar a conexão depois de usar, pra não deixar o arquivo do banco "travado".


# ---------------------------------------------------------------------------
# 2. FORMATO DOS DADOS (o que a API espera receber e devolver)
# ---------------------------------------------------------------------------

class MeasurementIn(BaseModel):
    # Esse é o "molde" do JSON que o ESP32 vai mandar no POST /measurements.
    # Compare com o exemplo de payload do PDF do desafio — os nomes dos campos batem.
    device: str
    temperature: float
    humidity: float
    luminosity: float
    timestamp: str
    # Se o ESP32 mandar um JSON faltando algum desses campos, o FastAPI já rejeita
    # automaticamente com um erro 422, antes mesmo de chegar no nosso código.


# ---------------------------------------------------------------------------
# 3. CRIAÇÃO DA APLICAÇÃO
# ---------------------------------------------------------------------------

app = FastAPI(title="Vortex IoT API")
# Essa variável "app" é o coração da aplicação — é nela que "penduramos" cada endpoint abaixo.
# O título aparece na documentação automática, disponível em http://localhost:8000/docs
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.on_event("startup")
def on_startup():
    # Esse decorator diz: "rode essa função quando o servidor iniciar".
    # É aqui que garantimos que a tabela do banco já existe antes de qualquer requisição chegar.
    init_db()


# ---------------------------------------------------------------------------
# 4. ENDPOINTS OBRIGATÓRIOS (seção 6.3 do desafio)
# ---------------------------------------------------------------------------

@app.post("/measurements")
def create_measurement(m: MeasurementIn):
    # @app.post(...) registra essa função pra rodar quando alguém fizer um POST em /measurements.
    # O parâmetro "m: MeasurementIn" faz o FastAPI já entregar os dados prontos e validados
    # (você não precisa fazer json.loads nem checar campo por campo na mão).

    conn = get_connection()
    conn.execute(
        """
        INSERT INTO measurements
            (device, temperature, humidity, luminosity, device_timestamp, server_timestamp)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        # Usamos "?" como placeholders em vez de colar os valores direto na string SQL.
        # Isso evita SQL injection — nunca faça f"INSERT ... VALUES ({m.device})" com dado externo.
        (m.device, m.temperature, m.humidity, m.luminosity, m.timestamp, datetime.utcnow().isoformat()),
    )
    conn.commit()
    conn.close()

    return {"status": "ok", "message": "Medição salva com sucesso"}
    # O FastAPI converte esse dicionário Python em JSON automaticamente na resposta.


@app.get("/measurements")
def list_measurements(device: str | None = None, limit: int = 100):
    # device: str | None = None -> parâmetro OPCIONAL de query string.
    # Ex: GET /measurements?device=esp-mari-01 filtra só as medições daquele ESP.
    # Sem o parâmetro, devolve as mais recentes de todos os dispositivos.

    conn = get_connection()

    if device:
        # Se o cliente pediu um device específico, filtramos com WHERE.
        rows = conn.execute(
            "SELECT * FROM measurements WHERE device = ? ORDER BY id DESC LIMIT ?",
            (device, limit),
        ).fetchall()
    else:
        # Senão, devolvemos as últimas medições de todo mundo.
        rows = conn.execute(
            "SELECT * FROM measurements ORDER BY id DESC LIMIT ?",
            (limit,),
        ).fetchall()

    conn.close()

    # Convertemos cada "linha" do SQLite (objeto Row) num dicionário Python comum,
    # porque o FastAPI sabe transformar listas de dicionários em JSON, mas não sabe
    # transformar objetos Row diretamente.
    return [dict(row) for row in rows]


@app.get("/devices")
def list_devices():
    # A tabela não tem uma tabela separada "devices" — em vez disso, descobrimos
    # os dispositivos "on the fly" olhando quais valores distintos de "device" já mandaram dados.
    # (Isso é suficiente pro escopo do desafio; um projeto maior teria uma tabela devices própria.)

    conn = get_connection()
    rows = conn.execute(
        """
        SELECT
            device,
            MAX(server_timestamp) AS last_seen,
            COUNT(*) AS total_measurements
        FROM measurements
        GROUP BY device
        """
        # GROUP BY device -> agrupa todas as linhas de cada ESP32 numa linha só de resumo.
        # MAX(server_timestamp) -> pega a medição mais recente daquele device (usado pra status online/offline).
    ).fetchall()
    conn.close()

    return [dict(row) for row in rows]


@app.get("/devices/{device_id}")
def get_device(device_id: str):
    # {device_id} na URL vira automaticamente o parâmetro device_id da função.
    # Ex: GET /devices/esp-mari-01 -> device_id = "esp-mari-01"

    conn = get_connection()
    row = conn.execute(
        """
        SELECT
            device,
            MAX(server_timestamp) AS last_seen,
            COUNT(*) AS total_measurements
        FROM measurements
        WHERE device = ?
        GROUP BY device
        """,
        (device_id,),
    ).fetchone()
    # fetchone() em vez de fetchall() porque só queremos UM resultado (ou nenhum).
    conn.close()

    if row is None:
        # Se não achou nenhuma medição desse device, devolvemos erro 404 (não encontrado)
        # em vez de fingir que deu certo — é assim que uma API "de verdade" se comporta.
        raise HTTPException(status_code=404, detail="Dispositivo não encontrado")

    return dict(row)


# ---------------------------------------------------------------------------
# Como rodar (não precisa disso no código, é só referência):
#
#   pip install fastapi uvicorn
#   uvicorn main:app --reload --host 0.0.0.0 --port 8000
#
# --host 0.0.0.0 é importante: deixa a API acessível por outros dispositivos
# da mesma rede Wi-Fi (como o ESP32), não só pelo seu próprio computador.
#
# Depois de rodar, acesse http://localhost:8000/docs — o FastAPI gera uma
# página interativa onde você testa cada endpoint direto do navegador,
# sem precisar do Postman.
# ---------------------------------------------------------------------------