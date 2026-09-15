/* Compile-only wire layout contract, using the actual renderer definition. */
#include "../../code/renderercommon/tr_image_png.c"
_Static_assert(sizeof(struct PNG_ChunkHeader) == PNG_ChunkHeader_Size, "PNG chunk header size");
_Static_assert(_Alignof(struct PNG_ChunkHeader) == 1, "PNG chunk header alignment");
