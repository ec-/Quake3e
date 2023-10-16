#include "client.h"
#include "x_local.h"

// ====================
//   CVars

static cvar_t *x_net_port_auto_renew = 0;

static cvar_t *x_net_show_commands = 0;

static char x_net_last_renewed_server[MAX_OSPATH];

// ====================
//   Const vars

static char X_HELP_NET_PORT_AUTO_RENEW[] = "\n ^fx_net_port_auto_renew^5 0|1^7\n\n"
										   "   Automatically select the network port with the best ping value (when changing a map or joining to a server).\n";

// ====================
//   Static routines

static void InitNetScanProgressBar(void);
static void UpdateStaticServerInfo(void);
static void PortRenewNoisy(void);
static void SendCommand(void);
static void PortRenew(qboolean silent);
static void SetPortAndRestartNetwork(int port);
static void CompletePortScan(void);

// ====================
//   Implementation

void X_Net_Init()
{
	X_Main_RegisterXCommand(x_net_port_auto_renew, "1", "0", "1", X_HELP_NET_PORT_AUTO_RENEW);
	X_Main_RegisterXCommand(x_net_show_commands, "0", "0", "1", 0);

	Cmd_AddCommand("x_net_port_renew", PortRenewNoisy);
	Cmd_AddCommand("x_send", SendCommand);

	InitNetScanProgressBar();

	UpdateStaticServerInfo();
}

static void InitNetScanProgressBar(void)
{
	vec4_t color1, color2;

	MAKERGBA(color1, 0.1f, 0.1f, 0.1f, 0.6f);
	MAKERGBA(color2, 1.f, 0.f, 0.f, 0.6f);

	X_Hud_InitProgressBar(&xmod.net.scanBar, color1, color2, 320.f, 14.f, 420.f, 15.f, X_MAX_NET_PORTS - 1);
}

void X_Net_Teardown(void)
{
	Cmd_RemoveCommand("x_net_port_renew");
}

void X_Net_RenewPortOnSnapshot(snapshot_t *snapshot)
{
	Network *net = &xmod.net;

	if (!net->scan)
	{
		return;
	}

	if (clc.clientNum != snapshot->ps.clientNum)
	{
		return;
	}

	if (snapshot->snapFlags & SNAPFLAG_NOT_ACTIVE)
	{
		return;
	}

	if (net->skipSnaps)
	{
		net->skipSnaps--;
		return;
	}

	if (net->currentSnap < countof(net->snapMS))
	{
		net->snapMS[net->currentSnap] = snapshot->ping;
		net->currentSnap++;
	}

	// Switch to a new port
	qboolean renew = qfalse;
	if (net->currentSnap >= countof(net->snapMS))
	{
		int sum = 0;
		for (int a = 0; a < countof(net->snapMS); a++)
		{
			sum += net->snapMS[a];
		}

		net->avrgMS[net->currentPort] = sum / countof(net->snapMS);

		net->currentPort++;
		net->currentSnap = 0;
		net->scanTime = Sys_Milliseconds();
		net->skipSnaps = 5;

		renew = qtrue;
	}

	// Complete scan
	if (net->currentPort >= countof(net->ports))
	{
		CompletePortScan();
	}
	else if (renew)
	{
		SetPortAndRestartNetwork(net->ports[net->currentPort]);
	}
}

void X_Net_DrawScanProgress()
{
	Network *net = &xmod.net;

	if (!net->scan)
	{
		return;
	}

	X_Hud_DrawProgressBarInCenter(&xmod.net.scanBar, net->currentPort);

	char buffer[64];
	float x = xmod.net.scanBar.x, y = xmod.net.scanBar.y;
	SCR_AdjustFrom640(&x, &y, 0, 0);
	Com_sprintf(buffer, sizeof(buffer), "^kScanning port ^3%d", net->ports[net->currentPort]);
	X_Hud_DrawString(x, y - 14.f, 12.f, 0, 5, xmod.rs.shaderXCharmap, buffer);

	Com_sprintf(buffer, sizeof(buffer), "^8You can feel network lags until scan ends");
	X_Hud_DrawString(x, y + 16.f, 11.f, 0, 5, xmod.rs.shaderCharmap[2], buffer);

	if (clc.clientNum != xmod.snap.ps.clientNum)
	{
		X_Hud_DrawString(x, y + 2.f, 10.f, 0, 5, xmod.rs.shaderCharmap[2], "^9PAUSED");
	}
}

