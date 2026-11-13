class CfgPatches
{
    class ServerStats
    {
        units[] = {};
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] = {"DZ_Data", "DZ_Scripts"};
    };
};

class CfgMods
{
    class ServerStats
    {
        dir = "ServerStats";
        name = "Server Stats";
        author = "Eduardo Dias";
        type = "mod";
        dependencies[] = {"World", "Mission"};

        class defs
        {
            class worldScriptModule
            {
                value = "";
                files[] = {"ServerStats/scripts/4_World"};
            };
            class missionScriptModule
            {
                value = "";
                files[] = {"ServerStats/scripts/5_Mission"};
            };
        };
    };
};
