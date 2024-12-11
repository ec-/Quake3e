#!/bin/sh

# Script used to compile static curl libraries for mingw
# We intending to support HTTP HTTPS protocols only

./configure --prefix=/lib32 --with-winssl --disable-rt --disable-pthreads --disable-curldebug --disable-ares --disable-ech --disable-largefile --disable-ftp --disable-file --disable-ldap --disable-ldaps  --disable-rtsp --disable-proxy   --disable-dict   --disable-telnet   --disable-tftp   --disable-pop3   --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt --disable-manual --disable-alt-svc --disable-websockets --disable-shared --enable-symbol-hiding --disable-versioned-symbols
make -j32
make install
make clean

./configure --prefix=/lib64 --host=x86_64-w64-mingw32 --with-winssl --disable-rt --disable-pthreads --disable-curldebug --disable-ares --disable-ech --disable-largefile --disable-ftp --disable-file --disable-ldap --disable-ldaps  --disable-rtsp --disable-proxy   --disable-dict   --disable-telnet   --disable-tftp   --disable-pop3   --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt --disable-manual --disable-alt-svc --disable-websockets --disable-shared --enable-symbol-hiding --disable-versioned-symbols
make -j32
make install
make clean
          
