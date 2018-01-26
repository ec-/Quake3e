add_executable(quake3e
	linux_glimp.c
	linux_joystick.c
	linux_qgl.c
	linux_signals.c
	linux_snd.c
	unix_main.c
	unix_shared.c
	x11_dga.c
	x11_randr.c
	x11_vidmode.c
	)

target_link_libraries(quake3e botlib client jpeg qcommon renderer server X11 Xext m dl)
