var fs = require('fs')
var os = require('os')
var path = require('path')
var glob = require('glob')
var assert = require('assert')
var minimatch = require('minimatch')
var {ufs} = require('unionfs')
ufs.use(fs)

var PROJECT = '/Applications/ioquake3/baseq3'
var PAK_NAME = path.join(__dirname, 'previous-pak.json')
var INFO_NAME = path.join(__dirname, 'previous-info.json')
var INDEX_NAME = path.join(__dirname, 'previous-index.json')
var STEPS = {
  'source': 'Load Pk3 sources',
  'graph': 'Create a graph',
  'info': 'Print game info',
  'convert': 'Convert web format',
  'repack': 'Zip new paks',
  'clean': 'Cleaning up'
}

var help = `
npm run repack [options] [mod directory]
--edges {n} - (default is 3) number of connected edges to deserve it's own pk3
--roots - insert yourself anywhere in the graph, show top connections from that asset
--info -i - only print info, don't actually do any converting
--convert {args} - options to pass to image magick, make sure to put these last
--transcode {args} - options to pass to opus/ogg vorbis, make sure to put these last
--entities {ent.def/file} - entities definition to group models and sounds
--no-progress - turn off the progress bars for some sort of admining
--previous {optional file or previous-graph.js} -p - try to load information
  from the previous graph so we don't have to do step 1
--temp {directory}
--verbose -v - print all percentage status updates in case there is an error
--help -h - print this help message and exit
--no-overwrite - don't overwrite files during conversion, TODO: during unzipping either
--whitelist - TODO: force include matching files with menu/game like sarge/major/footsteps,
  and anything else found with in the logs matching "R_FindImageFile could not find|Warning: Failed to load sound"
--no-graph - skip graphing, just run the convert and put files back into pk3s like they were,
  works nicely with --virtual open on content server
e.g. npm run repack -- /Applications/ioquake3/baseq3
npm run repack -- --info
TODO:
--no-deduplicate - utility, unzip to a combined directory to remove duplicate/overrides
--collisions - skip unzipping and repacking, just list files that interfere with each other
Better graph mode that ensures all files are present and allows clients to download entire pk3 based on indexed file it needs
`

// in order of confidence, most to least
var numericMap = [
  ['menu', 1], // 1 - ui.qvm, menu system to get the game running, all scripts
               // 12-19 - menu pk3s, hud, sprites
  ['game', 2], // 2 - cgame.qvm, qagame.qvm, in game feedback sounds not in menu
  ['maps', 9], // 90-99 - map textures
  ['weapon', 5], // 50-59 - weapons 1-9
  ['mapobject', 7], // misc_models
  ['powerup', 3], // 30-39 - powerups
  ['player', 4], // 40-49 - player models
  ['model', 6],
  ['other', /.*/, 8], // 80-89 - map models
]

// minimatch/globs
var whitelist = {
  'baseq3': [
    '**/+(sarge|major)/**',
    '**/player/*',
    '**/player/footsteps/*',
    '**/weapons2/+(machinegun|gauntlet)/**',
    '**/weaphits/**',
    '**/scripts/*.shader',
  ],
  'missionpack': [
    '**/+(james|janet|sarge)/**',
    '**/player/*',
    '**/player/footsteps/*',
    '**/weapons2/+(machinegun|gauntlet)/**',
    '**/weaphits/**',
    '**/scripts/*.shader',
    '**/ui/assets/**',
  ],
  'baseoa': [
    '**/+(sarge|major)/**',
    '**/player/*',
    '**/player/footsteps/*',
    '**/weapons2/+(machinegun|gauntlet)/**',
    '**/weaphits/**',
    '**/scripts/*.shader',
  ],
  'baseq3r': [
    '**/+(player|players)/sidepipe/**',
    '**/+(player|players)/heads/doom*',
    '**/+(player|players)/plates/**',
    '**/+(player|players)/wheels/*cobra*',
    '**/player/*',
    '**/player/footsteps/*',
    '**/weaphits/**',
    '**/scripts/*.shader',
  ],
  'q3ut4': [
    '**/+(athena)/**',
    '**/player/*',
    '**/player/footsteps/*',
    '**/weapons2/+(handskins)/**',
    '**/weaphits/**',
    '**/scripts/*.shader',
  ]
}

var edges = 3
var noProgress = false
var convert = ''
var transcode = ''
var entities = ''
var mountPoints = []
var usePrevious = false
var noOverwrite = false
var noGraph = false
var noDedupe = false
var TEMP_DIR = os.tmpdir()
var verbose = false

