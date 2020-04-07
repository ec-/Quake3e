FROM debian:bullseye-slim AS builder

RUN \
  echo "# INSTALL DEPENDENCIES ##########################################" && \
  apt-get update && \
  apt-get install -y build-essential linux-headers-5.4.0-4-common libcurl4-gnutls-dev curl g++ gcc git make nodejs npm && \
  mkdir -p /tmp/build
RUN \
  echo "# FETCH INSTALLATION FILES ######################################" && \
  cd /tmp/build && \
  git clone --recursive --progress https://github.com/briancullinan/Quake3e
COPY . /tmp/build/Quake3e
RUN \
  echo "# NOW THE INSTALLATION ##########################################" && \
  cd /tmp/build/Quake3e && \
  make BUILD_CLIENT=0 NOFPU=1 && \
  cd /tmp/build/Quake3e && \
  npm install && \
  npm run install:emsdk && \
  make PLATFORM=js && \
  cp -r /tmp/build/* ~/Quake3e

FROM node:12.15-slim AS server
RUN adduser ioq3srv -D
COPY --from=builder /root/Quake3e /home/ioq3srv/Quake3e
USER ioq3srv
EXPOSE 27960/udp
ENTRYPOINT ["/home/ioq3srv/ioquake3/ioq3ded.x86_64"]
