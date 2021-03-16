
# Quake3 Unix Makefile
#
# Nov '98 by Zoid <zoid@idsoftware.com>
#
# Loki Hacking by Bernd Kreimeier
#  and a little more by Ryan C. Gordon.
#  and a little more by Rafael Barrero
#  and a little more by the ioq3 cr3w
#
# GNU Make required
#
COMPILE_PLATFORM=$(shell uname | sed -e 's/_.*//' | tr '[:upper:]' '[:lower:]' | sed -e 's/\//_/g')
COMPILE_ARCH=$(shell uname -m | sed -e 's/i.86/x86/' | sed -e 's/^arm.*/arm/')

ifeq ($(shell uname -m),arm64)
  COMPILE_ARCH=aarch64
endif

ifeq ($(COMPILE_PLATFORM),mingw32)
  ifeq ($(COMPILE_ARCH),i386)
    COMPILE_ARCH=x86
  endif
endif

BUILD_CLIENT     = 1
BUILD_SERVER     = 1

USE_SDL          = 1
USE_CURL         = 1
USE_LOCAL_HEADERS= 0
USE_VULKAN       = 0
USE_SYSTEM_JPEG  = 0
USE_VULKAN_API   = 1

USE_RENDERER_DLOPEN = 1

CNAME            = quake3e
DNAME            = quake3e.ded

RENDERER_PREFIX  = $(CNAME)

ifeq ($(V),1)
echo_cmd=@:
Q=
else
echo_cmd=@echo
Q=@
endif

#############################################################################
#
# If you require a different configuration from the defaults below, create a
# new file named "Makefile.local" in the same directory as this file and define
# your parameters there. This allows you to change configuration without
# causing problems with keeping up to date with the repository.
#
#############################################################################
-include Makefile.local

ifeq ($(COMPILE_PLATFORM),darwin)
  USE_SDL=1
endif

ifeq ($(COMPILE_PLATFORM),cygwin)
  PLATFORM=mingw32
endif

ifndef PLATFORM
PLATFORM=$(COMPILE_PLATFORM)
endif
export PLATFORM

ifeq ($(PLATFORM),mingw32)
  MINGW=1
endif
ifeq ($(PLATFORM),mingw64)
  MINGW=1
endif

ifeq ($(COMPILE_ARCH),i86pc)
  COMPILE_ARCH=x86
endif

ifeq ($(COMPILE_ARCH),amd64)
  COMPILE_ARCH=x86_64
endif
ifeq ($(COMPILE_ARCH),x64)
  COMPILE_ARCH=x86_64
endif

ifndef ARCH
ARCH=$(COMPILE_ARCH)
endif
export ARCH

ifneq ($(PLATFORM),$(COMPILE_PLATFORM))
  CROSS_COMPILING=1
else
  CROSS_COMPILING=0

  ifneq ($(ARCH),$(COMPILE_ARCH))
    CROSS_COMPILING=1
  endif
endif
export CROSS_COMPILING

ifndef DESTDIR
DESTDIR=/usr/local/games/quake3
endif

ifndef MOUNT_DIR
MOUNT_DIR=code
endif

ifndef BUILD_DIR
BUILD_DIR=build
endif

ifndef GENERATE_DEPENDENCIES
GENERATE_DEPENDENCIES=1
endif

ifndef USE_CCACHE
USE_CCACHE=0
endif
export USE_CCACHE

ifndef USE_CODEC_VORBIS
USE_CODEC_VORBIS=0
endif

ifndef USE_LOCAL_HEADERS
USE_LOCAL_HEADERS=1
endif

ifndef USE_CURL
USE_CURL=1
endif

ifndef USE_CURL_DLOPEN
  ifdef MINGW
    USE_CURL_DLOPEN=0
  else
    USE_CURL_DLOPEN=1
  endif
endif

ifneq ($(USE_RENDERER_DLOPEN),0)
USE_VULKAN=1
endif

ifneq ($(USE_VULKAN),0)
USE_VULKAN_API=1
endif


#############################################################################

BD=$(BUILD_DIR)/debug-$(PLATFORM)-$(ARCH)
BR=$(BUILD_DIR)/release-$(PLATFORM)-$(ARCH)
ADIR=$(MOUNT_DIR)/asm
CDIR=$(MOUNT_DIR)/client
SDIR=$(MOUNT_DIR)/server
RCDIR=$(MOUNT_DIR)/renderercommon
R1DIR=$(MOUNT_DIR)/renderer
RVDIR=$(MOUNT_DIR)/renderervk
SDLDIR=$(MOUNT_DIR)/sdl

CMDIR=$(MOUNT_DIR)/qcommon
UDIR=$(MOUNT_DIR)/unix
W32DIR=$(MOUNT_DIR)/win32
BLIBDIR=$(MOUNT_DIR)/botlib
UIDIR=$(MOUNT_DIR)/ui
JPDIR=$(MOUNT_DIR)/libjpeg

bin_path=$(shell which $(1) 2> /dev/null)

STRIP ?= strip
PKG_CONFIG ?= pkg-config
INSTALL=install
MKDIR=mkdir

