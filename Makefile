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

ifeq ($(COMPILE_PLATFORM),mingw32)
  ifeq ($(COMPILE_ARCH),i386)
    COMPILE_ARCH=x86
  endif
endif

BUILD_CLIENT     = 1
BUILD_SERVER     = 1

USE_CURL         = 1
USE_LOCAL_HEADERS= 0

CNAME            = quake3e
DNAME            = quake3e.ded

#USE_ALSA_STATIC = 1
#USE_STATIC_GL   = 1

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

ifndef COPYDIR
COPYDIR="/usr/local/games/quake3"
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

ifndef USE_ALSA_STATIC
USE_ALSA_STATIC=0
endif

ifndef USE_STATIC_GL
USE_STATIC_GL=0
endif

#############################################################################

BD=$(BUILD_DIR)/debug-$(PLATFORM)-$(ARCH)
BR=$(BUILD_DIR)/release-$(PLATFORM)-$(ARCH)
ADIR=$(MOUNT_DIR)/asm
CDIR=$(MOUNT_DIR)/client
SDIR=$(MOUNT_DIR)/server
RDIR=$(MOUNT_DIR)/renderer
RCDIR=$(MOUNT_DIR)/renderercommon
CMDIR=$(MOUNT_DIR)/qcommon
UDIR=$(MOUNT_DIR)/unix
W32DIR=$(MOUNT_DIR)/win32
BLIBDIR=$(MOUNT_DIR)/botlib
NDIR=$(MOUNT_DIR)/null
UIDIR=$(MOUNT_DIR)/ui
JPDIR=$(MOUNT_DIR)/jpeg-8c
LOKISETUPDIR=$(UDIR)/setup

bin_path=$(shell which $(1) 2> /dev/null)
STRIP=strip

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

#############################################################################
# SETUP AND BUILD -- LINUX
#############################################################################

## Defaults
INSTALL=install
MKDIR=mkdir

ifeq ($(PLATFORM),linux)

  ifeq ($(ARCH),x86_64)
    LIB=lib64
  else
    LIB=lib
  endif

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes -pipe

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
  endif

  BASE_CFLAGS += -I/usr/X11R7/include -I/usr/include

  OPTIMIZE = -O2 -fvisibility=hidden

  ifeq ($(ARCH),x86_64)
    CNAME    = quake3e.x64
    DNAME    = quake3e.ded.x64
  else
  ifeq ($(ARCH),x86)
    OPTIMIZE += -march=i586 -mtune=i686
  endif
  endif

  ifneq ($(HAVE_VM_COMPILED),true)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -g -O0

  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  LDFLAGS=-ldl -lm -Wl,--hash-style=both

  CLIENT_LDFLAGS=-L/usr/X11R7/$(LIB) -L/usr/$(LIB) -lX11

  ifeq ($(USE_STATIC_GL),1)
    CLIENT_LDFLAGS += -lGL
  endif

  ifeq ($(USE_ALSA_STATIC),1)
    CLIENT_LDFLAGS += -lasound -lpthread
  endif

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

  ifeq ($(ARCH),x86)
    # linux32 make ...
    BASE_CFLAGS += -m32
    LDFLAGS+=-m32
  endif

else # ifeq Linux


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
  ifndef WINDRES
    WINDRES=windres
  endif

  ifeq ($(CC),)
    $(error Cannot find a suitable cross compiler for $(PLATFORM))
  endif

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
    -DUSE_ICON

#  OPTIMIZE = -O3 -march=i586 -fomit-frame-pointer -ffast-math -falign-loops=2 \
#    -funroll-loops -falign-jumps=2 -falign-functions=2 -fstrength-reduce

  ifeq ($(ARCH),x86_64)
    CNAME    = quake3e.x64
    DNAME    = quake3e.ded.x64
    BASE_CFLAGS += -m64
    OPTIMIZE = -O2 -ffast-math -fstrength-reduce
    HAVE_VM_COMPILED = true
  endif
  ifeq ($(ARCH),x86)
    BASE_CFLAGS += -m32
    OPTIMIZE = -O2 -march=i586 -mtune=i686 -ffast-math -fstrength-reduce
    HAVE_VM_COMPILED = true
  endif

  DEBUG_CFLAGS=$(BASE_CFLAGS) -g -O0

  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  SHLIBEXT=dll
  SHLIBCFLAGS=
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  BINEXT=.exe

  LDFLAGS= -mwindows -Wl,--dynamicbase -Wl,--nxcompat
  LDFLAGS += -lwsock32 -lgdi32 -lwinmm -lole32 -lws2_32 -lpsapi -lcomctl32

  CLIENT_LDFLAGS=$(LDFLAGS)

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