var isConvertParams = false
var isTranscodeParams = false
for(var i = 0; i < process.argv.length; i++) {
  var a = process.argv[i]
  if(a.match(/\/node$/ig)) continue
  if(a.match(/\/repack\.js$/ig)) continue
  if(ufs.existsSync(a) && ufs.statSync(a).isDirectory(a)) {
    mountPoints.push(a)
    continue
  } else if(a == '--edges') {
    edges = parseInt(process.argv[i+1])
    console.log(`Grouped edges by ${edges}`)
    i++
  } else if (a == '--no-overwrite') {
    console.log('No over-writing existing files')
    noOverwrite = true
  } else if (a == '--no-graph') {
    console.log('No graphing files, no info, just convert and repack')
    noGraph = true
    delete STEPS['graph']
    delete STEPS['info']
  } else if (a == '--info' || a == '-i') {
    console.log('Displaying pak info')
    delete STEPS['convert']
    delete STEPS['repack']
  } else if (a == '--no-progress') {
    console.log('No progress bars')
    noProgress = true
  } else if (a == '--convert') {
    isConvertParams = true
  } else if (a == '--entities') {
    if(ufs.existsSync(process.argv[i+1])) {
      entities = ufs.readFileSync(process.argv[i+1]).toString('utf-8')
      // TODO: need some basic parsing to get the part before _ of every entity name
      i++
    } else {
      console.error(`ERROR: entities def ${process.argv[i+1]} not found`)
    }
  } else if (a == '--previous' || a == '-p') {
    console.log('Using previous graph')
    if(ufs.existsSync(process.argv[i+1])) {
      usePrevious = process.argv[i+1]
      i++
    } else {
      usePrevious = true
    }
  } else if (a == '--temp') {
    if(ufs.existsSync(process.argv[i+1]) && ufs.statSync(process.argv[i+1].isDirectory())) {
      TEMP_DIR = process.argv[i+1]
      console.log(`Temporary directory set to ${TEMP_DIR}`)
      i++
    } else {
      throw new Error(`Temp directory ${process.argv[i+1]} not found or not a directory`)
    }
  } else if (a == '--verbose' || a == '-v') {
    verbose = true
  } else if (a == '--help' || a == '-h') {
    console.log(help)
    process.exit(0)
  } else if(isConvertParams) {
    convert += ' ' + a
    continue
  } else if(isTranscodeParams) {
    transcode += ' ' + a
    continue
  } else {
    console.error(`ERROR: Unrecognized option "${a}"`)
  }
  isConvertParams = false
  isTranscodeParams = false
}
if(typeof STEPS['convert'] != 'undefined') {
  delete STEPS['info']
}
if(noGraph && typeof STEPS['convert'] == 'undefined') {
  console.warn('Can\'t generate info with --no-graph option')
}
if(noGraph && usePrevious) {
  console.warn('Can\'t use previous graph because not graphing, use --no-overwrite to speed up extraction')
} else if(usePrevious) {
  delete STEPS['source']
}
if(mountPoints.length == 0) {
  console.error('ERROR: No mount points, e.g. run `npm run repack /Applications/ioquake3/baseq3`')
  if(ufs.existsSync(PROJECT))
    mountPoints.push(PROJECT)
} else {
  mountPoints.sort((a, b) => a.localeCompare(b, 'en', { sensitivity: 'base' }))
}
for(var i = 0; i < mountPoints.length; i++) {
  var name = path.basename(mountPoints[i])
  console.log(`Repacking directory ${mountPoints[i]} -> ${path.join(TEMP_DIR, name + '-ccr')}`)
}
try {
  require.resolve('cli-progress');
} catch (err) {
  noProgress = true
}
if (!process.stdout.isTTY && !noProgress) {
  console.log('WARNING not a tty, using --no-progress')
  noProgress = true
}
if(!noProgress) {
  var cliProgress = require('cli-progress')
  var multibar = new cliProgress.MultiBar({
      fps: 120,
      clearOnComplete: true,
      hideCursor: true,
      format: `[\u001b[34m{bar}\u001b[0m] {percentage}% | {value}/{total} | {message}`,
      barCompleteChar: '\u2588',
      barIncompleteChar: '\u2588',
      barGlue: '\u001b[33m',
      forceRedraw: true,
      linewrap: null,
      barsize: 30,
  })
  var oldConsole = console
  function resetRedraw(out) {
    var args = Array.from(arguments).slice(1)
    multibar.terminal.cursorRelativeReset()
    multibar.terminal.clearBottom()
    multibar.terminal.lineWrapping(true)
    oldConsole[out].apply(oldConsole, args)
    multibar.update()
  }
  console = {
    log: resetRedraw.bind(null, 'log'),
    error: resetRedraw.bind(null, 'error'),
    info: resetRedraw.bind(null, 'info'),
  }
}

