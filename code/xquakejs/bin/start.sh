cd /home/ioq3srv/
node /home/ioq3srv/quakejs/bin/repack.js --no-graph --no-overwrite /tmp/baseq3
rm /home/ioq3srv/baseq3-cc
ln -s /tmp/baseq3-cc /home/ioq3srv/baseq3-cc
sleep 1
for pid in $(pidof -x node); do
  if [ $pid != $$ ]; then
    kill -9 $pid
  fi 
done
node /home/ioq3srv/quakejs/bin/web.js -R -wr /assets/baseq3-cc /tmp/baseq3-cc &
sleep 1
node /home/ioq3srv/quakejs/bin/proxy.js 1081 &
sleep 1
/home/ioq3srv/Quake3e/quake3e.ded.x64 \
  +cvar_restart +set net_port 27960 +set fs_basepath /home/ioq3srv \
  +set dedicated 2 +set fs_homepath /home/ioq3srv/Quake3e \
  +set fs_basegame ${BASEGAME} +set fs_game ${GAME} \
  +set ttycon 0 +set rconpassword ${RCON} \
  +set logfile 2 +set com_hunkmegs 150 +set vm_rtChecks 0 \
  +set sv_maxclients 32 +set sv_pure 0 +exec server.cfg
