add_library(renderer STATIC
	tr_animation.c
	tr_arb.c
	tr_backend.c
	tr_bsp.c
	tr_cmds.c
	tr_curve.c
	tr_flares.c
	tr_font.c
	tr_image_bmp.c
	tr_image.c
	tr_image_jpg.c
	tr_image_pcx.c
	tr_image_png.c
	tr_image_tga.c
	tr_init.c
	tr_light.c
	tr_main.c
	tr_marks.c
	tr_mesh.c
	tr_model.c
	tr_model_iqm.c
	tr_noise.c
	tr_scene.c
	tr_shade.c
	tr_shade_calc.c
	tr_shader.c
	tr_shadows.c
	tr_sky.c
	tr_surface.c
	tr_vbo.c
	tr_world.c
	)

target_link_libraries(renderer jpeg qcommon)