else # ifeq mingw32

#############################################################################
# SETUP AND BUILD -- FREEBSD
#############################################################################

ifeq ($(PLATFORM),freebsd)

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
                -I/usr/X11R6/include -I/usr/local/include \
                -fvisibility=hidden

  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  ifeq ($(ARCH),x86_64)
    RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O3 -ffast-math -funroll-loops \
      -fomit-frame-pointer -fexpensive-optimizations
  else
  ifeq ($(ARCH),x86)
    RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O3 -mtune=pentiumpro \
      -march=pentium -fomit-frame-pointer -pipe -ffast-math \
      -falign-loops=2 -falign-jumps=2 -falign-functions=2 \
      -funroll-loops -fstrength-reduce
  endif
  endif

  ifneq ($(HAVE_VM_COMPILED),true)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  # don't need -ldl (FreeBSD)
  LDFLAGS=-lm -lGL -lX11 -L/usr/local/lib -L/usr/X11R6/lib -lX11 -lXext

  CLIENT_LDFLAGS =-lm -lGL -lX11 -L/usr/local/lib -L/usr/X11R6/lib -lX11 -lXext

else # ifeq freebsd

#############################################################################
# SETUP AND BUILD -- OPENBSD
#############################################################################

ifeq ($(PLATFORM),openbsd)

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
                -I/usr/X11R6/include -I/usr/local/include \
                -I/usr/local/include/SDL -fvisibility=hidden

  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  ifeq ($(USE_CODEC_VORBIS),1)
    BASE_CFLAGS += -DUSE_CODEC_VORBIS=1
  endif

  ifeq ($(ARCH),x86_64)
    RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O3 -ffast-math -funroll-loops \
      -fomit-frame-pointer -fexpensive-optimizations
  else
  ifeq ($(ARCH),x86)
    RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O3 -mtune=pentiumpro \
      -march=pentium -fomit-frame-pointer -pipe -ffast-math \
      -falign-loops=2 -falign-jumps=2 -falign-functions=2 \
      -funroll-loops -fstrength-reduce
  endif
  endif

  ifneq ($(HAVE_VM_COMPILED),true)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)

  THREAD_LDFLAGS=-lpthread
  # don't need -ldl (FreeBSD)
  LDFLAGS=-lm -lSDL -lGL -lX11 -L/usr/local/lib -L/usr/X11R6/lib -lX11 -lXext

  CLIENT_LDFLAGS =-lm -lSDL -lGL -lX11 -L/usr/local/lib -L/usr/X11R6/lib -lX11 -lXext

  ifeq ($(USE_CODEC_VORBIS),1)
    CLIENT_LDFLAGS += -lvorbisfile -lvorbis -logg
  endif

else # ifeq openbsd

#############################################################################
# SETUP AND BUILD -- NETBSD
#############################################################################

ifeq ($(PLATFORM),netbsd)

  LDFLAGS=-lm
  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared $(LDFLAGS)
  THREAD_LDFLAGS=-lpthread

  BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes
  DEBUG_CFLAGS=$(BASE_CFLAGS) -g

  ifneq ($(HAVE_VM_COMPILED),true)
    BASE_CFLAGS += -DNO_VM_COMPILED
  endif

  BUILD_CLIENT = 0

else # ifeq netbsd

#############################################################################
# SETUP AND BUILD -- GENERIC
#############################################################################

  BASE_CFLAGS=-DNO_VM_COMPILED
  DEBUG_CFLAGS=$(BASE_CFLAGS) -g
  RELEASE_CFLAGS=$(BASE_CFLAGS) -DNDEBUG -O3

  SHLIBEXT=so
  SHLIBCFLAGS=-fPIC
  SHLIBLDFLAGS=-shared

