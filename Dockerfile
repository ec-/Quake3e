FROM debian:bullseye-slim AS builder

RUN \
  echo "# INSTALL DEPENDENCIES ##########################################" && \
  apt-get update && \
  apt-get install -y build-essential linux-headers-5.4.0-4-common libcurl4-gnutls-dev curl g++ gcc git make && \
  mkdir -p /tmp/build
RUN \
  echo "# FETCH INSTALLATION FILES ######################################" && \
  cd /tmp/build && \
  git clone --progress https://github.com/briancullinan/Quake3e
RUN \
  echo "# NOW THE INSTALLATION ##########################################" && \
  cd /tmp/build/Quake3e && \
  make BUILD_CLIENT=0 NOFPU=1 && \
  make PLATFORM=js && \
  cp -r /tmp/build/* ~/ioquake3

FROM node:12.15-slim AS server
RUN adduser ioq3srv -D
COPY --from=builder /root/ioquake3 /home/ioq3srv/ioquake3
RUN ln -s /pak0.pk3 /home/ioq3srv/ioquake3/baseq3/pak0.pk3
USER ioq3srv
EXPOSE 27960/udp
ENTRYPOINT ["/home/ioq3srv/ioquake3/ioq3ded.x86_64"]
