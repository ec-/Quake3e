add_library(server STATIC
	sv_bot.c
	sv_ccmds.c
	sv_client.c
	sv_game.c
	sv_init.c
	sv_main.c
	sv_net_chan.c
	#sv_rankings.c
	sv_snapshot.c
	sv_world.c
	)

target_link_libraries(server botlib)
