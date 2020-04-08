var Buffer = require('buffer').Buffer
var dgram = require('dgram')
var WebSocketServer = require('ws').Server
var Huffman = require('../lib/huffman.js')
var {Server} = require('../lib/socks.server.js')
var {createServer} = require('net')
var http = require('http')
var ip6addr = require('ip6addr')

var help = `
npm run proxy [options]
--help -h - print this help message and exit
--max {number} - Max number of connections for this process
--master - Designated master server, force redirection after connection
--slave - is implied from lack of --master, don't need this option
If max is set, the following number is the number of connections,
If max is not set or any number following max is assumed to be port numbers
all numbers and addresses are used as slave addresses
e.g. 127.0.0.1:1080 1081 1082 192.168.0.120 1080-1082 or CIDR notations
All of these numbers and addresses will be fully enumerated as a part of health check
TODO:
Add a command to check the number of connections so it can be used as health check
Either using the socket or event using process signals to communicate across process threads
Add websocket piping back in for quakejs servers
e.g. npm run proxy -- --max 10
`

var ports = [1081, 80]
var specifyPorts = false
var slaves = []
for(var i = 0; i < process.argv.length; i++) {
  var a = process.argv[i]
  if(a.match(/\/node$/ig)) continue
  if(a.match(/\/proxy\.js$/ig)) continue

  if(a == '--help' || a == '-h') {
		console.log(help)
    process.exit(0)
  } else if (parseInt(a) + '' === a) {
    if(!specifyPorts) {
      ports = []
      specifyPorts = true
    }
    ports.push(parseInt(a))
	} else {
    try {
      var ipv6 = ip6addr.parse(a)
      slaves.push(a)
    } catch (e) {
      console.log(`ERROR: Unrecognized option "${a}"`, e)
    }
	}
}

var socks = new Server({slaves}) // TODO: add password authentication

// redirect http attempts to loading page
/*
const server = createServer(function(socket) {
	try {
		socks._onConnection(socket)
	} catch (e) {
		console.log(e)
	}
})
server.listen(1080, () => console.log(`Server running at http://0.0.0.0:1080`))
*/

ports.forEach((p, i, ports) => {
  var httpServer = http.createServer(function(req, res) {
  	res.writeHead(200, {'Location': 'https://quake.games' + req.url})
  	res.write('It works!')
  	res.end()
  })

  var wss = new WebSocketServer({server: httpServer})

  wss.on('connection', function(ws) {
  	try {
  		socks._onConnection(ws)
  	} catch (e) {
  		console.log(e)
  	}
  })

  httpServer.listen(ports[i],  () => console.log(`Http running at http://0.0.0.0:${ports[i]}`))
})
