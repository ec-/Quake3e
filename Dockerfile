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
  cd /tmp/build/Quake3e && \
  git submodule add -f git://github.com/emscripten-core/emsdk.git code/xquakejs/lib/emsdk && \
  git submodule update --init --recursive --progress
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
  export STANDALONE=1 && \
  make PLATFORM=js
RUN \
  echo "# COPY OUTPUT ##########################################" && \
  mkdir ~/Quake3e && \
  mkdir ~/quakejs && \
  mkdir ~/quakejs/bin && \
  mkdir ~/quakejs/lib && \
  rm -R /tmp/build/Quake3e/code/xquakejs/lib/emsdk && \
  cp -R /tmp/build/Quake3e/code/xquakejs/bin/* ~/quakejs/bin && \
  cp -R /tmp/build/Quake3e/code/xquakejs/lib/* ~/quakejs/lib && \
  cp /tmp/build/Quake3e/package.json ~/quakejs && \
  cp /tmp/build/Quake3e/build/release-js-js/quake3e.* ~/quakejs/bin && \
  cp /tmp/build/Quake3e/build/release-linux-x86_64/quake3e.ded.x64 ~/Quake3e

#  cp /home/ioq3srv/Quake3e/quakejs/bin/q3eded.service /etc/systemd/system && \
FROM node:12.15-slim AS server
COPY --from=builder /root/Quake3e /home/ioq3srv/Quake3e
COPY --from=builder /root/quakejs /home/ioq3srv/quakejs
RUN \
  apt-get update && \
  apt-get install -y systemd imagemagick vorbis-tools vim && \
  useradd ioq3srv && \
  mkdir /home/ioq3srv/baseq3 && \
  sed -i -e 's/code\/xquakejs\///g' /home/ioq3srv/quakejs/package.json && \
  cp /home/ioq3srv/quakejs/bin/http.service /etc/systemd/system && \
  cp /home/ioq3srv/quakejs/bin/proxy.service /etc/systemd/system && \
  cd /home/ioq3srv/quakejs && \
  npm install && \
  npm install --only=dev && \
  chmod a+x /home/ioq3srv/quakejs/bin/start.sh && \
  chown -R ioq3srv /home/ioq3srv
USER ioq3srv
EXPOSE 27960/udp
EXPOSE 1081/tcp
EXPOSE 8080/tcp
VOLUME [ "/tmp/baseq3" ]
VOLUME [ "/tmp/Quake3e" ]
VOLUME [ "/tmp/quakejs" ]
ENV RCON=rconpass
ENV GAME=baseq3-cc
ENV BASEGAME=baseq3-cc
CMD ["/home/ioq3srv/quakejs/bin/start.sh"]