void X_Net_CheckScanPortTimeout()
{
	Network *net = &xmod.net;

	if (!net->scan)
	{
		return;
	}

	int currentMs = Sys_Milliseconds();

	if (net->scanTime + 2000 < currentMs)
	{
		net->scanTime = Sys_Milliseconds();
		net->ports[net->currentPort] = (rand() % 320) * (rand() % 100) + 10000;
		SetPortAndRestartNetwork(net->ports[net->currentPort]);
	}
}

qboolean X_Net_ShowCommands(void)
{
	if (!X_Main_IsXModeActive())
	{
		return qfalse;
	}

	return (x_net_show_commands->integer ? qtrue : qfalse);
}

static void UpdateStaticServerInfo(void)
{
	if (clc.demoplaying)
	{
		return;
	}

	X_Con_PrintToChatSection("connected to ^7%s", cls.servername);

	if (x_net_port_auto_renew && x_net_port_auto_renew->integer)
	{
		if (Q_stricmp(cls.servername, x_net_last_renewed_server))
		{
			Q_strncpyz(x_net_last_renewed_server, cls.servername, sizeof(x_net_last_renewed_server));
			PortRenew(qtrue);
		}
	}
	else
	{
		x_net_last_renewed_server[0] = 0;
	}

}

static void PortRenewNoisy()
{
	if (clc.demoplaying)
	{
		Com_Printf("Can't renew port when demo is playing");
		return;
	}

	PortRenew(qfalse);
}

static void PortRenew(qboolean silent)
{
	if (xmod.net.scan)
	{
		return;
	}

	Com_Printf("^fPort scan has been started\n");

	Network *net = &xmod.net;
	net->scan = qtrue;
	net->currentPort = 0;
	net->currentSnap = 0;
	net->scanTime = Sys_Milliseconds();
	net->skipSnaps = 5;
	net->silent = silent;

	memset(net->snapMS, 0, sizeof(net->snapMS));

	for (int i = 0; i < countof(net->ports); i++)
	{
		if (i == 0)
		{
			net->ports[i] = Cvar_VariableIntegerValue("net_port");//TODO: ipv6 support
		}
		else
		{
			net->ports[i] = (rand() % 320) * (rand() % 100) + 10000;
		}

		net->avrgMS[i] = 999;
	}

	SetPortAndRestartNetwork(net->ports[0]);
}

static void SetPortAndRestartNetwork(int port)
{
	char cmd[64];

	X_Main_XModeDisableOutput(qtrue);

	Com_sprintf(cmd, sizeof(cmd), "net_port %d", port);
	Cmd_ExecuteString(cmd);

	Com_sprintf(cmd, sizeof(cmd), "net_port6 %d", port);
	Cmd_ExecuteString(cmd);

	Cmd_ExecuteString("net_restart");

	X_Main_XModeDisableOutput(qfalse);
}

static void CompletePortScan(void)
{
	Network *net = &xmod.net;

	net->scan = qfalse;

	int lowest = 999, port = 27960;

	Com_Printf("^fPort scan has been completed\n");

	if (!net->silent)
	{
		Com_Printf(
		"\n  ^7Summary\n\n"
		);
	}

	for (int i = 0; i < countof(net->avrgMS); i++)
	{
		if (lowest > net->avrgMS[i])
		{
			lowest = net->avrgMS[i];
			port = net->ports[i];
		}

		if (!net->silent)
		{
			Com_Printf("    ^7port %4d -> %d ms\n", net->ports[i], net->avrgMS[i]);
		}
	}

	int origPort = net->ports[0], origMs = net->avrgMS[0];
	Com_Printf(
	"\n  ^7Selected top port ^f%5d^7 with ^f%d^7 ms\n",
	port, lowest
	);

	if (origPort != port)
	{
		Com_Printf(
		"           ^7old port ^f%5d^7 has ^f%2d^7 ms (^z+^1%d^7^zms)\n\n",
		origPort, origMs, origMs - lowest);
	}
	else
	{
		Com_Printf("\n");
	}

	SetPortAndRestartNetwork(port);
}


static void SendCommand(void)
{
	if (Cmd_Argc() > 1)
	{

		CL_AddReliableCommand(Cmd_Argv(1), qfalse);
	}
}