ifneq ($(call bin_path, $(PKG_CONFIG)),)
  SDL_INCLUDE ?= $(shell $(PKG_CONFIG) --silence-errors --cflags-only-I sdl2)
  SDL_LIBS ?= $(shell $(PKG_CONFIG) --silence-errors --libs sdl2)
  X11_INCLUDE ?= $(shell $(PKG_CONFIG) --silence-errors --cflags-only-I x11)
  X11_LIBS ?= $(shell $(PKG_CONFIG) --silence-errors --libs x11)
endif

# supply some reasonable defaults for SDL/X11?
ifeq ($(X11_INCLUDE),)
X11_INCLUDE = -I/usr/X11R6/include
endif
ifeq ($(X11_LIBS),)
X11_LIBS = -lX11
endif
ifeq ($(SDL_LIBS),)
SDL_LIBS = -lSDL2
endif

# extract version info
VERSION=$(shell grep "\#define Q3_VERSION" $(CMDIR)/q_shared.h | \
  sed -e 's/.*".* \([^ ]*\)"/\1/')

# common qvm definition
ifeq ($(ARCH),x86_64)
  HAVE_VM_COMPILED = true
else
ifeq ($(ARCH),x86)
  HAVE_VM_COMPILED = true
else
  HAVE_VM_COMPILED = false
endif
endif

ifeq ($(ARCH),arm)
  HAVE_VM_COMPILED = true
endif
ifeq ($(ARCH),aarch64)
  HAVE_VM_COMPILED = true
endif

BASE_CFLAGS =

ifeq ($(USE_SYSTEM_JPEG),1)
  BASE_CFLAGS += -DUSE_SYSTEM_JPEG
endif

ifneq ($(HAVE_VM_COMPILED),true)
  BASE_CFLAGS += -DNO_VM_COMPILED
endif

ifneq ($(USE_RENDERER_DLOPEN),0)
  BASE_CFLAGS += -DUSE_RENDERER_DLOPEN
  BASE_CFLAGS += -DRENDERER_PREFIX=\\\"$(RENDERER_PREFIX)\\\"
endif

ifeq ($(USE_CODEC_VORBIS),1)
  BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
endif

ifdef DEFAULT_BASEDIR
  BASE_CFLAGS += -DDEFAULT_BASEDIR=\\\"$(DEFAULT_BASEDIR)\\\"
endif

ifeq ($(USE_LOCAL_HEADERS),1)
  BASE_CFLAGS += -DUSE_LOCAL_HEADERS=1
endif

ifeq ($(USE_CURL),1)
  BASE_CFLAGS += -DUSE_CURL
  ifeq ($(USE_CURL_DLOPEN),1)
    BASE_CFLAGS += -DUSE_CURL_DLOPEN
  else
    ifeq ($(MINGW),1)
      BASE_CFLAGS += -DCURL_STATICLIB
    endif
  endif
endif

ifeq ($(USE_VULKAN_API),1)
  BASE_CFLAGS += -DUSE_VULKAN_API
endif

ifeq ($(GENERATE_DEPENDENCIES),1)
  BASE_CFLAGS += -MMD
endif


ARCHEXT=

CLIENT_EXTRA_FILES=


#############################################################################
# SETUP AND BUILD -- MINGW32
#############################################################################

ifdef MINGW

  ifeq ($(CROSS_COMPILING),1)
    # If CC is already set to something generic, we probably want to use
    # something more specific
    ifneq ($(findstring $(strip $(CC)),cc gcc),)
      CC=
    endif

    # We need to figure out the correct gcc and windres
    ifeq ($(ARCH),x86_64)
      MINGW_PREFIXES=x86_64-w64-mingw32 amd64-mingw32msvc
      STRIP=x86_64-w64-mingw32-strip
    endif
    ifeq ($(ARCH),x86)
      MINGW_PREFIXES=i686-w64-mingw32 i586-mingw32msvc i686-pc-mingw32
    endif

    ifndef CC
      CC=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
         $(call bin_path, $(MINGW_PREFIX)-gcc))))
    endif

