const dgram = require('dgram')
var {createServer, Socket, isIP} = require('net')
var dns = require('dns')
var util = require('util')
var Parser = require('./socks.parser')
var ip6addr = require('ip6addr')
var WebSocket = require('ws')

var UDP_TIMEOUT = 30 * 1000 // clear stale listeners so we don't run out of ports
var ATYP = {
  IPv4: 0x01,
  NAME: 0x03,
  IPv6: 0x04
}
var REP = {
  SUCCESS: 0x00,
  GENFAIL: 0x01,
  DISALLOW: 0x02,
  NETUNREACH: 0x03,
  HOSTUNREACH: 0x04,
  CONNREFUSED: 0x05,
  TTLEXPIRED: 0x06,
  CMDUNSUPP: 0x07,
  ATYPUNSUPP: 0x08
}

var BUF_AUTH_NO_ACCEPT = Buffer.from([0x05, 0xFF]),
    BUF_REP_INTR_SUCCESS = Buffer.from([0x05,
                                       REP.SUCCESS,
                                       0x00,
                                       0x01,
                                       0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00]),
    BUF_REP_DISALLOW = Buffer.from([0x05, REP.DISALLOW]),
    BUF_REP_CMDUNSUPP = Buffer.from([0x05, REP.CMDUNSUPP])

function Server(opts) {
  if (!(this instanceof Server))
    return new Server()

  this._slaves = (opts || {}).slaves || []
  this._listeners = {}
  this._timeouts = {}
  this._dnsLookup = {}
  this._debug = true
  this._auths = []
  this._connections = 0
  this.maxConnections = Infinity
}

Server.prototype._onClose = function (socket, onData) {
  console.log('Closing ', socket._socket.remoteAddress)
  socket.off('data', onData)
  socket.off('message', onData)
  if (socket.dstSock) {
    if(typeof socket.dstSock.end == 'function')
      socket.dstSock.end()
    else if(typeof socket.dstSock.close == 'function')
      socket.dstSock.close()
  }
  socket.dstSock = undefined
  if(socket._socket.writable) {
    socket.on('data', onData)
    socket.on('message', onData)
  }
}

Server.prototype._onParseError = function(socket, onData, err) {
  console.log('Parse error ', err)
  socket.off('data', onData)
  socket.off('message', onData)
  socket.close()
}

Server.prototype._onMethods = function(parser, socket, onData, methods) {
  var auths = this._auths
  parser.authed = true
  socket.off('data', onData)
  socket.off('message', onData)
  socket._socket.pause()
  socket.send(Buffer.from([0x05, 0x00]))
  socket.on('data', onData)
  socket.on('message', onData)
  socket._socket.resume()
  //socket.send(BUF_AUTH_NO_ACCEPT)
}

Server.prototype._onRequest = async function(socket, onData, reqInfo) {
  reqInfo.srcAddr = socket._socket.remoteAddress
  reqInfo.srcPort = socket._socket.remotePort
  socket.off('data', onData)
  socket.off('message', onData)
  socket._socket.pause()
  var intercept = false // TODO: use this for something cool
  if (intercept && !reqInfo.dstAddr.includes('0.0.0.0')) {
    socket.send(BUF_REP_INTR_SUCCESS);
    socket.removeListener('error', onErrorNoop);
    process.nextTick(function() {
      socket.resume();
      var body = 'Hello ' + reqInfo.srcAddr + '!\n\nToday is: ' + (new Date());
      socket.send([
        'HTTP/1.1 200 OK',
        'Connection: close',
        'Content-Type: text/plain',
        'Content-Length: ' + Buffer.byteLength(body),
        '',
        body
      ].join('\r\n'));
    });
    return socket;
  } else {
    console.log('Requesting', reqInfo.dstAddr, ':', reqInfo.dstPort)
    await this.proxySocket.apply(this, [socket, reqInfo])
  }
}

Server.prototype.lookupDNS = async function (address) {
  var self = this
  if(typeof this._dnsLookup[address] != 'undefined')
    return this._dnsLookup[address]
  return new Promise((resolve, reject) => dns.lookup(address, function(err, dstIP) {
    if(err) {
      return reject(err)
    }
    self._dnsLookup[address] = dstIP
    return resolve(dstIP)
  }))
}