endif #Linux
endif #mingw32
endif #FreeBSD
endif #OpenBSD
endif #NetBSD

#TARGET_CLIENT=$(CNAME).$(ARCH)$(BINEXT)
#TARGET_SERVER=$(DNAME).$(ARCH)$(BINEXT)

TARGET_CLIENT=$(CNAME)$(BINEXT)
TARGET_SERVER=$(DNAME)$(BINEXT)

TARGETS =

ifneq ($(BUILD_SERVER),0)
  TARGETS += $(B)/$(TARGET_SERVER)
endif

ifneq ($(BUILD_CLIENT),0)
  TARGETS += $(B)/$(TARGET_CLIENT)
endif

ifeq ($(USE_CCACHE),1)
  CC := ccache $(CC)
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
  BASE_CFLAGS += -DCURL_STATICLIB
endif
endif

ifeq ($(USE_ALSA_STATIC),1)
  BASE_CFLAGS += -DUSE_ALSA_STATIC
endif

ifeq ($(USE_STATIC_GL),1)
  BASE_CFLAGS += -DUSE_STATIC_GL
endif

ifeq ($(GENERATE_DEPENDENCIES),1)
  BASE_CFLAGS += -MMD
endif

ifeq ($(USE_SVN),1)
  BASE_CFLAGS += -DSVN_VERSION=\\\"$(SVN_VERSION)\\\"
endif

define DO_CC
$(echo_cmd) "CC $<"
$(Q)$(CC) $(NOTSHLIBCFLAGS) $(CFLAGS) -o $@ -c $<
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


#############################################################################
# MAIN TARGETS
#############################################################################

default: release
all: debug release

debug:
	@$(MAKE) targets B=$(BD) CFLAGS="$(CFLAGS) $(DEBUG_CFLAGS)" V=$(V)

release:
	@$(MAKE) targets B=$(BR) CFLAGS="$(CFLAGS) $(RELEASE_CFLAGS)" V=$(V)

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
	@if [ ! -d $(B)/ded ];then $(MKDIR) $(B)/ded;fi

#############################################################################
# CLIENT/SERVER
#############################################################################

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
  $(B)/client/l_struct.o \
  \
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
  $(B)/client/jutils.o \
  \
  $(B)/client/tr_animation.o \
  $(B)/client/tr_arb.o \
  $(B)/client/tr_backend.o \
  $(B)/client/tr_bsp.o \
  $(B)/client/tr_cmds.o \
  $(B)/client/tr_curve.o \
  $(B)/client/tr_flares.o \
  $(B)/client/tr_font.o \
  $(B)/client/tr_image.o \
  $(B)/client/tr_image_png.o \
  $(B)/client/tr_image_jpg.o \
  $(B)/client/tr_image_bmp.o \
  $(B)/client/tr_image_tga.o \
  $(B)/client/tr_image_pcx.o \
  $(B)/client/tr_init.o \
  $(B)/client/tr_light.o \
  $(B)/client/tr_main.o \
  $(B)/client/tr_marks.o \
  $(B)/client/tr_mesh.o \
  $(B)/client/tr_model.o \
  $(B)/client/tr_model_iqm.o \
  $(B)/client/tr_noise.o \
  $(B)/client/tr_scene.o \
  $(B)/client/tr_shade.o \
  $(B)/client/tr_shade_calc.o \
  $(B)/client/tr_shader.o \
  $(B)/client/tr_shadows.o \
  $(B)/client/tr_sky.o \
  $(B)/client/tr_surface.o \
  $(B)/client/tr_vbo.o \
  $(B)/client/tr_world.o \

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
endif

ifeq ($(USE_CURL),1)
  Q3OBJ += $(B)/client/cl_curl.o
endif