// load these modules down here to the console is overridden
var {
  loadQVM, loadQVMData, getGameAssets, mapGameAssets, MATCH_ENTS
} = require('../lib/asset.qvm.js')
var {
  deDuplicate, graphGame, graphModels, graphMaps, graphShaders, TEMP_NAME
} = require('../lib/asset.game.js')
var {compressDirectory, unpackPk3s} = require('../bin/compress.js')
var {
  findTypes, fileTypes, sourceTypes,
  audioTypes, imageTypes, findTypes,
  allTypes
} = require('../bin/repack-whitelist.js')
var {
  convertGameFiles, convertNonAlpha, convertAudio
} = require('../bin/convert.js')
var {getLeaves} = require('../lib/asset.graph.js')

var globalBars = []

function getPercent(l, a, b) {
  return `${l}: ${a}/${b} - ${Math.round(a/b*1000) / 10}%`
}

function percent(l, a, b) {
  console.log(getPercent(l, a, b))
}

async function progress(bars, forceVerbose) {
  if(bars === false) {
    for(var i = 0; i < globalBars.length; i++) {
      if(!globalBars[i]) continue
      globalBars[i].stop()
    }
    multibar.stop()
    await new Promise(resolve => setTimeout(resolve, 10))
  }
  //e.g. [[1, 0, 10, 'Removing temporary files']]
  for(var i = 0; i < bars.length; i++) {
    if(!multibar) {
      if(bars[i][1] === false) continue
      percent(bars[i][3], bars[i][1], bars[i][2])
      continue
    }
    if(bars[i][1] === false) {
      if(typeof globalBars[bars[i][0]] != 'undefined') {
        globalBars[bars[i][0]].stop()
        multibar.remove(globalBars[bars[i][0]])
      }
      globalBars[bars[i][0]] = void 0
      await new Promise(resolve => setTimeout(resolve, 10))
      continue
    }
    if(verbose || forceVerbose) {
      // print it out so we can see a record too
      percent(bars[i][3], bars[i][1], bars[i][2])
    }
    var info = {
      percentage: Math.round(bars[i][1]/bars[i][2]*1000) / 10,
      value: bars[i][1],
      total: bars[i][2],
      message: bars[i][3]
    }
    if(typeof globalBars[bars[i][0]] == 'undefined') {
      globalBars[bars[i][0]] = multibar.create()
      globalBars[bars[i][0]].start(bars[i][2], bars[i][1], info)
      await new Promise(resolve => setTimeout(resolve, 10))
    } else {
      globalBars[bars[i][0]].setTotal(bars[i][2])
      globalBars[bars[i][0]].update(bars[i][1], info)
    }
    await new Promise(resolve => setTimeout(resolve, 1))
  }
}

