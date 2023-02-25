#ifndef CODE_X_LOCAL2_H
#define CODE_X_LOCAL2_H

/*********************************
 *  x_main.c
 *********************************/
void X_InitAfterCGameVM(void);
void X_StopAfterCGameVM(void);

void X_Event_OnGetSnapshot(snapshot_t* snapshot);
void X_Event_OnConfigstringModified(int index);
qboolean X_Event_OnServerCommand(const char* cmd, qboolean* result);
void X_Event_OnChatCommand(field_t* field);
void X_Event_OnDrawScreen(void);
sfxHandle_t X_Event_ReplaceSoundOnSoundStart(int entity, sfxHandle_t sound);
void X_Event_OnSoundStart(int entityNum, vec3_t origin, char* soundName);


qboolean X_Hook_CGame_Cvar_SetSafe(const char* var_name, const char* value);
void X_Hook_UpdateEntityPosition(int entityNum, const vec3_t origin);
int	X_Hook_FS_GetFileList(const char* path, const char* extension, char* listbuf, int bufsize);
void X_Hook_AddLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx);

/*********************************
 *  x_net.c
 *********************************/
qboolean X_Net_ShowCommands(void);

/*********************************
 *  x_hud.c
 *********************************/
void X_Hud_TurnOffForcedTransparency(void);
void X_Hud_TurnOnForcedTransparency(void);
#endif //CODE_X_LOCAL2_H
