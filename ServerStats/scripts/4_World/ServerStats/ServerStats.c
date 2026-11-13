// Guarda as estatísticas de cada jogador e grava em $profile:ServerStats/
//
//   config.json        configuração (criada com o padrão na primeira vez)
//   stats.json         todos os jogadores que já entraram
//   leaderboard.json   os N primeiros por kills
//   killfeed.jsonl     uma linha por kill entre jogadores
//
// Tudo fica em memória e vai pro disco a cada saveIntervalSec, quando alguém sai e quando o
// servidor desliga. Assim uma troca de tiro com dez mortes não vira dez gravações de arquivo.

class ServerStats
{
    static const string DIR = "$profile:ServerStats";
    static const string CONFIG_PATH = "$profile:ServerStats/config.json";
    static const string STATS_PATH = "$profile:ServerStats/stats.json";
    static const string STATS_TMP = "$profile:ServerStats/stats.json.tmp";
    static const string BOARD_PATH = "$profile:ServerStats/leaderboard.json";
    static const string KILLS_PATH = "$profile:ServerStats/killfeed.jsonl";

    // um tiro só conta como headshot se o último dano na vítima foi na cabeça há menos disso
    static const int HEADSHOT_WINDOW_MS = 2000;

    private static ref ServerStats s_Instance;

    protected ref ServerStatsConfig m_Config;
    protected ref map<string, ref ServerStatsPlayer> m_Players;
    protected ref map<string, int> m_SessionStart;  // Steam64 -> GetGame().GetTime() de quando entrou
    protected bool m_Dirty;

    static ServerStats Get()
    {
        if (!s_Instance)
            s_Instance = new ServerStats();
        return s_Instance;
    }

    void ServerStats()
    {
        m_Players = new map<string, ref ServerStatsPlayer>;
        m_SessionStart = new map<string, int>;
    }

    void Init()
    {
        if (!FileExist(DIR))
            MakeDirectory(DIR);
        LoadConfig();
        LoadStats();
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(Flush, m_Config.saveIntervalSec * 1000, true);
        Print("[ServerStats] " + m_Players.Count().ToString() + " jogadores carregados");
    }

    // ---------------------------------------------------------------- eventos

    void OnConnect(PlayerIdentity identity)
    {
        ServerStatsPlayer p = Player(identity);
        if (!p)
            return;
        p.lastSeen = Now();
        m_SessionStart.Set(p.id, GetGame().GetTime());
        m_Dirty = true;
    }

    void OnDisconnect(string uid)
    {
        AddPlaytime(uid);
        m_SessionStart.Remove(uid);
        ServerStatsPlayer p = m_Players.Get(uid);
        if (p)
            p.lastSeen = Now();
        m_Dirty = true;
        Flush();
    }

    void OnPlayerKilled(PlayerBase victim, Object killer, string lastHitZone, int lastHitAgoMs)
    {
        ServerStatsPlayer v = Player(victim.GetIdentity());
        if (!v)
            return;  // corpo de quem já desconectou
        v.deaths++;
        m_Dirty = true;

        PlayerBase murderer = ResolvePlayer(killer);
        if (!murderer || !murderer.GetIdentity())
            return;  // infectado, animal, queda, fome, sangramento...
        if (murderer == victim)
        {
            v.suicides++;
            return;
        }

        ServerStatsPlayer k = Player(murderer.GetIdentity());
        float distance = vector.Distance(victim.GetPosition(), murderer.GetPosition());
        bool headshot = lastHitAgoMs < HEADSHOT_WINDOW_MS && (lastHitZone == "Head" || lastHitZone == "Brain");
        string weapon = WeaponName(EntityAI.Cast(killer), murderer);

        k.kills++;
        if (headshot)
            k.headshots++;
        if (distance > k.longestKill)
        {
            k.longestKill = Math.Round(distance * 10) / 10;
            k.longestWeapon = weapon;
        }

        if (m_Config.writeKillLog)
            AppendKill(k, v, weapon, distance, headshot, victim.GetPosition());

        if (m_Config.killfeed)
        {
            int meters = Math.Round(distance);
            string msg = string.Format(m_Config.killfeedFormat, k.name, v.name, weapon, meters.ToString());
            if (headshot)
                msg += m_Config.killfeedHeadshot;
            Broadcast(msg);
        }
    }

