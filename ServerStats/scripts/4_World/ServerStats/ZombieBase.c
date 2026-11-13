modded class ZombieBase
{
    override void EEKilled(Object killer)
    {
        if (GetGame().IsDedicatedServer())
            ServerStats.Get().OnInfectedKilled(killer);
        super.EEKilled(killer);
    }
}
