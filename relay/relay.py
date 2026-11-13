"""Lê o killfeed.jsonl que o mod grava e manda cada kill pra API de ranking (POST /events/kill).

    python relay.py --file "C:/DayZServer/profiles/ServerStats/killfeed.jsonl" \
                    --url http://127.0.0.1:8000 --key minha-chave

Fica olhando o arquivo e envia as linhas novas. A posição já enviada vai pra um arquivo
.state ao lado do log, então parar e reiniciar o relay não reenvia nem pula kill.
Só biblioteca padrão: roda no mesmo Windows do servidor sem instalar nada além do Python.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

log = logging.getLogger("relay")

FIELDS = ("killer_id", "killer_name", "victim_id", "victim_name", "weapon", "distance", "headshot")


@dataclass
class Result:
    sent: int = 0
    skipped: int = 0  # linhas que a API recusou (422) ou que não são JSON: puladas, não travam a fila


class Retry(Exception):
    """A API está fora ou pediu pra esperar: a linha não avança e é reenviada depois."""

    def __init__(self, wait: float, reason: str):
        super().__init__(reason)
        self.wait = wait


class Relay:
    def __init__(self, file: Path, url: str, key: str, timeout: float = 5):
        self.file = file
        self.state = file.with_name(file.name + ".state")
        self.endpoint = url.rstrip("/") + "/events/kill"
        self.key = key
        self.timeout = timeout

    # ---------------------------------------------------------------- posição

    def offset(self) -> int:
        try:
            return int(json.loads(self.state.read_text(encoding="utf-8"))["offset"])
        except (FileNotFoundError, ValueError, KeyError):
            return 0

    def save_offset(self, offset: int) -> None:
        tmp = self.state.with_name(self.state.name + ".tmp")
        tmp.write_text(json.dumps({"offset": offset}), encoding="utf-8")
        os.replace(tmp, self.state)  # troca atômica: o .state nunca fica pela metade

    # ---------------------------------------------------------------- envio

    def post(self, kill: dict) -> None:
        body = json.dumps({k: kill[k] for k in FIELDS}).encode()
        req = urllib.request.Request(self.endpoint, data=body, method="POST",
                                     headers={"Content-Type": "application/json", "X-Api-Key": self.key})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout):
                return
        except urllib.error.HTTPError as e:
            if e.code == 429:
                raise Retry(float(e.headers.get("Retry-After", "5")), "limite de requisições") from None
            if e.code >= 500:
                raise Retry(5, f"API respondeu {e.code}") from None
            raise  # 401, 422...: problema com a linha ou com a chave, não adianta repetir igual
        except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
            raise Retry(5, f"API fora do ar ({e})") from None

    def run_once(self) -> Result:
        """Envia tudo que estiver completo no arquivo agora. Para no primeiro Retry."""
        result = Result()
        try:
            size = self.file.stat().st_size
        except FileNotFoundError:
            return result
        offset = self.offset()
        if size < offset:
            log.warning("killfeed.jsonl ficou menor que a posição salva (apagado ou trocado): voltando ao início")
            offset = 0
            self.save_offset(0)

        with self.file.open("rb") as f:
            f.seek(offset)
            for raw in f:
                if not raw.endswith(b"\n"):
                    break  # o mod ainda está escrevendo essa linha
                line = raw.strip()
                if line:
                    try:
                        kill = json.loads(line)
                        self.post(kill)
                        result.sent += 1
                    except (ValueError, KeyError) as e:
                        log.warning("linha ignorada (formato): %s: %s", e, line[:120])
                        result.skipped += 1
                    except urllib.error.HTTPError as e:
                        if e.code == 401:
                            raise SystemExit("API recusou a chave (401). Confira --key.") from None
                        log.warning("API recusou a linha (%s): %s", e.code, line[:120])
                        result.skipped += 1
                offset += len(raw)
                self.save_offset(offset)
        return result

    def run_forever(self, interval: float = 2) -> None:
        while True:
            try:
                r = self.run_once()
                if r.sent or r.skipped:
                    log.info("enviadas %d, puladas %d", r.sent, r.skipped)
                time.sleep(interval)
            except Retry as e:
                log.warning("%s; tentando de novo em %.0f s", e, e.wait)
                time.sleep(e.wait)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--file", type=Path, required=True, help="caminho do killfeed.jsonl")
    parser.add_argument("--url", default="http://127.0.0.1:8000", help="endereço da API de ranking")
    parser.add_argument("--key", default=os.environ.get("LB_API_KEY"), help="X-Api-Key (ou LB_API_KEY)")
    parser.add_argument("--once", action="store_true", help="envia o que tiver e sai")
    args = parser.parse_args()
    if not args.key:
        parser.error("informe --key ou a variável LB_API_KEY")

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    relay = Relay(args.file, args.url, args.key)
    if args.once:
        try:
            r = relay.run_once()
        except Retry as e:
            raise SystemExit(f"parou: {e}") from None
        log.info("enviadas %d, puladas %d", r.sent, r.skipped)
    else:
        relay.run_forever()


if __name__ == "__main__":
    main()
