var glob = require('glob')
var path = require('path')
var fs = require('fs')
var minimatch = require("minimatch")

var imageTypes = [
  '.png',
  '.jpg',
  '.jpeg',
  '.tga',
  '.gif',
  '.pcx',
  '.webp',
  '.hdr',
]

var audioTypes = [
  '.opus',
  '.wav',
  '.mp3',
  '.ogg',
]

var sourceTypes = [
  '.c', // these can be compiled in game to run bot AI
  '.h',
  '.map',
  '.scc',
]

var fileTypes = [
  '.roq',
  '.cfg',
  '.qvm',
  '.bot',
  '.txt',
  '.bsp',
  '.aas',
  '.md3',
  '.md5',
  '.iqm',
  '.mdr',
  '.log',
  '.shader',
  '.crosshair',
  '.skin',
  '.pk3',
  '.font',
  '.config',
  '.menu',
  '.defi', // CPMA game mode definition
  '.arena', // map based game mode definition
]

var knownDirs = [
  'scripts',
  'botfiles',
  'fonts',
  'gfx',
  'hud',
  'icons',
  'include',
  'menu',
  'models',
  'music',
  'powerups',  // powerup shaders
  'sprites',
  'sound',
  'ui',
  'maps',
  'videos',
  'textures',
]

function findTypes(types, project) {
  if(Array.isArray(types)) types = `**/*+(${types.join('|')})`
  if(fs.existsSync(project)) {
    return glob.sync(types, {cwd: project})
      .map(f => path.join(project, f))
  } else if(Array.isArray(project)) {
    return project.filter(minimatch.filter(types))
  } else {
    throw new Error(`Don't know what to do with ${project}`)
  }
}

module.exports = {
  knownDirs,
  fileTypes,
  sourceTypes,
  audioTypes,
  imageTypes,
  findTypes,
  allTypes: [imageTypes, audioTypes, sourceTypes, fileTypes].flat(1)
}
