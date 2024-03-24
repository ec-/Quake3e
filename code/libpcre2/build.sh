#! /bin/bash

build () {
	SRC_DIR=src
	BUILD_DIR=build/$1
	INSTALL_DIR=bin/$1

	VERSION=pcre2-10.43
	if [[ ! -d ${SRC_DIR} ]]; then
		git clone --depth 1 --branch ${VERSION} https://github.com/PCRE2Project/pcre2.git ${SRC_DIR}
		[[ $? -eq 0 ]] || exit 1
	elif [[ $(git -C ${SRC_DIR} describe --tags) != ${VERSION} ]]; then
		echo "error: ${SRC_DIR} did not checkout ${VERSION}" 1>&2
		exit 1
	fi
	if ! git -C ${SRC_DIR} apply -qR --check ../embed_msvc_debug_info.patch; then
		git -C ${SRC_DIR} apply ../embed_msvc_debug_info.patch || exit 1
	fi

	GENERATOR=$2
	EXTRA_OPTIONS=${@:3}
	cmake -S ${SRC_DIR} -B ${BUILD_DIR} -G "${GENERATOR}" ${EXTRA_OPTIONS} \
		-DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_STATIC_LIBS=ON \
		-DPCRE2_BUILD_PCRE2_8=ON \
		-DPCRE2_BUILD_PCRE2_16=OFF \
		-DPCRE2_BUILD_PCRE2_32=OFF \
		-DPCRE2_SUPPORT_UNICODE=ON \
		-DPCRE2_BUILD_PCRE2GREP=OFF \
		-DPCRE2_BUILD_TESTS=OFF \
		-DPCRE2_STATIC_RUNTIME=ON \
		-DINSTALL_MSVC_PDB=OFF
	[[ $? -eq 0 ]] || exit 1

	cmake --build ${BUILD_DIR} --config Debug   || exit 1
	cmake --build ${BUILD_DIR} --config Release || exit 1

	cmake --install ${BUILD_DIR} --config Debug           || exit 1
	cmake --install ${BUILD_DIR} --config Release --strip || exit 1

	rm -r ${INSTALL_DIR}/bin                  || exit 1
	rm -r ${INSTALL_DIR}/cmake                || exit 1
	rm    ${INSTALL_DIR}/include/pcre2posix.h || exit 1
	rm -r ${INSTALL_DIR}/lib/pkgconfig        || exit 1
	rm    ${INSTALL_DIR}/lib/*pcre2-posix*    || exit 1
	rm -r ${INSTALL_DIR}/man                  || exit 1
	rm -r ${INSTALL_DIR}/share                || exit 1

	rm -f bin/include/pcre2.h                      || exit 1
	mv ${INSTALL_DIR}/include bin                  || exit 1
	mv ${INSTALL_DIR}/lib/*pcre2-8* ${INSTALL_DIR} || exit 1
	rm -d ${INSTALL_DIR}/lib                       || exit 1
}
