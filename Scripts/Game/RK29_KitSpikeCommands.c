//------------------------------------------------------------------------------------------------
//! 29th Infantry Division - M1 APPLY SPIKE, chat trigger (throwaway)
//!
//!   /kit <index>   - apply cached spike kit <index> to yourself
//!   /kitlist       - print how many kits the spike is holding
//!
//! Mirrors the RT_ round timer pattern: commands register CLIENT-side against the chat panel
//! manager, then route through SCR_PlayerController because the game mode is server-owned and a
//! client cannot RPC it directly.
//!
//! Additive modded class with RK29_-prefixed members, so it stacks with the round timer mod's own
//! SCR_GameModeEditor modding.
//!
//! Nothing here is intended to survive M1.
//------------------------------------------------------------------------------------------------
modded class SCR_GameModeEditor
{
	//------------------------------------------------------------------------------------------------
	override void OnGameStart()
	{
		super.OnGameStart();

		SCR_ChatPanelManager chatMgr = SCR_ChatPanelManager.GetInstance();
		if (!chatMgr)
		{
			Print("[RK29Spike] no chat manager (dedicated?) - spike commands not registered here", LogLevel.NORMAL);
			return;
		}

		ChatCommandInvoker invKit = chatMgr.GetCommandInvoker("kit");
		if (invKit)
			invKit.Insert(RK29_OnChatKit);

		ChatCommandInvoker invKitList = chatMgr.GetCommandInvoker("kitlist");
		if (invKitList)
			invKitList.Insert(RK29_OnChatKitList);

		Print("[RK29Spike] chat commands registered (kit/kitlist)", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! CLIENT: "/kit <index>" -> ask the server to apply that cached kit to us.
	protected void RK29_OnChatKit(SCR_ChatPanel panel, string data)
	{
		int kitIdx = 0;

		array<string> tokens = {};
		data.Split(" ", tokens, false);
		if (tokens.Count() >= 1 && tokens[0] != "")
			kitIdx = tokens[0].ToInt();

		if (kitIdx < 0)
			kitIdx = 0;

		Print(string.Format("[RK29Spike] /kit -> index=%1", kitIdx), LogLevel.NORMAL);

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.RK29_RequestApplyKit(kitIdx);
	}

	//------------------------------------------------------------------------------------------------
	//! CLIENT: "/kitlist" -> ask the server to log its cache state.
	protected void RK29_OnChatKitList(SCR_ChatPanel panel, string data)
	{
		Print("[RK29Spike] /kitlist", LogLevel.NORMAL);

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.RK29_RequestKitList();
	}
}
