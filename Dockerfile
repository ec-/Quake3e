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
  cp /tmp/build/Quake3e/package.json ~/quakejs && \
  cp /tmp/build/Quake3e/build/release-js-js/quake3e.js ~/quakejs/bin && \
  cp /tmp/build/Quake3e/build/release-js-js/quake3e.wasm ~/quakejs/bin && \
  cp /tmp/build/Quake3e/build/release-linux-x86_64/quake3e.ded.x64 ~/Quake3e && \
  rm -R /tmp/build/Quake3e/code/xquakejs/lib/emsdk && \
  cp -R /tmp/build/Quake3e/code/xquakejs/bin/* ~/quakejs/bin && \
  cp -R /tmp/build/Quake3e/code/xquakejs/lib/* ~/quakejs/lib

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
  echo "cd /home/ioq3srv/quakejs\n\
npm run repack -- --no-graph --no-overwrite /home/ioq3srv/baseq3\n\
node /home/ioq3srv/quakejs/bin/web.js -R /assets/baseq3-cc /tmp/baseq3-cc &\n\
node /home/ioq3srv/quakejs/bin/proxy.js 8081 &\n\
/home/ioq3srv/Quake3e/quake3e.ded.x64 \\\n\
  +cvar_restart +set net_port 27960 +set fs_basepath /home/ioq3srv \\\n\
  +set dedicated 2 +set fs_homepath /home/ioq3srv \\\n\
  +set fs_basegame \\\${BASEGAME} +set fs_game \\\${GAME} \\\n\
  +set logfile 2 +set com_hunkmegs 150 +set vm_rtChecks 0 \\\n\
  +set ttycon 0 +set rconpassword \\\${RCON} \\\n\
  +set sv_maxclients 32 +exec server.cfg" >> /home/ioq3srv/start.sh && \
  chmod a+x /home/ioq3srv/start.sh && \
  chown -R ioq3srv /home/ioq3srv
USER ioq3srv
EXPOSE 27960/udp
EXPOSE 1081/tcp
EXPOSE 8080/tcp
VOLUME [ "/home/ioq3srv/baseq3" ]
VOLUME [ "/tmp/Quake3e" ]
VOLUME [ "/tmp/quakejs" ]
ENV RCON=rconpass
ENV GAME=baseq3
ENV BASEGAME=baseq3
CMD ["/home/ioq3srv/start.sh"]
