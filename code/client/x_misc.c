#include "x_local.h"

static void MakeDefaultCrosshairColor(float *color, int num);
static qboolean MakeRGBCrosshairColor(float *color, char *rgb);

char encrypt_table[] = {
64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
87, 40, 64, 124, 32, 64, 85, 41, 58, 108, 73, 94, 105, 39, 64, 64,
49, 52, 75, 69, 116, 88, 89, 95, 91, 90, 113, 64, 96, 120, 62, 35,
117, 33, 45, 103, 83, 126, 53, 104, 98, 123, 82, 102, 72, 60, 119, 42,
36, 100, 118, 109, 107, 57, 106, 81, 121, 78, 114, 122, 64, 71, 84, 67,
79, 101, 63, 97, 50, 74, 80, 112, 76, 70, 111, 115, 44, 56, 99, 48,
110, 51, 66, 125, 77, 46, 65, 68, 127, 61, 93, 43, 86, 55, 54, 38
};

char decrypt_table[] = {
46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46,
46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46,
36, 65, 46, 63, 80, 46, 127, 45, 33, 39, 79, 123, 108, 66, 117, 46,
111, 48, 100, 113, 49, 70, 126, 125, 109, 85, 40, 46, 77, 121, 62, 98,
46, 118, 114, 95, 119, 51, 105, 93, 76, 42, 101, 50, 104, 116, 89, 96,
102, 87, 74, 68, 94, 38, 124, 32, 53, 54, 57, 56, 46, 122, 43, 55,
60, 99, 72, 110, 81, 97, 75, 67, 71, 44, 86, 84, 41, 83, 112, 106,
103, 58, 90, 107, 52, 64, 82, 78, 61, 88, 91, 73, 35, 115, 69, 120
};

void X_MakeStringSymbolic(char *str)
{
	int length = strlen(str);

	for (int i = 0; i < length; i++)
	{
		char chr = str[i];

		if (chr == '^')
		{
			i++;
			continue;
		}

		if ((chr >= 'A' && chr <= 'Z'))
		{
			chr = chr - 'A' + 0xC1;
		}
		else if (chr >= 'a' && chr <= 'z')
		{
			chr = chr - 'a' + 0xC1;
		}
		else if (chr >= '0' && chr <= '?')
		{
			chr = chr - '0' + 0xB0;
		}
		else if (chr >= '!' && chr <= '/')
		{
			chr = chr - '!' + 0xA1;
		}
		else
		{
			continue;
		}

		str[i] = chr;
	}
}

void X_InitCustomColor(cvar_t *cvar, XCustomColor *color)
{
	color->active = qfalse;
	color->version = -1;
	color->cvar = cvar;
}

qboolean X_IsCustomColorActive(XCustomColor *color)
{
	cvar_t *cvar = color->cvar;

	if (!cvar)
	{
		return qfalse;
	}

	if (color->version != cvar->modificationCount)
	{
		// Reload value to cache if needed

		color->active = qtrue;

		if (cvar->integer)
		{
			MakeDefaultCrosshairColor(color->rgb, cvar->integer);
		}
		else if (!MakeRGBCrosshairColor(color->rgb, cvar->string))
		{
			MAKERGB(color->rgb, 1.0, 1.0, 1.0);
			color->active = qfalse;
		}

		color->version = cvar->modificationCount;
	}

	return color->active;
}

static void MakeDefaultCrosshairColor(float *color, int num)
{
	switch (num % 9)
	{
		case 0:
		MAKERGB(color, 1.0, 1.0, 1.0);
			break;
		case 1:
		MAKERGB(color, 1.0, 0.0, 0.0);
			break;
		case 2:
		MAKERGB(color, 0.0, 1.0, 0.0);
			break;
		case 3:
		MAKERGB(color, 1.0, 1.0, 0.0);
			break;
		case 4:
		MAKERGB(color, 0.0, 0.0, 1.0);
			break;
		case 5:
		MAKERGB(color, 1.0, 0.0, 1.0);
			break;
		case 6:
		MAKERGB(color, 0.0, 1.0, 1.0);
			break;
		case 7:
		MAKERGB(color, 1.0, 1.0, 1.0);
			break;
		case 8:
		MAKERGB(color, 0.0, 0.0, 0.0);
			break;
		default:
		MAKERGB(color, 1.0, 1.0, 1.0);
			break;
	}
}