ifdef MINGW
  Q3OBJ += \
    $(B)/client/win_gamma.o \
    $(B)/client/win_glimp.o \
    $(B)/client/win_input.o \
    $(B)/client/win_main.o \
    $(B)/client/win_minimize.o \
    $(B)/client/win_qgl.o \
    $(B)/client/win_shared.o \
    $(B)/client/win_snd.o \
    $(B)/client/win_syscon.o \
    $(B)/client/win_wndproc.o \
    $(B)/client/win_resource.o
else
  Q3OBJ += \
    $(B)/client/unix_main.o \
    $(B)/client/unix_shared.o \
    $(B)/client/linux_signals.o \
    $(B)/client/linux_glimp.o \
    $(B)/client/linux_qgl.o \
    $(B)/client/linux_snd.o \
    $(B)/client/x11_dga.o \
    $(B)/client/x11_randr.o \
    $(B)/client/x11_vidmode.o

#  ifeq ($(PLATFORM),linux)
#    Q3OBJ += $(B)/client/linux_joystick.o
#  endif

endif

$(B)/$(TARGET_CLIENT): $(Q3OBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3OBJ) $(CLIENT_LDFLAGS) \
		$(LDFLAGS)
	$(STRIP) $@


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
endif

$(B)/$(TARGET_SERVER): $(Q3DOBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3DOBJ) $(LDFLAGS)
	$(STRIP) $@

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

$(B)/client/%.o: $(RDIR)/%.c
	$(DO_CC)

$(B)/client/%.o: $(RCDIR)/%.c
	$(DO_CC)

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

$(B)/ded/%.o: $(NDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/%.o: $(W32DIR)/%.c
	$(DO_DED_CC)

$(B)/ded/%.o: $(W32DIR)/%.rc
	$(DO_WINDRES)

#############################################################################
# MISC
#############################################################################

copyfiles: release
	@if [ ! -d $(COPYDIR)/baseq3 ]; then echo "You need to set COPYDIR to where your Quake3 data is!"; fi
	-$(MKDIR) -p -m 0755 $(COPYDIR)/baseq3
	-$(MKDIR) -p -m 0755 $(COPYDIR)/missionpack

ifneq ($(BUILD_CLIENT),0)
	$(INSTALL) -s -m 0755 $(BR)/$(TARGET_CLIENT) $(COPYDIR)/$(TARGET_CLIENT)
endif

ifneq ($(BUILD_SERVER),0)
	@if [ -f $(BR)/$(TARGET_SERVER) ]; then \
		$(INSTALL) -s -m 0755 $(BR)/$(TARGET_SERVER) $(COPYDIR)/$(TARGET_SERVER); \
	fi
endif

clean: clean-debug clean-release
	@$(MAKE) -C $(LOKISETUPDIR) clean

clean2:
	@echo "CLEAN $(B)"
	@if [ -d $(B) ];then (find $(B) -name '*.d' -exec rm {} \;)fi
	@rm -f $(Q3OBJ) $(Q3DOBJ)
	@rm -f $(TARGETS)

clean-debug:
	@$(MAKE) clean2 B=$(BD)

clean-release:
	@$(MAKE) clean2 B=$(BR)

distclean: clean
	@rm -rf $(BUILD_DIR)

installer: release
	@$(MAKE) VERSION=$(VERSION) -C $(LOKISETUPDIR) V=$(V)

dist:
	rm -rf quake3-$(SVN_VERSION)
	svn export . quake3-$(SVN_VERSION)
	tar --owner=root --group=root --force-local -cjf quake3-$(SVN_VERSION).tar.bz2 quake3-$(SVN_VERSION)
	rm -rf quake3-$(SVN_VERSION)

dist2:
	rm -rf quake3-1.32e-src
	svn export . quake3-1.32e-src
	zip -9 -r quake3-1.32e-src.zip quake3-1.32e-src/*
	rm -rf quake3-1.32e-src

#############################################################################
# DEPENDENCIES
#############################################################################

D_FILES=$(shell find . -name '*.d')

#ifneq ($(strip $(D_FILES)),)
 # include $(D_FILES)
#endif

.PHONY: all clean clean2 clean-debug clean-release copyfiles \
	debug default dist distclean installer makedirs release \
	targets tools toolsclean