async function gameInfo(gs, project) {
  var game
  if(!gs.graph) {
    game = await graphGame(gs, project, progress)
  } else {
    game = gs
  }
  // how many files are matched versus unknown?
  game.images = game.images || findTypes(imageTypes, game.everything)
  game.audio = game.audio || findTypes(audioTypes, game.everything)
  game.sources = game.sources || findTypes(sourceTypes, game.everything)
  game.files = game.files || findTypes(fileTypes, game.everything)
  game.known = game.known || findTypes(allTypes, game.everything)
  var unrecognized = game.everything.filter(f => !game.known.includes(f))
  
  // how many files a part of menu system?  
  var uiqvm = getLeaves(game.graph.getVertices()
    .filter(v => v.id.match(/ui\.qvm/i)))
    .filter(e => game.everything.includes(e))
  var cgame = getLeaves(game.graph.getVertices()
    .filter(v => v.id.match(/cgame\.qvm/i)))
    .filter(e => game.everything.includes(e))
  var qagame = getLeaves(game.graph.getVertices()
    .filter(v => v.id.match(/qagame\.qvm/i)))
    .filter(e => game.everything.includes(e))
  var qvmFiles = game.everything
    .filter(f => uiqvm.includes(f) || cgame.includes(f) || qagame.includes(f))
  
  var mapFiles = Object.values(game.maps).flat(1)
    .concat(Object.values(game.mapEntities).flat(1))
    .concat(Object.values(game.shaders).flat(1))
    .concat(Object.values(game.qvms).flat(1))
  var exludingMap = game.notfound.filter(n => !mapFiles.includes(n))

  // largest matches, more than 5 edges?
  var vertices = game.graph.getVertices()
  vertices.sort((a, b) => b.inEdges.length - a.inEdges.length)
  
  // how many packs to create?
  var filesOverLimit = getLeaves(vertices
    .filter(v => v.inEdges.filter((e, i, arr) => arr.indexOf(e) === i).length > edges))
    .filter(f => game.everything.includes(f))
  
  // how many files are graphed versus unmatched or unknown?
  var leastUsed = vertices
    .filter(v => v.inEdges.length > 0 && !v.id.match(/(\.bsp|\.md3|\.qvm|\.aas)/i))
  leastUsed.sort((a, b) => a.inEdges.length - b.inEdges.length)
  var leastUsedExcept = vertices
    .filter(v => v.inEdges.length > 0 && v.id.match(/(\.md3)/i) && !v.id.match(/(players)/i))
  leastUsedExcept.sort((a, b) => a.inEdges.length - b.inEdges.length)
  
  var allShaders = Object.values(game.scripts).flat(1)
  var unused = vertices
    .filter(v => v.inEdges.length == 0
      && !v.id.match(/(\.bsp|\.md3|\.qvm|\.shader)/i)
      && !allShaders.includes(v.id))
  
  var allVertices = vertices.map(v => v.id)
  var graphed = game.everything.filter(e => allVertices.includes(e))
  var ungraphed = game.everything.filter(e => !allVertices.includes(e))
  
  var report = {
    summary: {
      images: getPercent('Image files', game.images.length, game.everything.length),
      audio: getPercent('Audio files', game.audio.length, game.everything.length),
      sources: getPercent('Source files', game.sources.length, game.everything.length),
      files: getPercent('Known files', game.files.length, game.everything.length),
      known: getPercent('Recognized files', game.known.length, game.everything.length),
      unrecognized: getPercent('Unrecognized files', unrecognized.length, game.everything.length),
      uiqvm: getPercent('UI files', uiqvm.length, game.everything.length),
      cgame: getPercent('CGame files', cgame.length, game.everything.length),
      qagame: getPercent('QAGame files', qagame.length, game.everything.length),
      qvmFiles: getPercent('Total QVM files', qvmFiles.length, game.everything.length),
      notfound: `Missing/not found: ${game.notfound.length}`,
      excludingMap: `Missing/not found excluding map/shaders: ${exludingMap.length}`,
      baseq3: `Files in baseq3: ${game.baseq3.length}`,
      vertices: 'Most used assets: ' + vertices.length,
      filesOverLimit: getPercent('Shared files', filesOverLimit.length, game.everything.length),
      leastUsed: 'Least used assets: ' + leastUsed.length,
      leastUsedExcept: 'Least used models: ' + leastUsedExcept.length,
      unused: 'Unused assets:' + unused.length,
      graphed: getPercent('All graphed', graphed.length, game.everything.length),
      ungraphed: getPercent('Ungraphed', ungraphed.length, game.everything.length),
    },
    images: game.images,
    audio: game.audio,
    sources: game.sources,
    files: game.files,
    known: game.known,
    unrecognized: unrecognized,
    uiqvm: uiqvm,
    cgame: cgame,
    qagame: qagame,
    qvmFiles: qvmFiles,
    notfound: game.notfound,
    excludingMap: exludingMap,
    baseq3: game.baseq3,
    vertices: vertices
      .map(v => v.inEdges.filter((e, i, arr) => arr.indexOf(e) === i).length + ' - ' + v.id),
    filesOverLimit: filesOverLimit,
    leastUsed: leastUsed
      .map(v => v.inEdges.length + ' - ' + v.id + ' - ' + v.inEdges.map(e => e.outVertex.id).join(', ')),
    leastUsedExcept: leastUsedExcept
      .map(v => v.inEdges.length + ' - ' + v.id + ' - ' + v.inEdges.map(e => e.outVertex.id).join(', ')),
    unused: unused.map(v => v.id),
    graphed: graphed,
    ungraphed: ungraphed,
  }
  
  var keys = [
    'images', 'audio', 'sources', 'files', 'known',
    ['unrecognized', 10],
    'uiqvm', 'cgame', 'qagame', 'qvmFiles',
    ['notfound', 10],
    ['excludingMap', 10],
    ['baseq3', 10],
    ['vertices', 10],
    'filesOverLimit',
    ['leastUsed', 10], ['leastUsedExcept', 10],
    ['unused', 10],
    'graphed',
    ['ungraphed', 10],
  ]
  keys.forEach(k => {
    console.log(report.summary[Array.isArray(k) ? k[0] : k])
    if(Array.isArray(k) && k[1] && k[1] > 0) {
      console.log(report[k[0]].slice(0, k[1]))
    }
  })
  
  console.log(`Info report written to "${INFO_NAME}"`)
  ufs.writeFileSync(INFO_NAME, JSON.stringify(report, null, 2))
  
  return gs
}

