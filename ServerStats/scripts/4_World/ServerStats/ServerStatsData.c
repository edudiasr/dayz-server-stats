// Formato dos arquivos que o mod grava em $profile:ServerStats/
// Os nomes dos campos são os nomes no JSON.

class ServerStatsConfig
{
    int version = 1;

    // mensagem no chat de todo mundo quando um jogador mata outro
    bool killfeed = true;
    // %1 matador, %2 vítima, %3 arma, %4 distância em metros
    string killfeedFormat = "%1 matou %2 com %3 a %4 m";
    string killfeedHeadshot = " (headshot)";

    // killfeed.jsonl: uma linha por kill, lida pelo relay que manda pro ranking
    bool writeKillLog = true;

    // de quanto em quanto tempo stats.json e leaderboard.json são regravados
    int saveIntervalSec = 60;
    int leaderboardSize = 10;
}

class ServerStatsPlayer
{
    string id;  // Steam64
    string name;
    int kills;
    int deaths;
    int suicides;
    int headshots;
    int infectedKills;
    float longestKill;
    string longestWeapon;
    int playtimeSec;
    string firstSeen;
    string lastSeen;

    float KD()
    {
        if (deaths == 0)
            return kills;
        float d = deaths;
        return kills / d;
    }
}

class ServerStatsFile
{
    int version = 1;
    ref array<ref ServerStatsPlayer> players = new array<ref ServerStatsPlayer>;
}

// Uma linha do killfeed.jsonl. Os nomes batem com o POST /events/kill da API de ranking.
class ServerStatsKill
{
    string ts;
    string killer_id;
    string killer_name;
    string victim_id;
    string victim_name;
    string weapon;
    float distance;
    bool headshot;
    float x;
    float y;
}

class ServerStatsBoardEntry
{
    int rank;
    string id;
    string name;
    int kills;
    int deaths;
    float kd;
    int headshots;
    float longestKill;
}

class ServerStatsBoard
{
    string updated;
    ref array<ref ServerStatsBoardEntry> top = new array<ref ServerStatsBoardEntry>;
}
