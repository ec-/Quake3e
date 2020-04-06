var express = require('express')
var path = require('path')
var {ufs} = require('unionfs')
var express = require('express')
var {pathToAbsolute, makeIndexJson, repackPk3Dir} = require('./content.js')
var {sendCompressed} = require('./compress.js')
var serveStatic = require('../lib/serve-static.js')

var app = express()
app.enable('etag')
express.static.mime.types['wasm'] = 'application/wasm'
express.static.mime.types['pk3'] = 'application/octet-stream'
express.static.mime.types['bsp'] = 'application/octet-stream'

function pathToDirectoryIndex(url) {
  const parsed = new URL(`https://local${url}`)
  var absolute = pathToAbsolute(decodeURIComponent(parsed.pathname))
  // return index.json for directories or return a file out of baseq3
  var filename
  if(absolute
    && ufs.existsSync(absolute)
    && ufs.statSync(absolute).isDirectory()) {
    filename = path.join(parsed.pathname, 'index.json')
    absolute = path.join(absolute, 'index.json')
  } else if (absolute
    && ufs.existsSync(path.dirname(absolute))
    && ufs.statSync(path.dirname(absolute)).isDirectory()
    && parsed.pathname.match(/\/index\.json$/ig)) {
    filename = decodeURIComponent(parsed.pathname)
  }
  return {filename, absolute}
}

async function serveUnionFs(req, res, next) {
  var {filename, absolute} = pathToDirectoryIndex(req.url)
  if(filename) {
    await makeIndexJson(filename, absolute)
  }
  if ((absolute + 'dir').includes('.pk3dir')
    && ufs.existsSync(absolute + 'dir')) {
    await repackPk3Dir(absolute + 'dir')
  }
  if (absolute && ufs.existsSync(absolute)) {
    sendCompressed(absolute, res, req.headers['accept-encoding'])
  } else {
    console.log(`Couldn't find file "${req.url}" "${absolute}".`)
		next()
	}
}

app.use(serveStatic(path.join(__dirname), {
  setHeaders: (res, path) => {
    res.setHeader('Access-Control-Allow-Origin', '*')
  }
}))
app.use(serveStatic(path.join(__dirname, '../../../build/release-js-js'), {
  setHeaders: (res, path) => {
    res.setHeader('Access-Control-Allow-Origin', '*')
  }
}))
app.use(serveUnionFs)

app.listen(8080)