Server.prototype._onUDPMessage = function (socket, message, rinfo) {
  // is this valid SOCKS5 for UDP?
  var returnIP = false
  var ipv6 = ip6addr.parse(rinfo.address)
  var localbytes = ipv6.toBuffer()
  if(ipv6.kind() == 'ipv4') {
    localbytes = localbytes.slice(12)
  }
  var domain = Object.keys(this._dnsLookup)
    .filter(n => this._dnsLookup[n] == rinfo.address)[0]
  var bufrep = returnIP || !domain
    ? Buffer.alloc(4 + localbytes.length + 2)
    : Buffer.alloc(5 + domain.length + 2)
  bufrep[0] = 0x00
  bufrep[1] = 0x00
  bufrep[2] = 0x00
  if(returnIP || !domain) {
    bufrep[3] = 0x01
    for (var i = 0, p = 4; i < localbytes.length; ++i, ++p)
      bufrep[p] = localbytes[i]
  } else {
    var domainBuf = Buffer.from(domain)
    bufrep[3] = 0x03
    bufrep[4] = domain.length
    domainBuf.copy(bufrep, 5)
    p = 5 + bufrep[4]
  }
  bufrep.writeUInt16BE(rinfo.port, p, true)
  socket.send(Buffer.concat([bufrep, message]))
}

Server.prototype._onUdp = function (parser, socket, onRequest, onData, udpLookupPort) {
  var self = this
  var udpSocket = this._listeners[udpLookupPort]
  var onUDPMessage = this._onUDPMessage.bind(self, socket)
  if(!udpSocket) {
    console.log('No socket found.')
    return
  }
  console.log('Switching to UDP listener', udpLookupPort)
  //socket.dstSock = udpSocket
  socket.off('data', onData)
  socket.off('message', onData)
  socket._socket.pause()
  parser.authed = true
  parser.off('request', onRequest)
  // connection and version number are implied from now on
  var newOnData = ((message) => {
    clearTimeout(self._timeouts[udpLookupPort])
    self._timeouts[udpLookupPort] = setTimeout(() => self._listeners[udpLookupPort].close(), UDP_TIMEOUT)
    var chunk = Buffer.from(message)
    if(chunk[3] === 1 || chunk[3] === 3 || chunk[3] === 4) {
      chunk[0] = 5
      chunk[1] = 1
      chunk[2] = 0
      parser._onData(chunk)
    }
  })
  var newOnRequest = async (reqInfo) => {
    clearTimeout(self._timeouts[udpLookupPort])
    self._timeouts[udpLookupPort] = setTimeout(() => self._listeners[udpLookupPort].close(), UDP_TIMEOUT)
    try {
      var dstIP = await this.lookupDNS(reqInfo.dstAddr)
      udpSocket.send(reqInfo.data, 0, reqInfo.data.length, reqInfo.dstPort, dstIP)
    } catch (err) {
      self._onProxyError(socket, err)
    }
  }
  socket.on('message', newOnData)
  udpSocket.on('message', onUDPMessage)
  udpSocket.on('close', () => {
    socket.off('data', newOnData)
    socket.off('message', newOnData)
    parser.off('request', newOnRequest)
    delete this._listeners[udpLookupPort]
    // switch back to regular messaging?
    socket.on('data', onData)
    socket.on('message', onData)
    parser.on('request', onRequest)
  })
  parser.on('request', newOnRequest)
  socket._socket.resume()
}

Server.prototype._onConnection = function(socket) {
  ++this._connections
  var parser = new Parser(socket)
      onData = parser._onData.bind(parser),
      onError = this._onParseError.bind(this, socket, onData), // data for unbinding, err passed in
      onMethods = this._onMethods.bind(this, parser, socket, onData),
      onRequest = this._onRequest.bind(this, socket, onData), // reqInfo passed in
      onClose = this._onClose.bind(this, socket, onData),
      onUdp = this._onUdp.bind(this, parser, socket, onRequest, onData)
      
  if(socket instanceof WebSocket) {
    console.log(`Websocket connection ${socket._socket.remoteAddress}....`)
    socket.on('message', onData)
  } else if (socket instanceof Socket) {
    console.log(`Net socket connection ${socket.remoteAddress}....`)
    socket.on('data', onData)
    socket.send = socket.write
    socket._socket = socket
    socket.close = socket.end
  } else {
    console.log('Socket type unknown!')
    socket.close()
    return
  }
  
  parser
    .on('error', onError)
    .on('methods', onMethods)
    .on('request', onRequest)
    .once('udp', onUdp)

  socket.on('error', this._onErrorNoop)
        .on('close', onClose)
}

Server.prototype.useAuth = function(auth) {
  if (typeof auth !== 'object'
      || typeof auth.server !== 'function'
      || auth.server.length !== 2)
    throw new Error('Invalid authentication handler')
  else if (this._auths.length >= 255)
    throw new Error('Too many authentication handlers (limited to 255).')

  this._auths.push(auth)

  return this
}

