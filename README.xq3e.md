# XQ3E

It's a modern Quake 3 engine based on improved Q3 engine https://github.com/ec-/Quake3e.


### Features

- Advanced crosshair customization
- Game\Weapon\Player customization
- Fixed game bugs
- Network improvements
- Experemental features
etc


### Installation

1. Download the latest XQ3E from the following page https://github.com/xq3e/engine/releases
2. Unzip XQ3E files
3. Put XQ3E files to a root of the existing Quake 3 folder
4. Execute XQ3E binary


### First steps

When you run XQ3E first time most of the feautres should be turned off. To enable recommended configuration type in a console 
```\exec xinit``` that will load a predefined config.

All XQ3E commands start with **x_** prefix and you are able to get a help for each one in a game. Just type a command without any parameter.


### X_HCK

XQ3E engine contains experemental features that come as part of the X_HCK module. This module is disabled by default but there are few ways to enable it for a server administrator.

**sv_cheats 1**

Turns on all X_HCK features

**sets x_hck 1**

Put it to a server-side configuration file or to a server console. It will send a **x_hck** configuration through ```\serverinfo```

**systeminfo cvar**

An alternative way to enable\disable **x_hck** is to put a x_hck cvar to a server code with flag ```Cvar_Get( "x_hck", "1", CVAR_SYSTEMINFO );```

**other x_hck commands**

Other commands that starts with a prefix ***x_hck_*** can be configured in the same way, for instance ```sets x_hck_dmg_draw 1``` enables x_hck_dmg_draw command on a client side event if x_hck is disabled.

### Disabling XQ3E

If an administrator doesn't want to allow playing with XQ3E features on a server he can turn off XQ3E by command ```sets x_enable 0```
