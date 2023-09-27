#!/bin/bash

# Let's make the user give us a target to work with.
# architecture is assumed universal if not specified, and is optional.
# if arch is defined, it we will store the .app bundle in the target arch build directory
if [ $# == 0 ] || [ $# -gt 2 ]; then
	echo "Usage:   $0 target <arch>"
	echo "Example: $0 release x86"
	echo "Valid targets are:"
	echo " release"
	echo " debug"
	echo
	echo "Optional architectures are:"
	echo " x86"
	echo " x86_64"
	echo " ppc"
	echo " aarch64"
	echo
	exit 1
fi

# validate target name
if [ "$1" == "release" ]; then
	TARGET_NAME="release"
elif [ "$1" == "debug" ]; then
	TARGET_NAME="debug"
else
	echo "Invalid target: $1"
	echo "Valid targets are:"
	echo " release"
	echo " debug"
	exit 1
fi

CURRENT_ARCH=""

# validate the architecture if it was specified
if [ "$2" != "" ]; then
	if [ "$2" == "x86" ]; then
		CURRENT_ARCH="x86"
	elif [ "$2" == "x86_64" ]; then
		CURRENT_ARCH="x86_64"
	elif [ "$2" == "ppc" ]; then
		CURRENT_ARCH="ppc"
	elif [ "$2" == "aarch64" ]; then
		CURRENT_ARCH="aarch64"
	else
		echo "Invalid architecture: $2"
		echo "Valid architectures are:"
		echo " x86"
		echo " x86_64"
		echo " ppc"
		echo " aarch64"
		echo
		exit 1
	fi
fi

# symlinkArch() creates a symlink with the architecture suffix.
# meant for universal binaries, but also handles the way this script generates
# application bundles for a single architecture as well.
function symlinkArch()
{
    EXT="dylib"
    SEP="${3}"
    SRCFILE="${1}"
    DSTFILE="${2}${SEP}"
    DSTPATH="${4}"

    if [ ! -e "${DSTPATH}/${SRCFILE}.${EXT}" ]; then
        echo "**** ERROR: missing ${SRCFILE}.${EXT} from ${MACOS}"
        exit 1
    fi

    if [ ! -d "${DSTPATH}" ]; then
        echo "**** ERROR: path not found ${DSTPATH}"
        exit 1
    fi

    pushd "${DSTPATH}" > /dev/null

    IS32=`file "${SRCFILE}.${EXT}" | grep "i386"`
    IS64=`file "${SRCFILE}.${EXT}" | grep "x86_64"`
    ISPPC=`file "${SRCFILE}.${EXT}" | grep "ppc"`
    ISARM=`file "${SRCFILE}.${EXT}" | grep "aarch64"`

    if [ "${IS32}" != "" ]; then
        if [ ! -L "${DSTFILE}x86.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}x86.${EXT}"
        fi
    elif [ -L "${DSTFILE}x86.${EXT}" ]; then
        rm "${DSTFILE}x86.${EXT}"
    fi

    if [ "${IS64}" != "" ]; then
        if [ ! -L "${DSTFILE}x86_64.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}x86_64.${EXT}"
        fi
    elif [ -L "${DSTFILE}x86_64.${EXT}" ]; then
        rm "${DSTFILE}x86_64.${EXT}"
    fi

    if [ "${ISPPC}" != "" ]; then
        if [ ! -L "${DSTFILE}ppc.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}ppc.${EXT}"
        fi
    elif [ -L "${DSTFILE}ppc.${EXT}" ]; then
        rm "${DSTFILE}ppc.${EXT}"
    fi

    if [ "${ISARM}" != "" ]; then
        if [ ! -L "${DSTFILE}aarch64.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}aarch64.${EXT}"
        fi
    elif [ -L "${DSTFILE}aarch64.${EXT}" ]; then
        rm "${DSTFILE}aarch64.${EXT}"
    fi

    popd > /dev/null
}

SEARCH_ARCHS="																	\
	x86																			\
	x86_64																		\
	ppc																			\
	aarch64																		\
"

HAS_LIPO=`command -v lipo`
HAS_CP=`command -v cp`

# if lipo is not available, we cannot make a universal binary, print a warning
if [ ! -x "${HAS_LIPO}" ] && [ "${CURRENT_ARCH}" == "" ]; then
	CURRENT_ARCH=`uname -m`
	if [ "${CURRENT_ARCH}" == "i386" ]; then CURRENT_ARCH="x86"; fi
	echo "$0 cannot make a universal binary, falling back to architecture ${CURRENT_ARCH}"
fi

# if the optional arch parameter is used, replace SEARCH_ARCHS to only work with one
if [ "${CURRENT_ARCH}" != "" ]; then
	SEARCH_ARCHS="${CURRENT_ARCH}"
fi

AVAILABLE_ARCHS=""

Q3E_VERSION=`grep '^VERSION=' Makefile | sed -e 's/.*=\(.*\)/\1/'`
Q3E_CLIENT_ARCHS=""
Q3E_SERVER_ARCHS=""
Q3E_CGAME_ARCHS=""
Q3E_GAME_ARCHS=""
Q3E_UI_ARCHS=""
Q3E_MP_CGAME_ARCHS=""
Q3E_MP_GAME_ARCHS=""
Q3E_MP_UI_ARCHS=""

BASEDIR="baseq3"
MISSIONPACKDIR="missionpack"

CGAME="cgame"
GAME="qagame"
UI="ui"

DEDICATED_NAME="quake3e.ded"

CGAME_NAME="${CGAME}.dylib"
GAME_NAME="${GAME}.dylib"
UI_NAME="${UI}.dylib"

ICNSDIR="code/unix"
ICNS="quake3_flat.icns"
PKGINFO="APPLQ3E"

OBJROOT="build"
#BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${CURRENT_ARCH}"
PRODUCT_NAME="quake3e"
WRAPPER_EXTENSION="app"
WRAPPER_NAME="${PRODUCT_NAME}.${WRAPPER_EXTENSION}"
CONTENTS_FOLDER_PATH="${WRAPPER_NAME}/Contents"
UNLOCALIZED_RESOURCES_FOLDER_PATH="${CONTENTS_FOLDER_PATH}/Resources"
EXECUTABLE_FOLDER_PATH="${CONTENTS_FOLDER_PATH}/MacOS"
EXECUTABLE_NAME="${PRODUCT_NAME}"

# loop through the architectures to build string lists for each universal binary
for ARCH in $SEARCH_ARCHS; do
	CURRENT_ARCH=${ARCH}
	BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${CURRENT_ARCH}"
	Q3E_CLIENT="${EXECUTABLE_NAME}.${CURRENT_ARCH}"
	Q3E_SERVER="${DEDICATED_NAME}"
	Q3E_CGAME="${CGAME}${CURRENT_ARCH}.dylib"
	Q3E_GAME="${GAME}${CURRENT_ARCH}.dylib"
	Q3E_UI="${UI}${CURRENT_ARCH}.dylib"

	if [ ! -d ${BUILT_PRODUCTS_DIR} ]; then
		CURRENT_ARCH=""
		BUILT_PRODUCTS_DIR=""
		continue
	fi

	# executables
	if [ -e ${BUILT_PRODUCTS_DIR}/${Q3E_CLIENT} ]; then
		Q3E_CLIENT_ARCHS="${BUILT_PRODUCTS_DIR}/${Q3E_CLIENT} ${Q3E_CLIENT_ARCHS}"
		VALID_ARCHS="${ARCH} ${VALID_ARCHS}"
	else
		continue
	fi
	if [ -e ${BUILT_PRODUCTS_DIR}/${Q3E_SERVER} ]; then
		Q3E_SERVER_ARCHS="${BUILT_PRODUCTS_DIR}/${Q3E_SERVER} ${Q3E_SERVER_ARCHS}"
	fi

	# game
	if [ -e ${BUILT_PRODUCTS_DIR}/${BASEDIR}/${Q3E_CGAME} ]; then
		Q3E_CGAME_ARCHS="${BUILT_PRODUCTS_DIR}/${BASEDIR}/${Q3E_CGAME} ${Q3E_CGAME_ARCHS}"
	fi
	if [ -e ${BUILT_PRODUCTS_DIR}/${BASEDIR}/${Q3E_GAME} ]; then
		Q3E_GAME_ARCHS="${BUILT_PRODUCTS_DIR}/${BASEDIR}/${Q3E_GAME} ${Q3E_GAME_ARCHS}"
	fi
	if [ -e ${BUILT_PRODUCTS_DIR}/${BASEDIR}/${Q3E_UI} ]; then
		Q3E_UI_ARCHS="${BUILT_PRODUCTS_DIR}/${BASEDIR}/${Q3E_UI} ${Q3E_UI_ARCHS}"
	fi
	# missionpack
	if [ -e ${BUILT_PRODUCTS_DIR}/${MISSIONPACKDIR}/${Q3E_CGAME} ]; then
		Q3E_MP_CGAME_ARCHS="${BUILT_PRODUCTS_DIR}/${MISSIONPACKDIR}/${Q3E_CGAME} ${Q3E_MP_CGAME_ARCHS}"
	fi
	if [ -e ${BUILT_PRODUCTS_DIR}/${MISSIONPACKDIR}/${Q3E_GAME} ]; then
		Q3E_MP_GAME_ARCHS="${BUILT_PRODUCTS_DIR}/${MISSIONPACKDIR}/${Q3E_GAME} ${Q3E_MP_GAME_ARCHS}"
	fi
	if [ -e ${BUILT_PRODUCTS_DIR}/${MISSIONPACKDIR}/${Q3E_UI} ]; then
		Q3E_MP_UI_ARCHS="${BUILT_PRODUCTS_DIR}/${MISSIONPACKDIR}/${Q3E_UI} ${Q3E_MP_UI_ARCHS}"
	fi

	#echo "valid arch: ${ARCH}"
done

# final preparations and checks before attempting to make the application bundle
cd `dirname $0`

if [ ! -f Makefile ]; then
	echo "$0 must be run from the quake3e build directory"
	exit 1
fi

if [ "${Q3E_CLIENT_ARCHS}" == "" ]; then
	echo "$0: no quake3e binary architectures were found for target '${TARGET_NAME}'"
	exit 1
fi

# set the final application bundle output directory
if [ "${2}" == "" ]; then
	BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-universal2"
	if [ ! -d ${BUILT_PRODUCTS_DIR} ]; then
		mkdir -p ${BUILT_PRODUCTS_DIR} || exit 1;
	fi
else
	BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${CURRENT_ARCH}"
fi

BUNDLEBINDIR="${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}"


# here we go
echo "Creating bundle '${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}'"
echo "with architectures:"
for ARCH in ${VALID_ARCHS}; do
	echo " ${ARCH}"
done
echo ""

# make the application bundle directories
if [ ! -d "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/$BASEDIR" ]; then
	mkdir -p "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/$BASEDIR" || exit 1;
fi
if [ ! -d "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/$MISSIONPACKDIR" ]; then
	mkdir -p "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/$MISSIONPACKDIR" || exit 1;
fi
if [ ! -d "${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}" ]; then
	mkdir -p "${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}" || exit 1;
fi

# copy and generate some application bundle resources
cp code/libsdl/macosx/*.dylib "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}"
cp ${ICNSDIR}/${ICNS} "${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/$ICNS" || exit 1;
echo -n ${PKGINFO} > "${BUILT_PRODUCTS_DIR}/${CONTENTS_FOLDER_PATH}/PkgInfo" || exit 1;

# create Info.Plist
PLIST="<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>${EXECUTABLE_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>quake3_flat</string>
    <key>CFBundleIdentifier</key>
    <string>org.quake3e.${PRODUCT_NAME}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${PRODUCT_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${Q3E_VERSION}</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>${Q3E_VERSION}</string>
    <key>CGDisableCoalescedUpdates</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>${MACOSX_DEPLOYMENT_TARGET}</string>"

if [ -n "${MACOSX_DEPLOYMENT_TARGET_PPC}" ] || [ -n "${MACOSX_DEPLOYMENT_TARGET_X86}" ] || [ -n "${MACOSX_DEPLOYMENT_TARGET_X86_64}" ] || [ -n "${MACOSX_DEPLOYMENT_TARGET_AARCH64}" ]; then
	PLIST="${PLIST}
    <key>LSMinimumSystemVersionByArchitecture</key>
    <dict>"

	if [ -n "${MACOSX_DEPLOYMENT_TARGET_PPC}" ]; then
	PLIST="${PLIST}
        <key>ppc</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_PPC}</string>"
	fi

	if [ -n "${MACOSX_DEPLOYMENT_TARGET_X86}" ]; then
	PLIST="${PLIST}
        <key>i386</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_X86}</string>"
	fi

	if [ -n "${MACOSX_DEPLOYMENT_TARGET_X86_64}" ]; then
	PLIST="${PLIST}
        <key>x86_64</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_X86_64}</string>"
	fi
	
	if [ -n "${MACOSX_DEPLOYMENT_TARGET_AARCH64}" ]; then
	PLIST="${PLIST}
        <key>aarch64</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_AARCH64}</string>"
	fi

	PLIST="${PLIST}
    </dict>"
fi

PLIST="${PLIST}
    <key>NSHumanReadableCopyright</key>
    <string>QUAKE III ARENA Copyright © 1999-2000 id Software, Inc. All rights reserved.</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>NSHighResolutionCapable</key>
    <false/>
    <key>NSRequiresAquaSystemAppearance</key>
    <false/>
</dict>
</plist>
"
echo -e "${PLIST}" > "${BUILT_PRODUCTS_DIR}/${CONTENTS_FOLDER_PATH}/Info.plist"

# action takes care of generating universal binaries if lipo is available
# otherwise, it falls back to using a simple copy, expecting the first item in
# the second parameter list to be the desired architecture
function action()
{
	COMMAND=""

	if [ -x "${HAS_LIPO}" ]; then
		COMMAND="${HAS_LIPO} -create -o"
		$HAS_LIPO -create -o "${1}" ${2} # make sure $2 is treated as a list of files
	elif [ -x ${HAS_CP} ]; then
		COMMAND="${HAS_CP}"
		SRC="${2// */}" # in case there is a list here, use only the first item
		$HAS_CP "${SRC}" "${1}"
	else
		"$0 cannot create an application bundle."
		exit 1
	fi

	#echo "${COMMAND}" "${1}" "${2}"
}

#
# the meat of universal binary creation
# destination file names do not have architecture suffix.
# action will handle merging universal binaries if supported.
# symlink appropriate architecture names for universal (fat) binary support.
#

# executables
action "${BUNDLEBINDIR}/${EXECUTABLE_NAME}"				"${Q3E_CLIENT_ARCHS}"
action "${BUNDLEBINDIR}/${DEDICATED_NAME}"				"${Q3E_SERVER_ARCHS}"

# game
action "${BUNDLEBINDIR}/${BASEDIR}/${CGAME_NAME}"		"${Q3E_CGAME_ARCHS}"
action "${BUNDLEBINDIR}/${BASEDIR}/${GAME_NAME}"		"${Q3E_GAME_ARCHS}"
action "${BUNDLEBINDIR}/${BASEDIR}/${UI_NAME}"			"${Q3E_UI_ARCHS}"
symlinkArch "${CGAME}"	"${CGAME}"	""	"${BUNDLEBINDIR}/${BASEDIR}"
symlinkArch "${GAME}"	"${GAME}"	""	"${BUNDLEBINDIR}/${BASEDIR}"
symlinkArch "${UI}"		"${UI}"		""	"${BUNDLEBINDIR}/${BASEDIR}"

# missionpack
action "${BUNDLEBINDIR}/${MISSIONPACKDIR}/${CGAME_NAME}"	"${Q3E_MP_CGAME_ARCHS}"
action "${BUNDLEBINDIR}/${MISSIONPACKDIR}/${GAME_NAME}"		"${Q3E_MP_GAME_ARCHS}"
action "${BUNDLEBINDIR}/${MISSIONPACKDIR}/${UI_NAME}"		"${Q3E_MP_UI_ARCHS}"
symlinkArch "${CGAME}"	"${CGAME}"	""	"${BUNDLEBINDIR}/${MISSIONPACKDIR}"
symlinkArch "${GAME}"	"${GAME}"	""	"${BUNDLEBINDIR}/${MISSIONPACKDIR}"
symlinkArch "${UI}"		"${UI}"		""	"${BUNDLEBINDIR}/${MISSIONPACKDIR}"
