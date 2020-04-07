var STATE_VERSION = 0,
    // server
    STATE_ULEN = 1,
    STATE_UNAME = 2,
    STATE_PLEN = 3,
    STATE_PASSWD = 4,
    // client
    STATE_STATUS = 5;

    // server
var BUF_SUCCESS = new Buffer([0x01, 0x00]),
    BUF_FAILURE = new Buffer([0x01, 0x01]);

module.exports = function UserPasswordAuthHandlers() {
  var authcb,
      user,
      pass,
      userlen,
      passlen;

  if (arguments.length === 1 && typeof arguments[0] === 'function')
    authcb = arguments[0];
  else if (arguments.length === 2
           && typeof arguments[0] === 'string'
           && typeof arguments[1] === 'string') {
    user = arguments[0];
    pass = arguments[1];
    userlen = Buffer.byteLength(user);
    passlen = Buffer.byteLength(pass);
    if (userlen > 255)
      throw new Error('Username too long (limited to 255 bytes)');
    else if (passlen > 255)
      throw new Error('Password too long (limited to 255 bytes)');
  } else
    throw new Error('Wrong arguments');

  return {
    METHOD: 0x02,
    server: function serverHandler(stream, cb) {
      var state = STATE_VERSION,
          userp = 0,
          passp = 0;

      function onData(chunk) {
        var i = 0,
            len = chunk.length,
            left,
            chunkLeft,
            minLen;

        while (i < len) {
          switch (state) {
            /*
              +----+------+----------+------+----------+
              |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
              +----+------+----------+------+----------+
              | 1  |  1   | 1 to 255 |  1   | 1 to 255 |
              +----+------+----------+------+----------+
            */
            case STATE_VERSION:
              if (chunk[i] !== 0x01) {
                stream.removeListener('data', onData);
                cb(new Error('Unsupported auth request version: ' + chunk[i]));
                return;
              }
              ++i;
              ++state;
            break;
            case STATE_ULEN:
              var ulen = chunk[i];
              if (ulen === 0) {
                stream.removeListener('data', onData);
                cb(new Error('Bad username length (0)'));
                return;
              }
              ++i;
              ++state;
              user = new Buffer(ulen);
              userp = 0;
            break;
            case STATE_UNAME:
              left = user.length - userp;
              chunkLeft = len - i;
              minLen = (left < chunkLeft ? left : chunkLeft);
              chunk.copy(user,
                         userp,
                         i,
                         i + minLen);
              userp += minLen;
              i += minLen;
              if (userp === user.length) {
                user = user.toString('utf8');
                ++state;
              }
            break;
            case STATE_PLEN:
              var plen = chunk[i];
              if (plen === 0) {
                stream.removeListener('data', onData);
                cb(new Error('Bad password length (0)'));
                return;
              }
              ++i;
              ++state;
              pass = new Buffer(plen);
              passp = 0;
            break;
            case STATE_PASSWD:
              left = pass.length - passp;
              chunkLeft = len - i;
              minLen = (left < chunkLeft ? left : chunkLeft);
              chunk.copy(pass,
                         passp,
                         i,
                         i + minLen);
              passp += minLen;
              i += minLen;
              if (passp === pass.length) {
                stream.removeListener('data', onData);
                pass = pass.toString('utf8');
                state = STATE_VERSION;
                if (i < len)
                  stream.unshift(chunk.slice(i));
                authcb(user, pass, function(success) {
                  if (stream.writable) {
                    if (success)
                      stream.write(BUF_SUCCESS);
                    else
                      stream.write(BUF_FAILURE);
                    cb(success);
                  }
                });
                return;
              }
            break;
            // ===================================================================
          }
        }
      }
      stream.on('data', onData);
    },
    client: function clientHandler(stream, cb) {
      var state = STATE_VERSION;
      function onData(chunk) {
        var i = 0,
            len = chunk.length;

        while (i < len) {
          switch (state) {
            /*
              +----+--------+
              |VER | STATUS |
              +----+--------+
              | 1  |   1    |
              +----+--------+
            */
            case STATE_VERSION:
              if (chunk[i] !== 0x01) {
                stream.removeListener('data', onData);
                cb(new Error('Unsupported auth request version: ' + chunk[i]));
                return;
              }
              ++i;
              state = STATE_STATUS;
            break;
            case STATE_STATUS:
              var status = chunk[i];
              ++i;
              state = STATE_VERSION;
              if (i < len)
                stream.unshift(chunk.slice(i));
              stream.removeListener('data', onData);
              cb(status === 0);
              return;
            break;
          }
        }
      }
      stream.on('data', onData);

      var buf = new Buffer(3 + userlen + passlen);
      buf[0] = 0x01;
      buf[1] = userlen;
      buf.write(user, 2, userlen);
      buf[2 + userlen] = passlen;
      buf.write(pass, 3 + userlen, passlen);

      stream.write(buf);
    }
  };
};