#   STRIP=$(MINGW_PREFIX)-strip -g

    ifndef WINDRES
      WINDRES=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
         $(call bin_path, $(MINGW_PREFIX)-windres))))
    endif
  else
    # Some MinGW installations define CC to cc, but don't actually provide cc,
    # so check that CC points to a real binary and use gcc if it doesn't
    ifeq ($(call bin_path, $(CC)),)
      CC=gcc
    endif

  endif

  # using generic windres if specific one is not present
  ifeq ($(WINDRES),)
    WINDRES=windres
  endif

  ifeq ($(CC),)
    $(error Cannot find a suitable cross compiler for $(PLATFORM))
  endif

  BASE_CFLAGS += -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
    -DUSE_ICON -DMINGW=1

  BASE_CFLAGS += -Wno-unused-result

  ifeq ($(ARCH),x86_64)
    ARCHEXT = .x64
    BASE_CFLAGS += -m64
    OPTIMIZE = -O2 -ffast-math -fstrength-reduce
  endif
  ifeq ($(ARCH),x86)
    BASE_CFLAGS += -m32
    OPTIMIZE = -O2 -march=i586 -mtune=i686 -ffast-math -fstrength-reduce
  endif

  SHLIBEXT = dll
  SHLIBCFLAGS = -fPIC -fvisibility=hidden
  SHLIBLDFLAGS = -shared $(LDFLAGS)

  BINEXT = .exe

  LDFLAGS = -mwindows -Wl,--dynamicbase -Wl,--nxcompat  -fvisibility=hidden
  LDFLAGS += -lwsock32 -lgdi32 -lwinmm -lole32 -lws2_32 -lpsapi -lcomctl32

  CLIENT_LDFLAGS=$(LDFLAGS)

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += -DUSE_LOCAL_HEADERS=1 -I$(MOUNT_DIR)/libsdl/windows/include/SDL2
    #CLIENT_CFLAGS += -DUSE_LOCAL_HEADERS=1
    ifeq ($(ARCH),x86)
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/libsdl/windows/mingw/lib32
      CLIENT_LDFLAGS += -lSDL2
      CLIENT_EXTRA_FILES += $(MOUNT_DIR)/libsdl/windows/mingw/lib32/SDL2.dll
    else
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/libsdl/windows/mingw/lib64
      CLIENT_LDFLAGS += -lSDL264
      CLIENT_EXTRA_FILES += $(MOUNT_DIR)/libsdl/windows/mingw/lib64/SDL264.dll
    endif
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

  ifeq ($(USE_CURL),1)
    BASE_CFLAGS += -I$(MOUNT_DIR)/libcurl/windows/include
    ifeq ($(ARCH),x86)
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/libcurl/windows/mingw/lib32
    else
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/libcurl/windows/mingw/lib64
    endif
    CLIENT_LDFLAGS += -lcurl -lwldap32 -lcrypt32
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -DDEBUG -D_DEBUG -g -O0
  RELEASE_CFLAGS = $(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

else # !MINGW

ifeq ($(COMPILE_PLATFORM),darwin)

#############################################################################
# SETUP AND BUILD -- MACOS
#############################################################################

  BASE_CFLAGS += -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes -pipe

  BASE_CFLAGS += -Wno-unused-result

  OPTIMIZE = -O2 -fvisibility=hidden

  SHLIBEXT = dylib
  SHLIBCFLAGS = -fPIC -fvisibility=hidden
  SHLIBLDFLAGS = -dynamiclib $(LDFLAGS)

  LDFLAGS =

  ifneq ($(SDL_INCLUDE),)
    BASE_CFLAGS += $(SDL_INCLUDE)
    CLIENT_LDFLAGS = $(SDL_LIBS)
  else
    BASE_CFLAGS += -I/Library/Frameworks/SDL2.framework/Headers
    CLIENT_LDFLAGS = -F/Library/Frameworks -framework SDL2
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -DDEBUG -D_DEBUG -g -O0
  RELEASE_CFLAGS = $(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

else

#############################################################################
# SETUP AND BUILD -- *NIX PLATFORMS
#############################################################################

  BASE_CFLAGS += -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes -pipe

  BASE_CFLAGS += -Wno-unused-result

  BASE_CFLAGS += -DUSE_ICON

  BASE_CFLAGS += -I/usr/include -I/usr/local/include

  OPTIMIZE = -O2 -fvisibility=hidden

  ifeq ($(ARCH),x86_64)
    ARCHEXT = .x64
  else
  ifeq ($(ARCH),x86)
    OPTIMIZE += -march=i586 -mtune=i686
  endif
  endif

  ifeq ($(ARCH),arm)
    OPTIMIZE += -march=armv7-a
    ARCHEXT = .arm
  endif

  ifeq ($(ARCH),aarch64)
    ARCHEXT = .aarch64
  endif

  SHLIBEXT = so
  SHLIBCFLAGS = -fPIC -fvisibility=hidden
  SHLIBLDFLAGS = -shared $(LDFLAGS)

  LDFLAGS = -lm

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += $(SDL_INCLUDE)
    CLIENT_LDFLAGS = $(SDL_LIBS)
  else
    BASE_CFLAGS += $(X11_INCLUDE)
    CLIENT_LDFLAGS = $(X11_LIBS)
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

  ifeq ($(USE_SYSTEM_JPEG),1)
    CLIENT_LDFLAGS += -ljpeg
  endif

  ifeq ($(USE_CURL),1)
    ifeq ($(USE_CURL_DLOPEN),0)
      CLIENT_LDFLAGS += -lcurl
    endif
  endif

  ifeq ($(PLATFORM),linux)
    LDFLAGS += -ldl -Wl,--hash-style=both
    ifeq ($(ARCH),x86)
      # linux32 make ...
      BASE_CFLAGS += -m32
      LDFLAGS += -m32
    endif
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -DDEBUG -D_DEBUG -g -O0
  RELEASE_CFLAGS = $(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  DEBUG_LDFLAGS = -rdynamic

endif # *NIX platforms

endif # !MINGW


TARGET_CLIENT = $(CNAME)$(ARCHEXT)$(BINEXT)

TARGET_REND1 = $(RENDERER_PREFIX)_opengl_$(SHLIBNAME)
TARGET_RENDV = $(RENDERER_PREFIX)_vulkan_$(SHLIBNAME)

TARGET_SERVER = $(DNAME)$(ARCHEXT)$(BINEXT)

TARGETS =

ifneq ($(BUILD_SERVER),0)
  TARGETS += $(B)/$(TARGET_SERVER)
endif

ifneq ($(BUILD_CLIENT),0)
  TARGETS += $(B)/$(TARGET_CLIENT)
  ifneq ($(USE_RENDERER_DLOPEN),0)
    TARGETS += $(B)/$(TARGET_REND1)
    TARGETS += $(B)/$(TARGET_RENDV)
  endif
endif

ifeq ($(USE_CCACHE),1)
  CC := ccache $(CC)
endif

ifneq ($(USE_RENDERER_DLOPEN),0)
    RENDCFLAGS=$(SHLIBCFLAGS)
else
    RENDCFLAGS=$(NOTSHLIBCFLAGS)
endif

define DO_CC
$(echo_cmd) "CC $<"
$(Q)$(CC) $(NOTSHLIBCFLAGS) $(CFLAGS) -o $@ -c $<
endef

define DO_REND_CC
$(echo_cmd) "REND_CC $<"
$(Q)$(CC) $(RENDCFLAGS) $(CFLAGS) -o $@ -c $<
endef

define DO_BOT_CC
$(echo_cmd) "BOT_CC $<"
$(Q)$(CC) $(NOTSHLIBCFLAGS) $(CFLAGS) $(BOTCFLAGS) -DBOTLIB -o $@ -c $<
endef

ifeq ($(GENERATE_DEPENDENCIES),1)
  DO_QVM_DEP=cat $(@:%.o=%.d) | sed -e 's/\.o/\.asm/g' >> $(@:%.o=%.d)
endif

define DO_SHLIB_CC
$(echo_cmd) "SHLIB_CC $<"
$(Q)$(CC) $(CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<
$(Q)$(DO_QVM_DEP)
endef

define DO_SHLIB_CC_MISSIONPACK
$(echo_cmd) "SHLIB_CC_MISSIONPACK $<"
$(Q)$(CC) -DMISSIONPACK $(CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<
$(Q)$(DO_QVM_DEP)
endef

define DO_AS
$(echo_cmd) "AS $<"
$(Q)$(CC) $(CFLAGS) -DELF -x assembler-with-cpp -o $@ -c $<
endef

define DO_DED_CC
$(echo_cmd) "DED_CC $<"
$(Q)$(CC) $(NOTSHLIBCFLAGS) -DDEDICATED $(CFLAGS) -o $@ -c $<
endef

define DO_WINDRES
$(echo_cmd) "WINDRES $<"
$(Q)$(WINDRES) -i $< -o $@
endef

ifndef SHLIBNAME
  SHLIBNAME=$(ARCH).$(SHLIBEXT)
endif

#############################################################################
# MAIN TARGETS
#############################################################################

default: release
all: debug release

debug:
	@$(MAKE) targets B=$(BD) CFLAGS="$(CFLAGS) $(DEBUG_CFLAGS)" LDFLAGS="$(LDFLAGS) $(DEBUG_LDFLAGS)" V=$(V)

release:
	@$(MAKE) targets B=$(BR) CFLAGS="$(CFLAGS) $(RELEASE_CFLAGS)" V=$(V)

define ADD_COPY_TARGET
TARGETS += $2
$2: $1
	$(echo_cmd) "CP $$<"
	@cp $1 $2
endef

# These functions allow us to generate rules for copying a list of files
# into the base directory of the build; this is useful for bundling libs,
# README files or whatever else
define GENERATE_COPY_TARGETS
$(foreach FILE,$1, \
  $(eval $(call ADD_COPY_TARGET, \
    $(FILE), \
    $(addprefix $(B)/,$(notdir $(FILE))))))
endef

ifneq ($(BUILD_CLIENT),0)
  $(call GENERATE_COPY_TARGETS,$(CLIENT_EXTRA_FILES))
endif

# Create the build directories and tools, print out
# an informational message, then start building
targets: makedirs tools
	@echo ""
	@echo "Building quake3 in $(B):"
	@echo ""
	@echo "  VERSION: $(VERSION)"
	@echo "  PLATFORM: $(PLATFORM)"
	@echo "  ARCH: $(ARCH)"
	@echo "  COMPILE_PLATFORM: $(COMPILE_PLATFORM)"
	@echo "  COMPILE_ARCH: $(COMPILE_ARCH)"
ifdef MINGW
	@echo "  WINDRES: $(WINDRES)"
endif
	@echo "  CC: $(CC)"
	@echo ""
	@echo "  CFLAGS:"
	@for i in $(CFLAGS); \
	do \
		echo "    $$i"; \
	done
	@echo ""
	@echo "  Output:"
	@for i in $(TARGETS); \
	do \
		echo "    $$i"; \
	done
	@echo ""
ifneq ($(TARGETS),)
	@$(MAKE) $(TARGETS) V=$(V)
endif

makedirs:
	@if [ ! -d $(BUILD_DIR) ];then $(MKDIR) $(BUILD_DIR);fi
	@if [ ! -d $(B) ];then $(MKDIR) $(B);fi
	@if [ ! -d $(B)/client ];then $(MKDIR) $(B)/client;fi
	@if [ ! -d $(B)/rend1 ];then $(MKDIR) $(B)/rend1;fi
	@if [ ! -d $(B)/rendv ];then $(MKDIR) $(B)/rendv;fi
	@if [ ! -d $(B)/ded ];then $(MKDIR) $(B)/ded;fi

#############################################################################
# CLIENT/SERVER
#############################################################################

Q3REND1OBJ = \
  $(B)/rend1/tr_animation.o \
  $(B)/rend1/tr_arb.o \
  $(B)/rend1/tr_backend.o \
  $(B)/rend1/tr_bsp.o \
  $(B)/rend1/tr_cmds.o \
  $(B)/rend1/tr_curve.o \
  $(B)/rend1/tr_flares.o \
  $(B)/rend1/tr_font.o \
  $(B)/rend1/tr_image.o \
  $(B)/rend1/tr_image_png.o \
  $(B)/rend1/tr_image_jpg.o \
  $(B)/rend1/tr_image_bmp.o \
  $(B)/rend1/tr_image_tga.o \
  $(B)/rend1/tr_image_pcx.o \
  $(B)/rend1/tr_init.o \
  $(B)/rend1/tr_light.o \
  $(B)/rend1/tr_main.o \
  $(B)/rend1/tr_marks.o \
  $(B)/rend1/tr_mesh.o \
  $(B)/rend1/tr_model.o \
  $(B)/rend1/tr_model_iqm.o \
  $(B)/rend1/tr_noise.o \
  $(B)/rend1/tr_scene.o \
  $(B)/rend1/tr_shade.o \
  $(B)/rend1/tr_shade_calc.o \
  $(B)/rend1/tr_shader.o \
  $(B)/rend1/tr_shadows.o \
  $(B)/rend1/tr_sky.o \
  $(B)/rend1/tr_surface.o \
  $(B)/rend1/tr_vbo.o \
  $(B)/rend1/tr_world.o

ifneq ($(USE_RENDERER_DLOPEN), 0)
  Q3REND1OBJ += \
    $(B)/rend1/q_shared.o \
    $(B)/rend1/puff.o \
    $(B)/rend1/q_math.o
endif

Q3RENDVOBJ = \
  $(B)/rendv/tr_animation.o \
  $(B)/rendv/tr_backend.o \
  $(B)/rendv/tr_bsp.o \
  $(B)/rendv/tr_cmds.o \
  $(B)/rendv/tr_curve.o \
  $(B)/rendv/tr_font.o \
  $(B)/rendv/tr_image.o \
  $(B)/rendv/tr_image_png.o \
  $(B)/rendv/tr_image_jpg.o \
  $(B)/rendv/tr_image_bmp.o \
  $(B)/rendv/tr_image_tga.o \
  $(B)/rendv/tr_image_pcx.o \
  $(B)/rendv/tr_init.o \
  $(B)/rendv/tr_light.o \
  $(B)/rendv/tr_main.o \
  $(B)/rendv/tr_marks.o \
  $(B)/rendv/tr_mesh.o \
  $(B)/rendv/tr_model.o \
  $(B)/rendv/tr_model_iqm.o \
  $(B)/rendv/tr_noise.o \
  $(B)/rendv/tr_scene.o \
  $(B)/rendv/tr_shade.o \
  $(B)/rendv/tr_shade_calc.o \
  $(B)/rendv/tr_shader.o \
  $(B)/rendv/tr_shadows.o \
  $(B)/rendv/tr_sky.o \
  $(B)/rendv/tr_surface.o \
  $(B)/rendv/tr_world.o \
  $(B)/rendv/vk.o \
  $(B)/rendv/vk_flares.o \
  $(B)/rendv/vk_vbo.o \

ifneq ($(USE_RENDERER_DLOPEN), 0)
  Q3RENDVOBJ += \
    $(B)/rend1/q_shared.o \
    $(B)/rendv/puff.o \
    $(B)/rendv/q_math.o
endif

JPGOBJ = \
  $(B)/client/jaricom.o \
  $(B)/client/jcapimin.o \
  $(B)/client/jcapistd.o \
  $(B)/client/jcarith.o \
  $(B)/client/jccoefct.o  \
  $(B)/client/jccolor.o \
  $(B)/client/jcdctmgr.o \
  $(B)/client/jchuff.o   \
  $(B)/client/jcinit.o \
  $(B)/client/jcmainct.o \
  $(B)/client/jcmarker.o \
  $(B)/client/jcmaster.o \
  $(B)/client/jcomapi.o \
  $(B)/client/jcparam.o \
  $(B)/client/jcprepct.o \
  $(B)/client/jcsample.o \
  $(B)/client/jctrans.o \
  $(B)/client/jdapimin.o \
  $(B)/client/jdapistd.o \
  $(B)/client/jdarith.o \
  $(B)/client/jdatadst.o \
  $(B)/client/jdatasrc.o \
  $(B)/client/jdcoefct.o \
  $(B)/client/jdcolor.o \
  $(B)/client/jddctmgr.o \
  $(B)/client/jdhuff.o \
  $(B)/client/jdinput.o \
  $(B)/client/jdmainct.o \
  $(B)/client/jdmarker.o \
  $(B)/client/jdmaster.o \
  $(B)/client/jdmerge.o \
  $(B)/client/jdpostct.o \
  $(B)/client/jdsample.o \
  $(B)/client/jdtrans.o \
  $(B)/client/jerror.o \
  $(B)/client/jfdctflt.o \
  $(B)/client/jfdctfst.o \
  $(B)/client/jfdctint.o \
  $(B)/client/jidctflt.o \
  $(B)/client/jidctfst.o \
  $(B)/client/jidctint.o \
  $(B)/client/jmemmgr.o \
  $(B)/client/jmemnobs.o \
  $(B)/client/jquant1.o \
  $(B)/client/jquant2.o \
  $(B)/client/jutils.o

Q3OBJ = \
  $(B)/client/cl_cgame.o \
  $(B)/client/cl_cin.o \
  $(B)/client/cl_console.o \
  $(B)/client/cl_input.o \
  $(B)/client/cl_keys.o \
  $(B)/client/cl_main.o \
  $(B)/client/cl_net_chan.o \
  $(B)/client/cl_parse.o \
  $(B)/client/cl_scrn.o \
  $(B)/client/cl_ui.o \
  $(B)/client/cl_avi.o \
  $(B)/client/cl_jpeg.o \
  \
  $(B)/client/cm_load.o \
  $(B)/client/cm_patch.o \
  $(B)/client/cm_polylib.o \
  $(B)/client/cm_test.o \
  $(B)/client/cm_trace.o \
  \
  $(B)/client/cmd.o \
  $(B)/client/common.o \
  $(B)/client/cvar.o \
  $(B)/client/files.o \
  $(B)/client/history.o \
  $(B)/client/keys.o \
  $(B)/client/md4.o \
  $(B)/client/md5.o \
  $(B)/client/msg.o \
  $(B)/client/net_chan.o \
  $(B)/client/net_ip.o \
  $(B)/client/huffman.o \
  $(B)/client/huffman_static.o \
  \
  $(B)/client/snd_adpcm.o \
  $(B)/client/snd_dma.o \
  $(B)/client/snd_mem.o \
  $(B)/client/snd_mix.o \
  $(B)/client/snd_wavelet.o \
  \
  $(B)/client/snd_main.o \
  $(B)/client/snd_codec.o \
  $(B)/client/snd_codec_wav.o \
  $(B)/client/snd_codec_ogg.o \
  \
  $(B)/client/sv_bot.o \
  $(B)/client/sv_ccmds.o \
  $(B)/client/sv_client.o \
  $(B)/client/sv_filter.o \
  $(B)/client/sv_game.o \
  $(B)/client/sv_init.o \
  $(B)/client/sv_main.o \
  $(B)/client/sv_net_chan.o \
  $(B)/client/sv_snapshot.o \
  $(B)/client/sv_world.o \
  \
  $(B)/client/q_math.o \
  $(B)/client/q_shared.o \
  \
  $(B)/client/unzip.o \
  $(B)/client/puff.o \
  $(B)/client/vm.o \
  $(B)/client/vm_interpreted.o \
  \
  $(B)/client/be_aas_bspq3.o \
  $(B)/client/be_aas_cluster.o \
  $(B)/client/be_aas_debug.o \
  $(B)/client/be_aas_entity.o \
  $(B)/client/be_aas_file.o \
  $(B)/client/be_aas_main.o \
  $(B)/client/be_aas_move.o \
  $(B)/client/be_aas_optimize.o \
  $(B)/client/be_aas_reach.o \
  $(B)/client/be_aas_route.o \
  $(B)/client/be_aas_routealt.o \
  $(B)/client/be_aas_sample.o \
  $(B)/client/be_ai_char.o \
  $(B)/client/be_ai_chat.o \
  $(B)/client/be_ai_gen.o \
  $(B)/client/be_ai_goal.o \
  $(B)/client/be_ai_move.o \
  $(B)/client/be_ai_weap.o \
  $(B)/client/be_ai_weight.o \
  $(B)/client/be_ea.o \
  $(B)/client/be_interface.o \
  $(B)/client/l_crc.o \
  $(B)/client/l_libvar.o \
  $(B)/client/l_log.o \
  $(B)/client/l_memory.o \
  $(B)/client/l_precomp.o \
  $(B)/client/l_script.o \
  $(B)/client/l_struct.o

ifneq ($(USE_SYSTEM_JPEG),1)
  Q3OBJ += $(JPGOBJ)
endif

ifneq ($(USE_RENDERER_DLOPEN),1)
  ifeq ($(USE_VULKAN),1)
    Q3OBJ += $(Q3RENDVOBJ)
  else
    Q3OBJ += $(Q3REND1OBJ)
  endif
endif

ifeq ($(ARCH),x86)
ifndef MINGW
  Q3OBJ += \
    $(B)/client/snd_mix_mmx.o \
    $(B)/client/snd_mix_sse.o
endif
endif

ifeq ($(HAVE_VM_COMPILED),true)
  ifeq ($(ARCH),x86)
    Q3OBJ += $(B)/client/vm_x86.o
  endif
  ifeq ($(ARCH),x86_64)
    Q3OBJ += $(B)/client/vm_x86.o
  endif
  ifeq ($(ARCH),arm)
    Q3OBJ += $(B)/client/vm_armv7l.o
  endif
  ifeq ($(ARCH),aarch64)
    Q3OBJ += $(B)/client/vm_aarch64.o
  endif
endif

ifeq ($(USE_CURL),1)
  Q3OBJ += $(B)/client/cl_curl.o
endif

ifdef MINGW

  Q3OBJ += \
    $(B)/client/win_main.o \
    $(B)/client/win_shared.o \
    $(B)/client/win_syscon.o \
    $(B)/client/win_resource.o

ifeq ($(USE_SDL),1)
    Q3OBJ += \
        $(B)/client/sdl_glimp.o \
        $(B)/client/sdl_gamma.o \
        $(B)/client/sdl_input.o \
        $(B)/client/sdl_snd.o
else # !USE_SDL
    Q3OBJ += \
        $(B)/client/win_gamma.o \
        $(B)/client/win_glimp.o \
        $(B)/client/win_input.o \
        $(B)/client/win_minimize.o \
        $(B)/client/win_qgl.o \
        $(B)/client/win_snd.o \
        $(B)/client/win_wndproc.o
ifeq ($(USE_VULKAN_API),1)
    Q3OBJ += \
        $(B)/client/win_qvk.o
endif
endif # !USE_SDL

else # !MINGW

  Q3OBJ += \
    $(B)/client/unix_main.o \
    $(B)/client/unix_shared.o \
    $(B)/client/linux_signals.o

ifeq ($(USE_SDL),1)
    Q3OBJ += \
        $(B)/client/sdl_glimp.o \
        $(B)/client/sdl_gamma.o \
        $(B)/client/sdl_input.o \
        $(B)/client/sdl_snd.o
else # !USE_SDL
    Q3OBJ += \
        $(B)/client/linux_glimp.o \
        $(B)/client/linux_qgl.o \
        $(B)/client/linux_snd.o \
        $(B)/client/x11_dga.o \
        $(B)/client/x11_randr.o \
        $(B)/client/x11_vidmode.o
ifeq ($(USE_VULKAN_API),1)
    Q3OBJ += \
        $(B)/client/linux_qvk.o
endif
endif # !USE_SDL

endif # !MINGW

# client binary

$(B)/$(TARGET_CLIENT): $(Q3OBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3OBJ) $(CLIENT_LDFLAGS) \
		$(LDFLAGS)

# modular renderers

$(B)/$(TARGET_REND1): $(Q3REND1OBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3REND1OBJ) $(SHLIBCFLAGS) $(SHLIBLDFLAGS)

$(B)/$(TARGET_RENDV): $(Q3RENDVOBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3RENDVOBJ) $(SHLIBCFLAGS) $(SHLIBLDFLAGS)

#############################################################################
# DEDICATED SERVER
#############################################################################

Q3DOBJ = \
  $(B)/ded/sv_bot.o \
  $(B)/ded/sv_client.o \
  $(B)/ded/sv_ccmds.o \
  $(B)/ded/sv_filter.o \
  $(B)/ded/sv_game.o \
  $(B)/ded/sv_init.o \
  $(B)/ded/sv_main.o \
  $(B)/ded/sv_net_chan.o \
  $(B)/ded/sv_snapshot.o \
  $(B)/ded/sv_world.o \
  \
  $(B)/ded/cm_load.o \
  $(B)/ded/cm_patch.o \
  $(B)/ded/cm_polylib.o \
  $(B)/ded/cm_test.o \
  $(B)/ded/cm_trace.o \
  $(B)/ded/cmd.o \
  $(B)/ded/common.o \
  $(B)/ded/cvar.o \
  $(B)/ded/files.o \
  $(B)/ded/history.o \
  $(B)/ded/keys.o \
  $(B)/ded/md4.o \
  $(B)/ded/md5.o \
  $(B)/ded/msg.o \
  $(B)/ded/net_chan.o \
  $(B)/ded/net_ip.o \
  $(B)/ded/huffman.o \
  $(B)/ded/huffman_static.o \
  \
  $(B)/ded/q_math.o \
  $(B)/ded/q_shared.o \
  \
  $(B)/ded/unzip.o \
  $(B)/ded/vm.o \
  $(B)/ded/vm_interpreted.o \
  \
  $(B)/ded/be_aas_bspq3.o \
  $(B)/ded/be_aas_cluster.o \
  $(B)/ded/be_aas_debug.o \
  $(B)/ded/be_aas_entity.o \
  $(B)/ded/be_aas_file.o \
  $(B)/ded/be_aas_main.o \
  $(B)/ded/be_aas_move.o \
  $(B)/ded/be_aas_optimize.o \
  $(B)/ded/be_aas_reach.o \
  $(B)/ded/be_aas_route.o \
  $(B)/ded/be_aas_routealt.o \
  $(B)/ded/be_aas_sample.o \
  $(B)/ded/be_ai_char.o \
  $(B)/ded/be_ai_chat.o \
  $(B)/ded/be_ai_gen.o \
  $(B)/ded/be_ai_goal.o \
  $(B)/ded/be_ai_move.o \
  $(B)/ded/be_ai_weap.o \
  $(B)/ded/be_ai_weight.o \
  $(B)/ded/be_ea.o \
  $(B)/ded/be_interface.o \
  $(B)/ded/l_crc.o \
  $(B)/ded/l_libvar.o \
  $(B)/ded/l_log.o \
  $(B)/ded/l_memory.o \
  $(B)/ded/l_precomp.o \
  $(B)/ded/l_script.o \
  $(B)/ded/l_struct.o

ifdef MINGW
  Q3DOBJ += \
  $(B)/ded/win_main.o \
  $(B)/client/win_resource.o \
  $(B)/ded/win_shared.o \
  $(B)/ded/win_syscon.o
else
  Q3DOBJ += \
  $(B)/ded/linux_signals.o \
  $(B)/ded/unix_main.o \
  $(B)/ded/unix_shared.o
endif

ifeq ($(HAVE_VM_COMPILED),true)
  ifeq ($(ARCH),x86)
    Q3DOBJ += $(B)/ded/vm_x86.o
  endif
  ifeq ($(ARCH),x86_64)
    Q3DOBJ += $(B)/ded/vm_x86.o
  endif
  ifeq ($(ARCH),arm)
    Q3DOBJ += $(B)/ded/vm_armv7l.o
  endif
  ifeq ($(ARCH),aarch64)
    Q3DOBJ += $(B)/ded/vm_aarch64.o
  endif
endif

$(B)/$(TARGET_SERVER): $(Q3DOBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3DOBJ) $(LDFLAGS)

#############################################################################
## CLIENT/SERVER RULES
#############################################################################

$(B)/client/%.o: $(ADIR)/%.s
	$(DO_AS)

$(B)/client/%.o: $(CDIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(SDIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(CMDIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(BLIBDIR)/%.c
	$(DO_BOT_CC)

$(B)/client/%.o: $(JPDIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(SDLDIR)/%.c
	$(DO_CC)

$(B)/rend1/%.o: $(R1DIR)/%.c
	$(DO_REND_CC)

$(B)/rend1/%.o: $(RCDIR)/%.c
	$(DO_REND_CC)

$(B)/rend1/%.o: $(CMDIR)/%.c
	$(DO_REND_CC)

$(B)/rendv/%.o: $(RVDIR)/%.c
	$(DO_REND_CC)

$(B)/rendv/%.o: $(RCDIR)/%.c
	$(DO_REND_CC)

$(B)/rendv/%.o: $(CMDIR)/%.c
	$(DO_REND_CC)

$(B)/client/%.o: $(UDIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(W32DIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(W32DIR)/%.rc
	$(DO_WINDRES)

$(B)/ded/%.o: $(ADIR)/%.s
	$(DO_AS)

$(B)/ded/%.o: $(SDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/%.o: $(CMDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/%.o: $(BLIBDIR)/%.c
	$(DO_BOT_CC)

$(B)/ded/%.o: $(UDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/%.o: $(W32DIR)/%.c
	$(DO_DED_CC)

$(B)/ded/%.o: $(W32DIR)/%.rc
	$(DO_WINDRES)

#############################################################################
# MISC
#############################################################################

install: release
	@for i in $(TARGETS); do \
		if [ -f $(BR)$$i ]; then \
			$(INSTALL) -D -m 0755 "$(BR)/$$i" "$(DESTDIR)$$i"; \
			$(STRIP) "$(DESTDIR)$$i"; \
		fi \
	done

clean: clean-debug clean-release

clean2:
	@echo "CLEAN $(B)"
	@if [ -d $(B) ];then (find $(B) -name '*.d' -exec rm {} \;)fi
	@rm -f $(Q3OBJ) $(Q3DOBJ)
	@rm -f $(TARGETS)

clean-debug:
	@rm -rf $(BD)

clean-release:
	@echo $(BR)
	@rm -rf $(BR)

distclean: clean
	@rm -rf $(BUILD_DIR)

#############################################################################
# DEPENDENCIES
#############################################################################

D_FILES=$(shell find . -name '*.d')

#ifneq ($(strip $(D_FILES)),)
 # include $(D_FILES)
#endif

.PHONY: all clean clean2 clean-debug clean-release copyfiles \
	debug default dist distclean makedirs release \
	targets tools toolsclean