    void OnInfectedKilled(Object killer)
    {
        PlayerBase murderer = ResolvePlayer(killer);
        if (!murderer)
            return;
        ServerStatsPlayer k = Player(murderer.GetIdentity());
        if (!k)
            return;
        k.infectedKills++;
        m_Dirty = true;
    }

    // ---------------------------------------------------------------- disco

    void Flush()
    {
        // tempo de jogo de quem está online entra a cada gravação, não só na saída:
        // se o servidor cair, perde no máximo um intervalo
        // (as chaves vão pra um array antes: AddPlaytime mexe no mapa)
        array<string> online = m_SessionStart.GetKeyArray();
        foreach (string uid : online)
            AddPlaytime(uid);

        if (!m_Dirty)
            return;

        ServerStatsFile file = new ServerStatsFile();
        foreach (string id, ServerStatsPlayer p : m_Players)
            file.players.Insert(p);

        // grava num .tmp e só então substitui: se cair no meio, o stats.json antigo continua inteiro
        string error;
        if (!JsonFileLoader<ServerStatsFile>.SaveFile(STATS_TMP, file, error))
        {
            Print("[ServerStats] não gravou stats: " + error);
            return;
        }
        DeleteFile(STATS_PATH);
        CopyFile(STATS_TMP, STATS_PATH);

        WriteLeaderboard();
        m_Dirty = false;
    }

    protected void LoadConfig()
    {
        string error;
        m_Config = new ServerStatsConfig();
        if (FileExist(CONFIG_PATH))
        {
            if (!JsonFileLoader<ServerStatsConfig>.LoadFile(CONFIG_PATH, m_Config, error))
            {
                Print("[ServerStats] config.json inválido, usando o padrão: " + error);
                m_Config = new ServerStatsConfig();
            }
        }
        else
        {
            JsonFileLoader<ServerStatsConfig>.SaveFile(CONFIG_PATH, m_Config, error);
        }
        if (m_Config.saveIntervalSec < 10)
            m_Config.saveIntervalSec = 10;
        if (m_Config.leaderboardSize < 1)
            m_Config.leaderboardSize = 10;
    }

    protected void LoadStats()
    {
        ServerStatsFile file = new ServerStatsFile();
        string error;
        bool ok = FileExist(STATS_PATH) && JsonFileLoader<ServerStatsFile>.LoadFile(STATS_PATH, file, error);
        if (!ok && FileExist(STATS_TMP))
        {
            // caiu entre apagar o stats.json e copiar o .tmp por cima
            file = new ServerStatsFile();
            ok = JsonFileLoader<ServerStatsFile>.LoadFile(STATS_TMP, file, error);
        }
        if (!ok)
        {
            if (error != "")
                Print("[ServerStats] stats.json ilegível, começando do zero: " + error);
            return;
        }
        foreach (ServerStatsPlayer p : file.players)
        {
            if (p && p.id != "")
                m_Players.Set(p.id, p);
        }
    }

    protected void WriteLeaderboard()
    {
        // ordena por kills e, no empate, por K/D (inserção: são poucos jogadores)
        array<ServerStatsPlayer> sorted = new array<ServerStatsPlayer>;
        foreach (string id, ServerStatsPlayer p : m_Players)
        {
            if (p.kills == 0)
                continue;
            int i = sorted.Count();
            while (i > 0 && Better(p, sorted[i - 1]))
                i--;
            sorted.InsertAt(p, i);
        }

        ServerStatsBoard board = new ServerStatsBoard();
        board.updated = Now();
        int n = Math.Min(sorted.Count(), m_Config.leaderboardSize);
        for (int r = 0; r < n; r++)
        {
            ServerStatsPlayer s = sorted[r];
            ServerStatsBoardEntry e = new ServerStatsBoardEntry();
            e.rank = r + 1;
            e.id = s.id;
            e.name = s.name;
            e.kills = s.kills;
            e.deaths = s.deaths;
            e.kd = Math.Round(s.KD() * 100) / 100;
            e.headshots = s.headshots;
            e.longestKill = s.longestKill;
            board.top.Insert(e);
        }
        string error;
        JsonFileLoader<ServerStatsBoard>.SaveFile(BOARD_PATH, board, error);
    }

