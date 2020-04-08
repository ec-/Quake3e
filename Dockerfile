FROM debian:bullseye-slim AS builder

RUN \
  echo "# INSTALL DEPENDENCIES ##########################################" && \
  apt-get update && \
  apt-get install -y build-essential linux-headers-5.4.0-4-common libcurl4-gnutls-dev curl g++ gcc git make nodejs npm && \
  mkdir -p /tmp/build
RUN \
  echo "# FETCH INSTALLATION FILES ######################################" && \
  cd /tmp/build && \
  git clone --recursive --progress https://github.com/briancullinan/Quake3e && \
  git submodule add -f git://github.com/emscripten-core/emsdk.git code/xquakejs/lib/emsdk && \
  git submodule update --init --recursive --progress && \
  cd /tmp/build/Quake3e
RUN \
  echo "# BUILD NATIVE SERVER ##########################################" && \
  cd /tmp/build/Quake3e && \
  make BUILD_CLIENT=0 NOFPU=1
RUN \
  echo "# BUILD JS CLIENT ##########################################" && \
  cd /tmp/build/Quake3e && \
  npm install && \
  npm run install:emsdk && \
  echo "BINARYEN_ROOT = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/upstream'" >> /root/.emscripten && \
  echo "LLVM_ROOT = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/upstream/bin'" >> /root/.emscripten && \
  echo "NODE_JS = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/node/12.9.1_64bit/bin/node'" >> /root/.emscripten && \
  echo "EM_CACHE = '/tmp/build/Quake3e/code/xquakejs/lib/emsdk/cache'" >> /root/.emscripten && \
  export EM_CACHE=/tmp/build/Quake3e/code/xquakejs/lib/emsdk/cache && \
  /usr/bin/python2.7 ./code/xquakejs/lib/emsdk/upstream/emscripten/embuilder.py build sdl2 vorbis ogg zlib && \
  make PLATFORM=js
RUN \
  echo "# COPY OUTPUT ##########################################" && \
  mkdir ~/Quake3e && \
  mkdir ~/Quake3e/quakejs && \
  mkdir ~/Quake3e/quakejs/bin && \
  mkdir ~/Quake3e/quakejs/lib && \
  cp /tmp/build/Quake3e/package.json ~/Quake3e/quakejs && \
  cp /tmp/build/Quake3e/build/release-js-js/quake3e.js ~/Quake3e/quakejs/bin && \
  cp /tmp/build/Quake3e/build/release-js-js/quake3e.wasm ~/Quake3e/quakejs/bin && \
  cp /tmp/build/Quake3e/build/release-linux-x86_64/quake3e.ded.x64 ~/Quake3e && \
  cp /tmp/build/Quake3e/code/xquakejs/bin/* ~/Quake3e/quakejs/bin && \
  cp /tmp/build/Quake3e/code/xquakejs/lib/* ~/Quake3e/quakejs/lib && \
  cp -R /tmp/build/Quake3e/code/xquakejs/lib/q3vm ~/Quake3e/quakejs/lib

FROM node:12.15-slim AS server
COPY --from=builder /root/Quake3e /home/ioq3srv/Quake3e
RUN \
  apt-get install systemd && \
  useradd ioq3srv && \
  mkdir /home/ioq3srv/Quake3e/baseq3 && \
  cp /home/ioq3srv/Quake3e/quakejs/bin/quake.service /etc/systemd/system \
  cp /home/ioq3srv/Quake3e/quakejs/bin/q3eded.service /etc/systemd/system
USER ioq3srv
EXPOSE 27960/udp
VOLUME [ "/home/ioq3srv/Quake3e/baseq3" ]
ENV RCON rconpass
ENV GAME baseq3
ENV BASEGAME baseq3
CMD ["/usr/sbin/init"]
