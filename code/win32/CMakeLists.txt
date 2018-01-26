add_executable(quake3e
	win_gamma.c
	win_glimp.c
	win_input.c
	win_main.c
	win_minimize.c
	win_qgl.c
	win_shared.c
	win_snd.c
	win_syscon.c
	win_wndproc.c
	)

target_link_libraries(quake3e botlib client jpeg qcommon renderer server wsock32 gdi32 winmm ole32 opengl32 ws2_32 psapi comctl32 dxerr9)
