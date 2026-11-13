"""Testes do relay contra uma API falsa que roda numa thread."""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from relay import Relay, Retry


def kill(n: int, **extra) -> dict:
    return {"ts": "2026-11-13T21:00:00Z", "killer_id": f"7656119000000000{n}", "killer_name": f"K{n}",
            "victim_id": "76561190000000099", "victim_name": "V", "weapon": "M4-A1", "distance": 120.5,
            "headshot": False, "x": 7000, "y": 9000, **extra}


class FakeApi:
    """Responde com os códigos da fila `replies` (depois dela, 201) e guarda o que recebeu."""

    def __init__(self):
        self.received: list[dict] = []
        self.keys: list[str] = []
        self.replies: list[tuple[int, dict]] = []
        api = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                code, headers = api.replies.pop(0) if api.replies else (201, {})
                if code == 201:
                    api.received.append(body)
                    api.keys.append(self.headers["X-Api-Key"])
                self.send_response(code)
                for k, v in headers.items():
                    self.send_header(k, v)
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"{}")

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_port}"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


@pytest.fixture
def api():
    a = FakeApi()
    yield a
    a.close()


@pytest.fixture
def feed(tmp_path):
    return tmp_path / "killfeed.jsonl"


def write(feed, *kills, partial: str = ""):
    with feed.open("a", encoding="utf-8", newline="") as f:
        for k in kills:
            f.write(json.dumps(k) + "\r\n")  # o FPrintln do servidor no Windows termina com \r\n
        f.write(partial)


def test_sends_every_line_with_the_key(api, feed):
    write(feed, kill(1), kill(2), kill(3))
    r = Relay(feed, api.url, "segredo").run_once()
    assert r.sent == 3
    assert [k["killer_name"] for k in api.received] == ["K1", "K2", "K3"]
    assert set(api.keys) == {"segredo"}
    # só os campos que a API aceita: ts, x e y ficam no arquivo
    assert set(api.received[0]) == {"killer_id", "killer_name", "victim_id", "victim_name", "weapon",
                                    "distance", "headshot"}


def test_restart_does_not_resend(api, feed):
    write(feed, kill(1), kill(2))
    Relay(feed, api.url, "k").run_once()
    write(feed, kill(3))
    r = Relay(feed, api.url, "k").run_once()  # outro processo, lendo o .state
    assert r.sent == 1
    assert [k["killer_name"] for k in api.received] == ["K1", "K2", "K3"]


def test_waits_for_a_line_still_being_written(api, feed):
    line = json.dumps(kill(2))
    write(feed, kill(1), partial=line[:30])
    relay = Relay(feed, api.url, "k")
    assert relay.run_once().sent == 1
    write(feed, partial=line[30:] + "\n")
    assert relay.run_once().sent == 1
    assert api.received[1]["killer_name"] == "K2"


def test_rate_limit_keeps_position_and_retries(api, feed):
    write(feed, kill(1), kill(2), kill(3))
    api.replies = [(201, {}), (429, {"Retry-After": "7"})]
    relay = Relay(feed, api.url, "k")
    with pytest.raises(Retry) as e:
        relay.run_once()
    assert e.value.wait == 7
    assert len(api.received) == 1
    assert relay.run_once().sent == 2  # a segunda volta do começo dela, não pula
    assert [k["killer_name"] for k in api.received] == ["K1", "K2", "K3"]


def test_rejected_and_broken_lines_are_skipped(api, feed):
    write(feed, kill(1))
    write(feed, partial="isso não é json\n")
    write(feed, kill(2), kill(3))
    api.replies = [(201, {}), (422, {})]  # a API recusa a kill(2)
    r = Relay(feed, api.url, "k").run_once()
    assert (r.sent, r.skipped) == (2, 2)
    assert [k["killer_name"] for k in api.received] == ["K1", "K3"]


def test_api_down_keeps_position(feed, api):
    write(feed, kill(1))
    url = api.url
    api.close()
    relay = Relay(feed, url, "k", timeout=1)
    with pytest.raises(Retry):
        relay.run_once()
    assert relay.offset() == 0


def test_wrong_key_stops(api, feed):
    write(feed, kill(1))
    api.replies = [(401, {})]
    with pytest.raises(SystemExit):
        Relay(feed, api.url, "errada").run_once()


def test_truncated_file_starts_over(api, feed):
    write(feed, kill(1), kill(2))
    relay = Relay(feed, api.url, "k")
    relay.run_once()
    feed.write_text(json.dumps(kill(5)) + "\n", encoding="utf-8")  # log apagado e recomeçado
    assert relay.run_once().sent == 1
    assert api.received[-1]["killer_name"] == "K5"


def test_missing_file_is_not_an_error(api, feed):
    assert Relay(feed, api.url, "k").run_once().sent == 0
