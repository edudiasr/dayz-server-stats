modded class MissionServer
{
    override void OnInit()
    {
        super.OnInit();
        ServerStats.Get().Init();
    }

    override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
    {
        super.InvokeOnConnect(player, identity);
        ServerStats.Get().OnConnect(identity);
    }

    override void PlayerDisconnected(PlayerBase player, PlayerIdentity identity, string uid)
    {
        ServerStats.Get().OnDisconnect(uid);
        super.PlayerDisconnected(player, identity, uid);
    }

    override void OnMissionFinish()
    {
        ServerStats.Get().Flush();
        super.OnMissionFinish();
    }
}