Server.prototype._onErrorNoop = function(err) {
  console.log(err)
}

Server.prototype._onSocketConnect = function(socket, reqInfo) {
  if(!socket._socket.writable) return
  var ipv6 = ip6addr.parse(this._slaves[0] || socket._socket.localAddress)
  var localbytes = ipv6.toBuffer()
  if(ipv6.kind() == 'ipv4') {
    localbytes = localbytes.slice(12)
  }
  var bufrep = Buffer.alloc(6 + localbytes.length)
  bufrep[0] = 0x05
  bufrep[1] = REP.SUCCESS
  bufrep[2] = 0x00
  bufrep[3] = (ipv6.kind() == 'ipv4' ? ATYP.IPv4 : ATYP.IPv6)
  for (var i = 0, p = 4; i < localbytes.length; ++i, ++p)
    bufrep[p] = localbytes[i]
  bufrep.writeUInt16BE(socket._socket.localPort, p, true)
  socket.send(bufrep)

  // do some new piping for the socket
  if(typeof socket.dstSock == 'function') {
    console.log('Starting pipe')
    socket._socket.pipe(socket.dstSock)
    socket.dstSock.pipe(socket._socket)
  } else {
    console.log('Starting messages ' + ipv6.kind())
    socket.send(bufrep)
  }
  socket._socket.resume()
}

Server.prototype._onProxyError = function(socket, err) {
  console.log(err)
  if(!socket._socket.writable) return
  var errbuf = Buffer.from([0x05, REP.GENFAIL])
  if (err.code) {
    switch (err.code) {
      case 'ENOENT':
      case 'ENOTFOUND':
      case 'ETIMEDOUT':
      case 'EHOSTUNREACH':
        errbuf[1] = REP.HOSTUNREACH
      break
      case 'ENETUNREACH':
        errbuf[1] = REP.NETUNREACH
      break
      case 'ECONNREFUSED':
        errbuf[1] = REP.CONNREFUSED
      break
    }
  }
  socket.send(errbuf)
  socket.close()
}

Server.prototype.tryBindPort = async function(reqInfo) {
  var self = this
  for(var i = 0; i < 10; i++) {
    try {
      var fail = false
      var portLeft = Math.round(Math.random() * 50) * 1000 + 5000
      var portRight = reqInfo.dstPort & 0xfff
      const listener = dgram.createSocket('udp4')
      console.log('Starting listener ', reqInfo.dstAddr, portLeft + portRight)
      await new Promise((resolve, reject) => listener
        .on('listening', resolve)
        .on('connection', self._onConnection)
        .on('close', () => clearTimeout(self._timeouts[reqInfo.dstPort]))
        .on('error', reject)
        .bind(portLeft + portRight, reqInfo.dstAddr))
      // TODO: fix this, port will be the same for every client
      //   client needs to request the random port we assign
      self._listeners[reqInfo.dstPort] = listener
      self._timeouts[reqInfo.dstPort] = setTimeout(() => listener.close(), UDP_TIMEOUT)
      return
    } catch(e) {
      if(!e.code.includes('EADDRINUSE')) throw e
    }
  }
  throw new Error('Failed to start UDP listener.')
}

Server.prototype.proxySocket = async function(socket, reqInfo) {
  var self = this
  var onConnect = this._onSocketConnect.bind(this, socket, reqInfo)
  var onError = this._onProxyError.bind(this, socket) // err is passed in
  try {
    var dstIP = await this.lookupDNS(reqInfo.dstAddr)
    if (reqInfo.cmd == 'udp') {
      await self.tryBindPort(reqInfo)
      onConnect()
    } else if(reqInfo.cmd == 'bind') {
      const listener = createServer()
      socket.dstSock = listener
      listener.on('connection', onConnect)
              .on('error', onError)
              .listen(reqInfo.dstPort, reqInfo.dstAddr)
    } else if(reqInfo.cmd == 'connect') {
      var dstSock = new Socket()
      socket.dstSock = dstSock
      dstSock.setKeepAlive(false)
      dstSock.on('error', onError)
             .on('connect', onConnect)
             .connect(reqInfo.dstPort, dstIP)
    // TODO: add websocket piping for quakejs servers
    } else {
      socket.send(BUF_REP_CMDUNSUPP)
      socket.close()
    }
  } catch (err) {
    self._onProxyError(socket, err)
  }
}

exports.Server = Server
