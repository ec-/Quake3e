
xmod/gfx/2d/numbers/0_64a
{
  nopicmip
  {
    map gfx/2d/numbers/0_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/1_64a
{
  nopicmip
  {
    map gfx/2d/numbers/1_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/2_64a
{
  nopicmip
  {
    map gfx/2d/numbers/2_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/3_64a
{
  nopicmip
  {
    map gfx/2d/numbers/3_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/4_64a
{
  nopicmip
  {
    map gfx/2d/numbers/4_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/5_64a
{
  nopicmip
  {
    map gfx/2d/numbers/5_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/6_64a
{
  nopicmip
  {
    map gfx/2d/numbers/6_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/7_64a
{
  nopicmip
  {
    map gfx/2d/numbers/7_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/8_64a
{
  nopicmip
  {
    map gfx/2d/numbers/8_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/9_64a
{
  nopicmip
  {
    map gfx/2d/numbers/9_64a.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/2d/numbers/hit
{
  nopicmip
  {
    map gfx/2d/numbers/hit_48.tga
    blendFunc blend
    rgbgen vertex
  }
}


xmod/gfx/misc/crosshita
{
  nopicmip
  {
    map gfx/misc/crosshita.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/misc/crosshitb
{
  nopicmip
  {
    map gfx/misc/crosshitb.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/misc/crosshitc
{
  nopicmip
  {
    map gfx/misc/crosshitc.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/gfx/misc/crosshitd
{
  nopicmip
  {
    map gfx/misc/crosshitd.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/sprites/xfoe1
{
  nomipmaps
  nopicmip
  {
    map sprites/xfoe1.tga
    blendFunc blend
    rgbgen vertex
  }
}
xmod/sprites/xfoe2
{
  nomipmaps
  nopicmip
  {
    map sprites/xfoe2.tga
    blendFunc blend
    rgbgen vertex
  }
}

xmod/sprites/xfoe-unfreeze
{
  nomipmaps
  nopicmip
  {
    map sprites/xfoe-unfreeze.tga
    blendFunc blend
    rgbgen vertex
  }
}


xmod/freeze/red
{
  deformvertexes wave 100 sin 3 0 0 0
  {
    map textures/effects/xenvmap-freeze.tga
    blendfunc gl_one gl_one
    tcgen environment
    rgbgen const ( 1.0 1.0 1.0 )
  }
}

xmod/freeze/blue
{
  deformvertexes wave 100 sin 3 0 0 0
  {
    map textures/effects/xenvmap-freeze.tga
    blendfunc gl_one gl_one
    tcgen environment
    rgbgen const ( 1.0 1.0 1.0 )
  }
}

// HitBoxes

xmod/hitbox-2d
{
  sort Additive

  nomipmaps
  nopicmip
  deformvertexes autosprite
  {
    map sprites/hitbox-2d.tga

    blendFunc blend
    rgbGen entity
  }
}

sprites/hitbox-3d
{
  deformvertexes wave 100 sin 1 0 0 0
  {
    map sprites/hitbox-3d.tga
    blendfunc gl_one gl_one
    tcgen environment
    rgbgen const ( 1.0 1.0 1.0 )
  }
}

xmod/gfx/bigchars_16
{
	nopicmip
	nomipmaps
	{
		map gfx/2d/xbigchars_16.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}

xmod/gfx/bigchars_32
{
	nopicmip
	nomipmaps
	{
		map gfx/2d/xbigchars_32.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}

xmod/gfx/bigchars_64
{
	nopicmip
	nomipmaps
	{
		map gfx/2d/xbigchars_64.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}

xmod/gfx/xbigchars
{
	nopicmip
	nomipmaps
	{
		map gfx/2d/xbigchars_ariel.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}

xmod/gfx/xoverlaychars
{
	nopicmip
	nomipmaps
	{
		map gfx/2d/xbigchars_kill.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}

xgfx/icon/railgun
{
	nopicmip
	nomipmaps
	{
		map icons/iconw_railgun.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}

xgfx/icon/lightning
{
	nopicmip
	nomipmaps
	{
		map icons/iconw_lightning.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbgen vertex
	}
}
