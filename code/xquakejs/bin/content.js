var fs = require('fs')
var path = require('path')
var {URL} = require('url')
var {Volume} = require('memfs')
var {ufs} = require('unionfs')
var { Readable } = require('stream')
var {compressFile, compressDirectory} = require('./compress.js')

var help = `
npm run start [options] [virtual path] [filesystem path]
NOTE: ./build/release-js-js is implied
--recursive -R - adds all directory files below current directory
--virtual -V - create virtual pk3dir out of pk3 and exclude pk3 files, opposite of repack
--write -wr - write all JSON files in every directory for CDN use
--repack -rp - repack on the fly as pk3/media/images/sound files are accessed
  opposit of pk3dir
--hidden -H - include hidden files (uncommon)
--watch - watch files for changes
--help -h - print this help message and exit
e.g. npm run start -- -R -rp /assets/baseq3 /Applications/ioquake3/baseq3
`

var recursive = false
var writeOut = false
var repackFiles = false
var virtualPk3dir = false
var runContentGeneration = false
var includeHidden = false
var watchChanges = false

// check the process args for a directory to serve as the baseq3 folders
var vol = Volume.fromJSON({})
ufs.use(fs).use(vol)
var mountPoint = '/assets/baseq3'
var mountPoints = []
for(var i = 0; i < process.argv.length; i++) {
  var a = process.argv[i]
  if(a.match(/\/node$/ig)) continue
  if(a.match(/\/web\.js$/ig)) continue
  if(fs.existsSync(a)) {
    // if running content script directly, automatically call each mount point 
    //   so the json files and zipped files can be generated
    if(a.match(/\/content\.js$/ig)) {
      console.log('Running content script')
      runContentGeneration = true
      continue
    }
		console.log(`Linking ${mountPoint} -> ${a}`)
    // create a link for user specified directory that doesn't exist
    mountPoints.push([mountPoint, a])
  // use an absolute path as a mount point if it doesn't exist
  } else if(a == '--recursive' || a == '-R') {
    console.log('Recursive')
    recursive = true
  } else if(a == '--hidden' || a == '-H') {
    console.log('Hidden files')
    includeHidden = true
  } else if(a == '--virtual' || a == '-V') {
    console.log('Virtual pk3dirs')
    virtualPk3dir = true
  } else if(a == '--write' || a == '-wr') {
    console.log('Writing manifest.json, not watching')
    writeOut = true
    watchChanges = false
  } else if(a == '--watch') {
    console.log('Watching for changes, not writing')
    watchChanges = true
    writeOut = false
  } else if(a == '--repack' || a == '-rp') {
    console.log('Live repacking')
    repackFiles = true
  } else if (a.match(/^\//i)) {
		console.log('Using mount point ' + a)
    mountPoint = a
  } else if (a == '--help' || a == '-h') {
    console.log(help)
    process.exit(0)
  } else {
    console.log(`ERROR: Unrecognized option "${a}"`)
  }
}
if(mountPoints.length === 0) {
  console.log('ERROR: No mount points, e.g. run `npm run start /Applications/ioquake3`')
}
mountPoints.sort((a, b) => a[0].localeCompare(b[0], 'en', { sensitivity: 'base' }))

function watchForChanges() {
  var chokidar = require('chokidar');
  var watcher = chokidar.watch(mountPoints.map(m => m[1] + '/**'), {
    interval: 1000,
    atomic: 1000,
    awaitWriteFinish: true
  })
  var doing = false
  watcher.on('change', function(changePath) {
    if(doing) return
    doing = true
    // remove all cache files from the directory tree
    var keys = Object.keys(vol.toJSON())
    for(var i = 0; i < mountPoints.length; i++) {
      if(changePath.includes(mountPoints[i][1])) {
        // remove all files in the affected mount point
        console.log(`Changes detected in ${mountPoints[i][1]}, unlinking...`)
        for(var j = 0; j < keys.length; j++) {
          if(keys[j].includes(mountPoints[i][1])) {
            try {
              // remove memfs cache of files
              vol.unlinkSync(keys[j])
            } catch (e) {
              // already removed?
              if(!e.code == 'ENOENT') throw e
            }
          }
        }
      }
    }
    doing = false
  })
}
if(watchChanges) {
  watchForChanges()
}

function pathToAbsolute(virtualPath) {
  var result
	for(var i = 0; i < mountPoints.length; i++) {
		if(virtualPath.includes(mountPoints[i][0])) {
      result = path.join(mountPoints[i][1],
        virtualPath.replace(mountPoints[i][0], ''))
      if(ufs.existsSync(result)) {
        return result
      }
		}
	}
  return result
}

function readMultiDir(fullpath, forceRecursive) {
	var dir = []
  // skip pk3dirs in repack mode because they will be zipped by indexer
  if(repackFiles && !forceRecursive
    && fullpath.includes('.pk3dir')
    && ufs.statSync(fullpath).isDirectory()) {
    return dir
  }
  if(ufs.existsSync(fullpath)) {
    var files = ufs.readdirSync(fullpath)
      .map(f => path.join(fullpath, f))
      .filter(f => includeHidden || path.basename(f)[0] != '.')
    dir.push.apply(dir, files)
    if(recursive || forceRecursive) {
      for(var j = 0; j < files.length; j++) {
        if(ufs.statSync(files[j]).isDirectory()) {
          var moreFiles = readMultiDir(files[j], forceRecursive)
          dir.push.apply(dir, moreFiles)
        }
      }
    }
  } else {
    throw new Error(`Cannot find directory ${fullpath}`)
  }
	return dir
}

async function repackPk3Dir(fullpath) {
  if(!repackFiles) {
    return
  }
  if(!ufs.existsSync(fullpath) || !ufs.statSync(fullpath).isDirectory()) {
    throw new Error(`Provided path ${fullpath} is not a directory.`)
  }
  var newPk3 = fullpath.replace('.pk3dir', '.pk3')
  vol.mkdirpSync(path.dirname(fullpath))
  if(!ufs.existsSync(fullpath.replace('.pk3dir', '.pk3')) || writeOut) {
    console.log(`archiving ${newPk3}`)
    await compressDirectory(
      readMultiDir(fullpath, true),
      vol.createWriteStream(newPk3),
      fullpath
    )
  }
  return await compressFile(newPk3, vol)
}

async function cacheFile(fullpath) {
  vol.mkdirpSync(path.dirname(fullpath))
  return await compressFile(fullpath, vol)
}

async function makeIndexJson(filename, absolute) {
  // if there is no index.json, generate one
  if(filename && !ufs.existsSync(absolute)) {
    console.log(`Creating directory index ${absolute}`)
		var files = readMultiDir(path.dirname(absolute), recursive && !repackFiles)
		var manifest = {}
		for(var i = 0; i < files.length; i++) {
			var fullpath = files[i]
			if(!ufs.existsSync(fullpath)) continue
			var file = {}
      if(virtualPk3dir
        && fullpath.includes('.pk3')
        && ufs.statSync(fullpath).isFile()) {
        var filesInZip = await readPak(fullpath, progress)
        filesInZip.forEach(entry => {
          manifest[path.join(fullpath, entry.name)] = {
            compressed: entry.compressedSize,
            name: path.join(path.basename(fullpath), entry.name),
            size: entry.size,
            offset: entry.offset
          }
        })
        file = {name: fullpath.replace('.pk3', '.pk3dir')}
      } else if(ufs.statSync(fullpath).isFile()) {
        //if(writeOut) {
        //  file = await cacheFile(fullpath)
        //} else {
          file = {size: ufs.statSync(fullpath).size}
        //}
			} else if(repackFiles
        && fullpath.includes('.pk3dir')
        && ufs.statSync(fullpath).isDirectory()) {
        // only make the pk3 if we are intentionally writing or it doesn't already exist
        file = await repackPk3Dir(fullpath)
        fullpath = fullpath.replace('.pk3dir', '.pk3')
      }
      
      var key = fullpath.replace(
        path.dirname(absolute),
        '/base/' + path.basename(path.dirname(absolute)))
        .toLowerCase()
      if(typeof file.size == 'undefined') {
        key += '/'
      }
			manifest[key] = Object.assign({
        name: fullpath.replace(path.dirname(absolute), '')
      }, file)
		}
    console.log(`Writing directory index ${absolute}`)
    var writefs = writeOut ? fs : vol
		vol.mkdirpSync(path.dirname(absolute))
    writefs.writeFileSync(absolute, JSON.stringify(manifest, null, 2))    
  }
}

async function runContent() {
  if(runContentGeneration) {
    for(var i = 0; i < mountPoints.length; i++) {
      var absolute = pathToAbsolute(mountPoints[i][0])
      await makeIndexJson(mountPoints[i][0], absolute + '/index.json')
    }
  }
}

runContent().catch(e => console.log(e))

module.exports = {
	makeIndexJson,
	pathToAbsolute,
  repackPk3Dir,
}
