// Guarda onde foi o último dano em cada jogador (pra saber se a morte foi headshot)
// e avisa o ServerStats quando ele morre. Só roda no servidor.

modded class PlayerBase
{
    protected string m_StatsLastHitZone;
    protected int m_StatsLastHitTime;

    override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef)
    {
        super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);
        if (GetGame().IsDedicatedServer())
        {
            m_StatsLastHitZone = dmgZone;
            m_StatsLastHitTime = GetGame().GetTime();
        }
    }

    override void EEKilled(Object killer)
    {
        if (GetGame().IsDedicatedServer())
            ServerStats.Get().OnPlayerKilled(this, killer, m_StatsLastHitZone, GetGame().GetTime() - m_StatsLastHitTime);
        super.EEKilled(killer);
    }
}
