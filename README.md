# Server Stats (mod de servidor para DayZ)

Mod só de servidor, em Enforce Script, que acompanha o que acontece entre os jogadores:

- **killfeed no chat**: "Lobo matou Corvo com M4-A1 a 182 m (headshot)"
- **estatísticas por jogador**: kills, mortes, suicídios, headshots, infectados mortos, kill mais
  longa (e com qual arma), tempo de jogo, primeira e última vez online
- **ranking** em `leaderboard.json`, regravado a cada minuto
- **log de kills** em `killfeed.jsonl`, uma linha por kill, que um relay em Python manda pra
  [API de ranking em tempo real](https://github.com/edudiasr/realtime-leaderboard-redis)

Como roda só no servidor (`-servermod`), o jogador não precisa baixar nada.

```
  DayZServer                             relay.py                     API de ranking (Redis)
 ┌───────────────────────┐        ┌────────────────────┐        ┌──────────────────────────┐
 │ PlayerBase.EEKilled   │ append │ lê linhas novas    │  POST  │ /events/kill             │
 │   → ServerStats       ├───────▶│ guarda a posição   ├───────▶│ ranking por dia/semana   │
 │ killfeed.jsonl        │        │ respeita 429       │        │ página ao vivo (SSE)     │
 └───────────────────────┘        └────────────────────┘        └──────────────────────────┘
```

## Arquivos gerados

Ficam em `profiles/ServerStats/` (a pasta do `-profiles=`):

| Arquivo | O que tem |
|---|---|
| `config.json` | configuração, criada com o padrão na primeira vez ([exemplo](examples/config.json)) |
| `stats.json` | todos os jogadores que já entraram |
| `leaderboard.json` | os 10 primeiros por kills, desempate por K/D ([exemplo](examples/leaderboard.json)) |
| `killfeed.jsonl` | uma kill por linha ([exemplo](examples/killfeed.jsonl)) |

## Decisões

**Quem matou.** O `EEKilled` recebe como "killer" a *arma* quando a morte foi por tiro e o próprio
*jogador* quando foi corpo a corpo. Por isso o mod sobe a hierarquia (`GetHierarchyRootPlayer`) pra
achar quem segurava a arma e, no corpo a corpo, pega o item na mão como nome da arma. Quando não há
jogador no fim (infectado, animal, queda, sangramento), conta só a morte da vítima.

**Headshot.** O `EEKilled` não diz onde foi o tiro. O mod guarda a zona do último dano no `EEHitBy` e,
na morte, conta headshot se essa zona era `Head` ou `Brain` e o dano foi há menos de 2 segundos.
Sem a janela de tempo, alguém que levou um tiro na cabeça, se curou e morreu de fome dez minutos
depois contaria como headshot.

**Disco.** As estatísticas ficam em memória e são gravadas a cada `saveIntervalSec`, quando alguém
sai e quando o servidor desliga. Assim uma troca de tiro com dez mortes não vira dez gravações de arquivo.
A gravação vai primeiro pra `stats.json.tmp` e só depois substitui o `stats.json`. Se o servidor
cair no meio, o arquivo antigo continua inteiro, e se cair entre apagar e copiar, o mod lê o `.tmp`.

**Tempo de jogo** é somado a cada gravação, não só na saída. Se o servidor cair, perde no máximo
um intervalo.

## Relay

`relay/relay.py` usa só a biblioteca padrão do Python, então roda no próprio Windows do servidor.

```bash
python relay/relay.py --file "C:/DayZServer/profiles/ServerStats/killfeed.jsonl" \
                      --url http://127.0.0.1:8000 --key SUA_CHAVE
```

- guarda a posição já enviada num `.state` ao lado do log (troca atômica com `os.replace`), então
  reiniciar o relay não reenvia nem pula kill
- linha que o mod ainda está escrevendo (sem `\n` no fim) fica pra próxima volta
- 429 da API: espera o `Retry-After` e reenvia a mesma linha. API fora do ar: tenta de novo em 5 s
- linha que a API recusa (422) ou que não é JSON é registrada e pulada, pra não travar a fila
- se o `killfeed.jsonl` for apagado e recomeçar, ele percebe (arquivo menor que a posição) e volta ao início

Os testes (`relay/test_relay.py`) sobem uma API falsa numa thread e cobrem cada um desses casos.
Contra a API de ranking de verdade:

```
$ python relay/relay.py --file killfeed.jsonl --key dev-key --once
12:47:07 enviadas 5, puladas 0
$ python relay/relay.py --file killfeed.jsonl --key dev-key --once
12:47:09 enviadas 0, puladas 0
```

## Instalação

1. Empacote a pasta `ServerStats` com o Addon Builder do DayZ Tools em `@ServerStats/addons/ServerStats.pbo`
2. Copie `@ServerStats` pra pasta do servidor
3. Inicie com `-servermod=@ServerStats` (junto com os outros que já usar: `-servermod=@A;@ServerStats`)
4. Na primeira subida o mod cria `profiles/ServerStats/config.json`. Edite e reinicie se quiser
   mudar o texto do killfeed ou desligar alguma parte

## Estrutura

```
ServerStats/
  config.cpp                                  CfgPatches e CfgMods
  scripts/4_World/ServerStats/
    ServerStats.c        estatísticas, gravação, ranking, killfeed
    ServerStatsData.c    classes que viram os JSON
    PlayerBase.c         EEHitBy (zona do último dano) e EEKilled
    ZombieBase.c         infectados mortos
  scripts/5_Mission/ServerStats/
    MissionServer.c      início, entrada, saída e desligamento do servidor
relay/
  relay.py               killfeed.jsonl -> POST /events/kill
  test_relay.py
examples/                exemplos dos arquivos gerados (jogadores fictícios)
```

## Licença

MIT
