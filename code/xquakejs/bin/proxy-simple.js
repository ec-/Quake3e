var Buffer = require('buffer').Buffer;
var dgram = require('dgram');
var WebSocketServer = require('ws').Server;
const http = require('http');
const server = http.createServer(function(req, res) {
	res.writeHead(200, {'Location': 'http://quakeiiiarena.com' + req.url});
	res.write('It works!')
	res.end();
});

var wss = new WebSocketServer({server});

// TODO: rewrite this to use node dns
var SERVER_IP = '127.0.0.1'
var SERVER_PORT = 27961
 
wss.on('error', function(error) {
	console.log("error", error);
});
 
wss.on('connection', function(ws) {
	try {
	
		console.log('on connection....');
		//Create a udp socket for this websocket connection
		var udpClient = dgram.createSocket('udp4');
		
		//ws.send("HAIIII");
		
		//When a message is received from udp server send it to the ws client
		udpClient.on('message', function(msg, rinfo) {
			//console.log("udp -> ws", msg);
			try {
				ws.send(msg);
			} catch(e) {
				console.log(`ws.send(${e})`)
			}
			//ws.send("test");
		});
	 
		//When a message is received from ws client send it to udp server.
		ws.on('message', function(message) {
			var msgBuff = new Buffer.from(message);
			try {
				// TODO: sniff websocket connection to figure out how to intercept the server address
				// emscripten qcommon/huffman.c and decode the message
				// add cl_main.c getchallenge/connect net_ip to control this proxy server
				// this will allow clients to control the proxy server to connect
				//   to any server they want from the browser
				//if(msgBuff.toString('utf-8').includes('connect ')) {
				//	debugger;
				//}
				udpClient.send(msgBuff, 0, msgBuff.length, SERVER_PORT, SERVER_IP);
			} catch(e) {
				console.log("udpClient.send")
			}
			//console.log("ws -> udp", message);
		});
	} catch (e) {
		console.log(e);
	}
});

server.listen(27961, () => console.log(`Server running at http://0.0.0.0:27961`));
