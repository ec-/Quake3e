if(DEDICATED)
	add_executable(q3eded
		linux_signals.c
		unix_main.c
		unix_shared.c
		)
	target_link_libraries(q3eded botlib qcommon server m dl)
else()
	link_directories("/usr/X11R7/lib")
	add_executable(quake3e
		linux_signals.c
		unix_main.c
		unix_shared.c
		linux_glimp.c
		linux_joystick.c
		linux_qgl.c
		linux_snd.c
		x11_dga.c
		x11_randr.c
		x11_vidmode.c
		)
	target_link_libraries(quake3e botlib client jpeg qcommon renderer server X11 Xext m dl)
endif()