async function groupAssets(gs, project) {
  var game
  if(!gs.graph) {
    game = await graphGame(gs, project, progress)
  } else {
    game = gs
  }
  var vertices = game.graph.getVertices()
  var grouped = {'menu/menu': [], 'game/game': []}
  
  // group all entities
  var entityDuplicates = Object.keys(game.entities)
    .map(ent => game.graph.getVertex(ent))
    .filter(v => v)
    .map(v => getLeaves(v))
    .flat(1)
    .filter((f, i, arr) => arr.indexOf(f) !== i)
  Object.keys(game.entities).forEach(ent => {
    var v = game.graph.getVertex(ent)
    if(!v) return true
    var entFiles = getLeaves(v).filter(f => game.everything.includes(f)
      && !entityDuplicates.includes(f))
    var model = entFiles.filter(f => f.match(/\.md3/i))[0] || entFiles[0] || ent
    var pakClass = numericMap
      .filter(map => map.filter((m, i) => i < map.length - 1
        && model.match(new RegExp(m))).length > 0)[0][0]
    var pakKey = pakClass + '/' + ent.split('_')[1]
    if(typeof grouped[pakKey] == 'undefined') {
      grouped[pakKey] = []
    }
    grouped[pakKey].push.apply(grouped[pakKey], entFiles)
  })
  
  // group players with sounds
  // TODO: use graph for player models instead,
  //   incase they are not all in the correctly named folders
  game.everything.filter(minimatch.filter('**/+(player|players)/**'))
    .forEach(f => {
      var player = path.basename(path.dirname(f))
      //model and sound matched
      if(typeof grouped['player/' + player] === 'undefined') {
        grouped['player/' + player] = []
      }
      grouped['player/' + player].push(f)
    })

  // all current assets that shouldn't be included in menu/cgame by default
  var externalAssets = Object.values(grouped).flat(1)
  
  // group shared textures and sounds by folder name
  var filesOverLimit = getLeaves(vertices
    .filter(v => v.inEdges.filter((e, i, arr) => 
      arr.indexOf(e) === i).length > edges
      || v.id.match(/\.menu/i)))
    .concat(entityDuplicates)
    .concat(Object.values(game.menus).flat(1))
    .filter((f, i, arr) => arr.indexOf(f) === i
      && game.everything.includes(f) && !externalAssets.includes(f)
      && !f.match(/maps\//i))
  filesOverLimit.forEach(f => {
    var pakName = path.basename(path.dirname(f))
    var pakClass = numericMap
      .filter(map => map.filter((m, i) => i < map.length - 1
        && f.match(new RegExp(m))).length > 0)[0][0]
    if(pakName == path.basename(project)) {
      pakName = pakClass
    }
    if(typeof grouped[pakClass + '/' + pakName] == 'undefined') {
      grouped[pakClass + '/' + pakName] = []
    }
    grouped[pakClass + '/' + pakName].push(f)
  })

  var externalAndShared;
  
  // map menu and cgame files
  var qvms = Object.keys(game.qvms)
    .filter(qvm => qvm.match(/ui.qvm/i))
    .concat(Object.keys(game.qvms)
    .filter(qvm => !qvm.match(/ui.qvm/i)))
  qvms.forEach(qvm => {
    // update shared items so menu is downloaded followed by cgame, and not redownloadin menu assets
    externalAndShared = Object.values(grouped).flat(1)
    var className = qvm.match(/ui.qvm/i) ? 'menu' : 'game'
    var gameAssets = mapGameAssets(game.graph.getVertex(qvm))
      .filter(f => game.everything.includes(f)
        && !externalAndShared.includes(f) && !f.match(/maps\//i))
    gameAssets.forEach(f => {
      var pakName = path.basename(path.dirname(f))
      if(pakName == path.basename(project)) {
        pakName = className
      }
      if(typeof grouped[className + '/' + pakName] == 'undefined') {
        grouped[className + '/' + pakName] = []
      }
      grouped[className + '/' + pakName].push(f)
    })
  })
  
  externalAndShared = Object.values(grouped).flat(1)
  
  // group all map models and textures by map name
  Object.keys(game.maps)
    .map(m => game.graph.getVertex(m))
    .forEach(v => {
      var map = path.basename(v.id).replace(/\.bsp/i, '')
      var mapAssets = getLeaves(v)
        .filter(f => game.everything.includes(f) && !externalAndShared.includes(f))
      if(map.includes('mpterra2')) {
        console.log(getLeaves(v).filter(f => f.includes('pjrock12b_2')))
        console.log(externalAndShared.filter(f => f.includes('nightcity')))
      }
      if(mapAssets.length > 0) {
        grouped['maps/' + map] = mapAssets
      }
    })

  // make sure lots of items are linked
  var groupedFlat = Object.values(grouped).flat(1)
  var linked = game.everything.filter(e => groupedFlat.includes(e))
  var unlinked = game.everything.filter(e => !groupedFlat.includes(e))
  percent('Linked assets', linked.length, game.everything.length)
  percent('Unlinked assets', unlinked.length, game.everything.length)
  console.log(unlinked.slice(0, 10))
  
  // regroup groups with only a few files
  var condensed = Object.keys(grouped).reduce((obj, k) => {
    var newKey = k.split('/')[0]
    if((grouped[k].length <= edges || k.split('/')[1] == newKey)
      // do not merge map indexes for the sake of FS_InMapIndex lookup
      // all BSPs and players must be downloaded seperately
      && grouped[k].filter(f => f.match(/maps\/|players\/|player\//i)).length === 0) {
      if(typeof obj[newKey] == 'undefined') {
        obj[newKey] = []
      }
      obj[newKey].push.apply(obj[newKey], grouped[k])
    } else {
      if(typeof obj[k] == 'undefined') {
        obj[k] = []
      }
      obj[k].push.apply(obj[k], grouped[k])
    }
    return obj
  }, {})
  
  // make sure condensing worked properly
  var condensedFlat = Object.values(condensed).flat(1)
  var condensedLinked = game.everything.filter(e => condensedFlat.includes(e))
  assert(condensedLinked.length === linked.length, 'Regrouped length doesn\'t match linked length')
  
  // regroup by numeric classification
  var numeralCounts = {}
  var renamed = Object.keys(condensed).reduce((obj, k) => {
    var pakClass = numericMap
      .filter(map => map.filter((m, i) => i < map.length - 1
        && k.match(new RegExp(m))).length > 0)[0]
    var numeral = pakClass[pakClass.length - 1]
    if(typeof numeralCounts[numeral] == 'undefined') numeralCounts[numeral] = 1
    else numeralCounts[numeral]++
    var pakNumeral = getSequenceNumeral(numeral, numeralCounts[numeral])
    var pakName = k.split('/').length === 1
      ? (pakNumeral + k)
      : (pakNumeral + k.split('/')[1])
    obj[ 'pak' + pakName] = condensed[k]
    return obj
  }, {})
  
  // make sure renaming worked properly
  var renamedFlat = Object.values(renamed).flat(1)
  var renamedLinked = game.everything.filter(e => renamedFlat.includes(e))
  assert(renamedLinked.length === linked.length, 'Renamed length doesn\'t match linked length')
  
  // make sure there are no duplicates
  var duplicates = Object.values(renamed).flat(1).filter((f, i, arr) => arr.indexOf(f) !== i)
  console.log('Duplicates found: ' + duplicates.length, duplicates.slice(0, 10))
  
  // sort the renamed keys for printing output
  var renamedKeys = Object.keys(renamed)
  renamedKeys.sort()
  var ordered = renamedKeys.reduce((obj, k) => {
    obj[k] = renamed[k]
    obj[k].sort()
    return obj
  }, {})
  console.log('Proposed layout:',
    renamedKeys.map(k => k + ' - ' + renamed[k].length),
    renamedKeys.map(k => k + ' - ' + renamed[k].length).slice(100))
  console.log(`Pak layout written to "${PAK_NAME}"`)
  ufs.writeFileSync(PAK_NAME, JSON.stringify(ordered, null, 2))
  
  game.ordered = ordered
  game.renamed = renamed
  game.condensed = condensed
  return game
}

function getSequenceNumeral(pre, count) {
  var digitCount = Math.ceil(count / 10) + 1
  var result = 0
  for(var i = 1; i < digitCount; i++) {
    result += pre * Math.pow(10, i)
  }
  return result + (count % 10)
}

function getHelp(outputProject) {
  var noCCR = path.basename(outputProject).replace('-ccr', '')
  return `, you should run:
npm run start -- /assets/${noCCR} ${outputProject}
and
open ./build/release-*/ioq3ded +set fs_basepath ${path.dirname(path.dirname(outputProject))
} +set fs_basegame ${noCCR} +set fs_game ${noCCR}
and
npm run start -- /assets/${noCCR} ${outputProject}
`
}

function repackIndexJson(game, outCombined, outConverted, outputProject) {
  // replace game.ordered without extensions because the graph
  //   does not match the converted paths at this point
  var orderedNoExt = Object.keys(game.ordered)
    .reduce((obj, k) => {
      obj[k] = game.ordered[k].map(f => f
        .replace(outConverted, '')
        .replace(outCombined, '')
        .replace(path.extname(f), '')
        .replace(/^\/|\/$/ig, ''))
      return obj
    }, {})
  var indexJson, help2
  if(outputProject) {
    indexJson = path.join(outputProject, './index.json')
  } else {
    indexJson = INDEX_NAME
  }
  if(!game.graph) {
    console.log(`Skipping index.json "${indexJson}"`, getHelp(outputProject))
    return
  }
  // generate a index.json the server can use for pk3 sorting based on map/game type
  var remapped = {}
  var filesOverLimit = getLeaves(game.graph.getVertices()
    .filter(v => v.inEdges.filter((e, i, arr) => arr.indexOf(e) === i).length > edges))
  var entityAssets = getLeaves(Object.keys(game.entities)
    .map(ent => game.graph.getVertex(ent))
    .filter(v => v))
  var externalAndShared = entityAssets
    .concat(game.everything.filter(minimatch.filter('**/+(player|players)/**')))
  var qvms = Object.keys(game.qvms)
    .filter(qvm => qvm.match(/ui.qvm/i))
    .concat(Object.keys(game.qvms)
    .filter(qvm => !qvm.match(/ui.qvm/i)))
  qvms.forEach(qvm => {
    // update shared items so menu is downloaded followed by cgame
    var gameAdditions
    if(qvm.match(/game.qvm/i)) {
      gameAdditions = whitelist[path.basename(outConverted)]
        || Object.keys(whitelist)
        .filter(k => !(k.split('-')[0] + '-cc').localeCompare(path.basename(outConverted, 'en', { sensitivity: 'base' })))
        .map(k => whitelist[k])
        .flat(1)
      if(!Array.isArray(gameAdditions)) gameAdditions = [gameAdditions]
      gameAdditions = gameAdditions.map(m => {
        return externalAndShared.filter(minimatch.filter(m))
      }).flat(1)
    } else {
      gameAdditions = []
    }
    var gameAssets = mapGameAssets(game.graph.getVertex(qvm))
      .filter(f => game.everything.includes(f)
        && !externalAndShared.includes(f))
      // add extra game assets the parser missed like default models/player sounds
      .concat(gameAdditions)
    externalAndShared = externalAndShared.concat(gameAssets)
    gameAssets.forEach(f => {
      var matchPak = Object.keys(orderedNoExt)
        .filter(k => orderedNoExt[k].includes(f
          .replace(outConverted, '')
          .replace(outCombined, '')
          .replace(path.extname(f), '')
          .replace(/^\/|\/$/ig, '')))[0]
      var newName = (qvm.match(/ui.qvm/i) ? 'menu' : 'game') + '/' + matchPak
      if(typeof matchPak === 'undefined') {
        console.log(Object.values(orderedNoExt).flat(1).filter(f2 => f2.includes(path.basename(f))))
        console.error('Couldn\'t find file in packs ' + f)
        return true
      }
      if(typeof remapped[newName] == 'undefined') {
        remapped[newName] = {
          name: matchPak + '.pk3',
          size: outputProject
            ? ufs.statSync(path.join(outputProject, matchPak + '.pk3')).size
            : 0
        }
      }
    })
  })
  Object.keys(game.maps).map(m => game.graph.getVertex(m)).forEach(v => {
    var map = path.basename(v.id).replace(/\.bsp/i, '')
    var mapPak = Object.keys(orderedNoExt).filter(k => k.includes(map))
    var mapAssets = getLeaves(v)
      .filter(f => game.everything.includes(f) 
        && (!externalAndShared.includes(f) || entityAssets.includes(f)))
    mapAssets.forEach(f => {
      var matchPak = mapPak // always check the map pak first
        .concat(Object.keys(orderedNoExt))
        .filter(k => orderedNoExt[k].includes(f
          .replace(outConverted, '')
          .replace(outCombined, '')
          .replace(path.extname(f), '')
          .replace(/^\/|\/$/ig, '')))[0]
      if(typeof matchPak === 'undefined') {
        throw new Error('Couldn\'t find file in packs ' + f)
      }
      var newName = 'maps/' + map + '/' + matchPak
      if(typeof remapped[newName] == 'undefined') {
        remapped[newName] = {
          name: matchPak + '.pk3',
          size: outputProject
            ? ufs.statSync(path.join(outputProject, matchPak + '.pk3')).size
            : 0
        }
      }
    })
  })
  console.log(`Writing index.json "${indexJson}"`, getHelp(outputProject))
  if(outputProject) {
    ufs.writeFileSync(indexJson, JSON.stringify(remapped, null, 2))
  }
  ufs.writeFileSync(INDEX_NAME, JSON.stringify(remapped, null, 2))
}

async function repack(gs, outConverted, outputProject) {
  var game
  if(!gs || !gs.ordered) {
    game = await groupAssets(gs, outConverted)
  } else {
    game = gs
  }
  if(!ufs.existsSync(outputProject)) ufs.mkdirSync(outputProject)
  var orderedKeys = Object.keys(game.ordered)
  for(var i = 0; i < orderedKeys.length; i++) {
    await progress([[1, i, orderedKeys.length, orderedKeys[i]]], true)
    var pak = game.ordered[orderedKeys[i]]
    var real = pak.filter(f => ufs.existsSync(f) && !ufs.statSync(f).isDirectory())
    var outFile = path.join(outputProject, orderedKeys[i] + '.pk3')
    if(noOverwrite && ufs.existsSync(outFile)) continue
    ufs.closeSync(ufs.openSync(outFile, 'w'))
    var output = ufs.createWriteStream(outFile)
    // remove absolute path from zip file, make it relative
    await compressDirectory(real, output, real[0].includes(orderedKeys[i] + '.pk3dir')
      ? path.join(outConverted, orderedKeys[i] + '.pk3dir')
      : outConverted)
  }
}

// do the actual work specified in arguments
async function repackGames() {
  var stepCounter = 0
  var stepTotal = Object.keys(STEPS).length
  for(var i = 0; i < mountPoints.length; i++) {
    try {
      var outCombined = path.join(TEMP_DIR, path.basename(mountPoints[i]) + '-c')
      var outConverted = path.join(TEMP_DIR, path.basename(mountPoints[i]) + '-cc')
      var outRepacked = path.join(TEMP_DIR, path.basename(mountPoints[i]) + '-ccr')
      var gs
      if(typeof STEPS['source'] != 'undefined') {
        await progress([
          [0, stepCounter, stepTotal, STEPS['source']],
          [1, 0, 2, 'Sourcing files']
        ])
        await unpackPk3s(mountPoints[i], outCombined, progress, noOverwrite)
        stepCounter++
      }
      if(!noGraph) {
        if(!usePrevious) {
          await progress([[0, stepCounter, stepTotal, STEPS['graph']]])
          gs = await graphGame(0, outCombined, progress)
        } else {
          await progress([[0, stepCounter, stepTotal, STEPS['graph']]])
          gs = await graphGame(JSON.parse(ufs.readFileSync(TEMP_NAME).toString('utf-8')),
            outCombined, progress)
        }
        stepCounter++
      }
      
      if(typeof STEPS['info'] != 'undefined') {
        await progress([
          [1, false],
          [0, stepCounter, stepTotal, STEPS['info']],
        ])
        await gameInfo(gs, outCombined)
        stepCounter++
      }
      
      if(!noGraph) {
        await groupAssets(gs, outCombined)
      } else {
        var everything = glob.sync('**/*', { cwd: outCombined, nodir: true })
          .map(f => path.join(outCombined, f))
        everything = deDuplicate(everything)
        gs = {
          ordered: everything.reduce((obj, f) => {
            if(f.match(/\.pk3dir/i)) {
              var pk3Path = path.basename(f.substr(0, f.match(/\.pk3dir/i).index))
              if(typeof obj[pk3Path] == 'undefined') {
                obj[pk3Path] = []
              }
              obj[pk3Path].push(f)
            }
            return obj
          }, {}),
          everything: everything
        }
      }
      
      // transcoding and graphics magick
      if(typeof STEPS['convert'] != 'undefined') {
        await progress([
          [1, false],
          [0, stepCounter, stepTotal, STEPS['convert']],
        ])
        await convertGameFiles(gs, outCombined, outConverted, noOverwrite, progress)
        console.log(`Updating Pak layout written to "${PAK_NAME}"`)
        ufs.writeFileSync(PAK_NAME, JSON.stringify(gs.ordered, null, 2))
        stepCounter++
      }
      
      if(typeof STEPS['repack'] != 'undefined') {    
        // repacking
        await progress([
          [1, false],
          [0, stepCounter, stepTotal, STEPS['repack']],
        ])
        await repack(gs, outConverted, outRepacked)
        repackIndexJson(gs, outCombined, outConverted, outRepacked)
      } else {
        await progress([
          [1, false],
          [0, stepCounter, stepTotal, STEPS['repack']],
        ])
        repackIndexJson(gs, outCombined, outConverted)
      }
    } catch (e) {
      console.log(e)
    }
  }
  await progress(false)
}

repackGames().then(() => progress(false))

module.exports = {
  repackGames,
  repack,
  gameInfo,
  PAK_NAME,
  INFO_NAME,
  STEPS,
}
