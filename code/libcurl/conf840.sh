#!/bin/sh

# Script used to compile static curl libraries for mingw
# We intending to support HTTP HTTPS protocols only

OPTIONS="--disable-shared --disable-debug --enable-optimize --disable-curldebug --enable-symbol-hiding --disable-ares --disable-rt --disable-ech --disable-largefile --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt --disable-manual --disable-pthreads --disable-ntlm --disable-ntlm-wb --disable-unix-sockets --disable-alt-svc --disable-cookies --disable-socketpair --disable-doh --disable-websockets --disable-file --with-schannel --without-libidn2 --without-librtmp"

make distclean
./configure --prefix=/q3e-lib32 ${OPTIONS}
make -j32 CFLAGS='-O2 -march=i586 -mtune=i686'
make install

make distclean
./configure --prefix=/q3e-lib64 --host=x86_64-w64-mingw32 ${OPTIONS}
make -j32
make install

