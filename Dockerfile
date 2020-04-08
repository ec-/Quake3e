FROM debian:bullseye-slim AS builder

RUN \
  echo "# INSTALL DEPENDENCIES ##########################################" && \
  apt-get update && \
  apt-get install -y build-essential linux-headers-5.4.0-4-common libcurl4-gnutls-dev curl g++ gcc git make nodejs npm python 2.7 python3.7 && \
  mkdir -p /tmp/build
RUN \
  echo "# FETCH INSTALLATION FILES ######################################" && \
  cd /tmp/build && \
  git clone --recursive --progress https://github.com/briancullinan/Quake3e && \
  cd /tmp/build/Quake3e
RUN \
  echo "# NOW THE INSTALLATION ##########################################" && \
  cd /tmp/build/Quake3e && \
  make BUILD_CLIENT=0 NOFPU=1
RUN \
  echo "# NOW THE JS ##########################################" && \
  cd /tmp/build/Quake3e && \
  git submodule add -f git://github.com/emscripten-core/emsdk.git code/xquakejs/lib/emsdk && \
  git submodule update --init --recursive --progress && \
  npm install && \
  npm run install:emsdk && \
  echo "BINARYEN_ROOT = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/upstream'" >> /root/.emscripten && \
  echo "LLVM_ROOT = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/upstream/bin'" >> /root/.emscripten && \
  echo "NODE_JS = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/node/12.9.1_64bit/bin/node'" >> /root/.emscripten && \
  echo "EM_CACHE = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/cache'" >> /root/.emscripten && \
  export EM_CACHE=/tmp/build/Quake3e/code/xquakejs/lib/emsdk/cache && \
  /usr/bin/python2.7 ./code/xquakejs/lib/emsdk/upstream/emscripten/embuilder.py build sdl2 vorbis ogg zlib && \
  make PLATFORM=js && \
  cp -r /tmp/build/* ~/Quake3e

FROM node:12.15-slim AS server
RUN adduser ioq3srv -D
COPY --from=builder /root/Quake3e /home/ioq3srv/Quake3e
USER ioq3srv
EXPOSE 27960/udp
ENTRYPOINT ["/home/ioq3srv/ioquake3/ioq3ded.x86_64"]