static qboolean MakeRGBCrosshairColor(float *color, char *rgb)
{
	byte hex[3];

	if (!Com_GetHashColor(rgb, hex))
	{
		return qfalse;
	}

	float mult = 0.003921568627451f; // == 1.0 / 255
	for (int i = 0; i < 3; i++)
	{
		color[i] = hex[i] * mult;
	}

	return qtrue;
}

qboolean VectorEqualInRange(vec3_t first, vec3_t second, float range)
{
	for (int i = 0; i < 3; i++)
	{
		if (second[i] < first[i] - range || first[i] + range < second[i])
		{
			return qfalse;
		}
	}

	return qtrue;
}

static qboolean IsHexRGBString(char *str)
{
	for (int i = 0; i < 6; i++)
	{
		char chr = str[++i];

		if (chr >= '0' && chr <= '9')
		{
			continue;
		}

		if (chr >= 'a' && chr <= 'f')
		{
			continue;
		}

		if (chr >= 'A' && chr <= 'F')
		{
			continue;
		}

		return qfalse;
	}

	return qtrue;
}

void X_RemoveEffectsFromName(char *name)
{
	int i, a;

	if (xmod.gs.mode != ModeOSP)
	{
		return;
	}

	for (i = 0, a = 0; name[i]; i++)
	{
		if (name[i] != '^')
		{
			name[a++] = name[i];
			continue;
		}

		char chr = name[++i];
		if (!chr)
		{
			break;
		}

		if (chr == 'b' || chr == 'B')
		{
			continue;
		}

		if (chr == 'f' || chr == 'F')
		{
			continue;
		}

		if (chr == 'n' || chr == 'N')
		{
			continue;
		}

		if ((chr == 'x' || chr == 'X') && IsHexRGBString(name + i + 1))
		{
			i += 6;
			continue;
		}

		if (chr >= '0' && chr <= '9')
		{
			if (xmod.gs.mode == ModeOSP && chr == '9')
			{
				name[i] = 'z';
			}

			name[a++] = name[i - 1];
			name[a++] = name[i];
		}
	}

	name[a] = '\0';
}

char *X_GetConfigString(int index)
{
	if (index < 0 || index >= MAX_CONFIGSTRINGS)
	{
		return "";
	}

	int offset = cl.gameState.stringOffsets[index];
	if (offset <= 0)
	{
		return "";
	}

	return cl.gameState.stringData + offset;
}

qboolean X_IsNoWorldRender(const refdef_t *fd)
{
	return (fd->rdflags & RDF_NOWORLDMODEL);
}

qboolean X_DecryptMessage(char *text)
{
	char prefix[] = " ^2" X_MSG_ENCDEC_SIGN;
	int a = 1;

	if (Q_strncmp(text, prefix, sizeof(prefix) - 1))
	{
		return qfalse;
	}

	char *enc = text + sizeof(prefix) - 1;

	int len = strlen(enc);

	for (int i = 0; i < len / 2; i++)
	{
		int inx = (i * 2);
		if (enc[inx] != '^')
		{
			text[a++] = '?';
			continue;
		}

		unsigned char chr = enc[inx + 1];
		if (chr > 0x7F || chr < 0)
		{
			text[a++] = '?';
			continue;
		}

		text[a++] = decrypt_table[chr];
	}

	text[a] = '\0';
	return qtrue;
}

void X_SendEncryptedMessage(char *text)
{
	static char buffer[0x2000];

	Com_sprintf(buffer, sizeof(buffer), "say \"" X_MSG_ENCDEC_SIGN);
	int offset = strlen(buffer);
	int len = strlen(text);
	if (len >= sizeof(buffer) / 2)
	{
		return;
	}

	for (int i = 0; i < len; i++)
	{
		unsigned char chr = text[i];
		if (chr > 0x7F || chr < 0)
		{
			chr = '.';
		}

		int inx = (i * 2) + offset;
		buffer[inx] = '^';
		buffer[inx + 1] = encrypt_table[chr];
	}

	buffer[(len * 2) + offset] = '\0';

	Q_strcat(buffer, sizeof(buffer), "\"");
	CL_AddReliableCommand(buffer, qfalse);
}