    protected void AppendKill(ServerStatsPlayer k, ServerStatsPlayer v, string weapon, float distance, bool headshot, vector pos)
    {
        ServerStatsKill ev = new ServerStatsKill();
        ev.ts = Now();
        ev.killer_id = k.id;
        ev.killer_name = k.name;
        ev.victim_id = v.id;
        ev.victim_name = v.name;
        ev.weapon = weapon;
        ev.distance = Math.Round(distance * 10) / 10;
        ev.headshot = headshot;
        ev.x = Math.Round(pos[0]);
        ev.y = Math.Round(pos[2]);  // no DayZ o "y" do mapa é o z do mundo

        string line;
        JsonSerializer json = new JsonSerializer();
        if (!json.WriteToString(ev, false, line))
            return;
        FileHandle fh = OpenFile(KILLS_PATH, FileMode.APPEND);
        if (fh == 0)
            return;
        FPrintln(fh, line);
        CloseFile(fh);
    }

    // ---------------------------------------------------------------- auxiliares

    protected ServerStatsPlayer Player(PlayerIdentity identity)
    {
        if (!identity)
            return null;
        string id = identity.GetPlainId();
        ServerStatsPlayer p = m_Players.Get(id);
        if (!p)
        {
            p = new ServerStatsPlayer();
            p.id = id;
            p.firstSeen = Now();
            m_Players.Set(id, p);
        }
        p.name = identity.GetName();
        return p;
    }

    protected void AddPlaytime(string uid)
    {
        int start;
        if (!m_SessionStart.Find(uid, start))
            return;
        ServerStatsPlayer p = m_Players.Get(uid);
        int now = GetGame().GetTime();
        if (p)
        {
            p.playtimeSec += (now - start) / 1000;
            m_Dirty = true;
        }
        // guarda o resto em milissegundos pra não perder fração a cada intervalo
        m_SessionStart.Set(uid, now - (now - start) % 1000);
    }

    // o "killer" que o jogo passa é a arma quando é tiro, e o próprio jogador quando é corpo a corpo
    static PlayerBase ResolvePlayer(Object killer)
    {
        EntityAI source = EntityAI.Cast(killer);
        if (!source)
            return null;
        PlayerBase player = PlayerBase.Cast(source.GetHierarchyRootPlayer());
        if (!player)
            player = PlayerBase.Cast(source);
        return player;
    }

    protected static string WeaponName(EntityAI source, PlayerBase murderer)
    {
        EntityAI item = source;
        if (source == murderer)
            item = murderer.GetItemInHands();
        if (!item)
            return "as mãos";
        return item.GetDisplayName();
    }

    protected static bool Better(ServerStatsPlayer a, ServerStatsPlayer b)
    {
        if (a.kills != b.kills)
            return a.kills > b.kills;
        return a.KD() > b.KD();
    }

    protected void Broadcast(string message)
    {
        array<Man> players = new array<Man>;
        GetGame().GetPlayers(players);
        foreach (Man man : players)
        {
            PlayerBase p = PlayerBase.Cast(man);
            if (p && p.GetIdentity())
                GetGame().RPCSingleParam(p, ERPCs.RPC_USER_ACTION_MESSAGE, new Param1<string>(message), true, p.GetIdentity());
        }
    }

    static string Now()
    {
        int year, month, day, hour, minute, second;
        GetYearMonthDayUTC(year, month, day);
        GetHourMinuteSecondUTC(hour, minute, second);
        return string.Format("%1-%2-%3T%4:%5:%6Z", year, month.ToStringLen(2), day.ToStringLen(2),
            hour.ToStringLen(2), minute.ToStringLen(2), second.ToStringLen(2));
    }
}
