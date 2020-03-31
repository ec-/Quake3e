var fs = require('fs')
var path = require('path')
var glob = require('glob')
var minimatch = require("minimatch")

var {disassembleQVM, graphQVM, getGameAssets, graphMenus} = require('../lib/asset.qvm.js')
var md3 = require('../lib/asset.md3.js')
var bsp = require('../lib/asset.bsp.js')
var shaderLoader = require('../lib/asset.shader.js')
var skinLoader = require('../lib/asset.skin.js')
var {
  findTypes, fileTypes, sourceTypes,
  audioTypes, imageTypes, findTypes,
  allTypes
} = require('../bin/repack-whitelist.js')
var {DirectedGraph} = require('../lib/asset.graph.js')

var STEPS = {
  'files': 'Scanning all files',
  'maps': 'Looking for maps',
  'models': 'Looking for models',
  'shaders': 'Looking for shaders',
  'skins': 'Looking for skins',
  'disassemble': 'Disassembling QVMs',
  'qvms': 'Looking for QVMs',
  'menus': 'Looking for menus',
  'entities': 'Looking for game entities',
  'vertices': 'Graphing vertices',
  'shaders': 'Graphing shaders',
}

var BASEQ3 = '/Users/briancullinan/planet_quake_data/quake3-baseq3'
var TEMP_NAME = path.join(__dirname, '../bin/previous-graph.json')

if(fs.existsSync(BASEQ3)) {
  var baseq3 = glob.sync('**/*', {cwd: BASEQ3})
    .map(f => path.join(BASEQ3, f).toLowerCase())
  fs.writeFileSync(path.join(__dirname, './baseq3-filelist.json'), JSON.stringify(baseq3, null, 2))
}

function graphMaps(project) {
  var result = {}
  var maps = findTypes(['.bsp'], project)
  for(var i = 0; i < maps.length; i++) {
    var buffer = fs.readFileSync(maps[i])
    try {
      var map = bsp.load(buffer, { lumps: [bsp.LUMP.ENTITIES, bsp.LUMP.SHADERS] })
      result[maps[i]] = map
    } catch (e) {
      console.error(`Error loading map ${maps[i]}: ${e.message}`, e)
    }
  }
  console.log(`Found ${Object.keys(result).length} maps`)
  return result
}

async function graphModels(project, progress) {
  var result = {}
  var models = findTypes(['.md5', '.md3'], project)
  console.log(`Found ${models.length} models`)
  for(var i = 0; i < models.length; i++) {
    await progress([[2, i, models.length, models[i].replace(project, '')]])
    var buffer = fs.readFileSync(models[i])
    try {
      var model = md3.load(buffer)
      result[models[i]] = model
    } catch (e) {
      console.error(`Error loading model ${models[i]}: ${e.message}`, e)
    }
  }
  var withSkins = Object.keys(result).filter(m => result[m].skins.length > 0)
  console.log(`Loaded ${Object.keys(result).length} models, ${withSkins.length} with skins`)
  return result
}

function graphShaders(project) {
  var result = {}
  var shaders = findTypes(['.shader'], project)
  for(var i = 0; i < shaders.length; i++) {
    var buffer = fs.readFileSync(shaders[i])
    var script = shaderLoader.load(buffer)
    result[shaders[i]] = script
  }
  console.log(`Found ${Object.keys(result).length} shader scripts, with ${Object.values(result).flat(1).length} shaders`)
  return result
}

function graphSkins(project) {
  var result = {}
  var skins = findTypes(['.skin'], project)
  for(var i = 0; i < skins.length; i++) {
    var buffer = fs.readFileSync(skins[i]).toString('utf-8')
    var skin = skinLoader.load(buffer)
    result[skins[i]] = skin
  }
  console.log(`Found ${Object.keys(result).length} skins`)
  return result
}

function deDuplicate(everything) {
  // sort in reverse order because zz packs override contents of pak0-8
  everything.sort((a, b) => b.localeCompare(a, 'en', { sensitivity: 'base' }))
  var unique = []
  return everything.reduce((arr, f) => {
    if(f.match(/\.pk3dir/i)) {
      var noPak = f.replace(/.*?\.pk3dir/i, '')
      if(!unique.includes(noPak)) {
        unique.push(noPak)
        arr.push(f)
      }
    } else if (!unique.includes(f)) {
      unique.push(f)
      arr.push(f)
    }
    return arr
  }, [])
}

async function loadGame(project, progress) {
  var stepTotal = Object.keys(STEPS).length
  await progress([[1, 0, stepTotal, STEPS['files']]])
  var everything = glob.sync('**/*', { cwd: project, nodir: true })
    .map(f => path.join(project, f))
  everything = deDuplicate(everything)
  
  var game = {}
  await progress([[1, 1, stepTotal, STEPS['maps']]])
  game.maps = graphMaps(project)
  await progress([
    [2, false],
    [1, 2, stepTotal, STEPS['models']]
  ])
  game.models = await graphModels(project, progress)
  await progress([
    [2, false],
    [1, 3, stepTotal, STEPS['shaders']]
  ])
  game.shaders = graphShaders(project)
  await progress([[1, 4, stepTotal, STEPS['skins']]])
  game.skins = graphSkins(project)
  var qvms = findTypes(['.qvm'], project)
  await progress([[1, 5, stepTotal, STEPS['qvms']]])
  for(var i = 0; i < qvms.length; i++) {
    var disassembly = qvms[i].replace(/\.qvm/i, '.dis')
    if(!fs.existsSync(disassembly)) {
      await progress([[1, 5, stepTotal, STEPS['disassemble']]])
      disassembleQVM(qvms[i], disassembly)
    }
  }
  await progress([[1, 6, stepTotal, STEPS['qvms']]])
  game.qvms = graphQVM(project)
  await progress([[1, 7, stepTotal, STEPS['menus']]])
  game.menus = await graphMenus(project, progress)
  // TODO: accept an entities definition to match with QVM
  // use some known things about QVMs to group files together first
  await progress([
    [2, false],
    [1, 8, stepTotal, STEPS['entities']]
  ])
  var entities = Object.values(game.qvms)
    .flat(1)
    .filter(k => k.match(/\.dis/i))
    .map(k => getGameAssets(k))
    .reduce((obj, o) => Object.assign(obj, o), {})
  
  // add all vertices
  var entityRefs = Object.keys(game.maps)
    .reduce((obj, k) => {
      obj[k] = game.maps[k].entities
        .reduce((arr, e) => {
          arr.push.apply(arr, [
            e.noise,
            (e.music || '').replace(/(\.wav)\s+/ig, '$1{SPLIT}').split(/\{SPLIT\}/ig),
            e.model,
            e.model2
          ].flat(1))
          return arr
        }, [])
        .filter(e => e && e.charAt(0) != '*')
        .concat([k.replace('.bsp', '.aas')])
        .concat(everything.filter(minimatch.filter('**/maps/' + path.basename(k.replace('.bsp', '')) + '/**')))
        .concat(game.maps[k].entities.map(e => e.classname))
        .filter((e, i, arr) => arr.indexOf(e) === i)
      obj[k].sort()
      return obj
    }, {})
  var mapShaders = Object.keys(game.maps)
    .reduce((obj, k) => {
      obj[k] = game.maps[k].shaders
        .map(s => s.shaderName)
      obj[k].sort()
      return obj
    }, {})
  var modelShaders = Object.keys(game.models)
    .reduce((obj, k) => {
      obj[k] = game.models[k].surfaces
        .map(s => s.shaders).flat(1)
        .filter(s => s)
      obj[k].sort()
      return obj
    }, {})
  var scriptShaders = Object.keys(game.shaders)
    .reduce((obj, k) => {
      obj[k] = Object.keys(game.shaders[k])
      obj[k].sort()
      return obj
    }, {})
  var scriptTextures = Object.keys(game.shaders)
    .reduce((obj, s) => {
      var keys = Object.keys(game.shaders[s])
        .forEach(k => {
          if(typeof obj[k] === 'undefined') {
            obj[k] = []
          }
          if(game.shaders[s][k].stages) {
            obj[k].push.apply(obj[k], game.shaders[s][k].stages.map(stage => stage.maps).flat(1))
          }
          if(game.shaders[s][k].outerBox) {
            obj[k].push.apply(obj[k], game.shaders[s][k].outerBox)
          }
          if(game.shaders[s][k].innerBox) {
            obj[k].push.apply(obj[k], game.shaders[s][k].innerBox)
          }
          obj[k].sort()
        })
      return obj
    }, {})
  var skinShaders = Object.keys(game.skins)
    .reduce((obj, k) => {
      obj[k] = game.skins[k].surfaces
        .map(s => s.shaderName)
      obj[k].sort()
      return obj
    }, {})
  var qvmFiles = await Object.keys(game.qvms)
    .reduce(async (objPromise, k, i) => {
      var obj = await objPromise
      await progress([[1, 9 + i, stepTotal + Object.keys(game.qvms).length,
        `Searching for QVM related files ${path.basename(k)} from ${game.qvms[k].length} strings`]], true)
      var wildcards = game.qvms[k].filter(s => s.includes('*'))
      obj[k] = wildcards
        .map(w => everything.filter(minimatch.filter('**/' + w)))
        .flat(1)
        .concat(game.qvms[k])
        .filter((e, i, arr) => arr.indexOf(e) === i)
      obj[k].sort()
      return obj
    }, Promise.resolve({}))
  var menuFiles = await Object.keys(game.menus)
    .reduce(async (objPromise, k, i) => {
      var obj = await objPromise
      obj[k] = game.menus[k]
        .filter((e, i, arr) => arr.indexOf(e) === i)
      obj[k].sort()
      return obj
    }, Promise.resolve({}))
  var gameState = {
    entities: entities,
    mapEntities: entityRefs,
    maps: mapShaders,
    models: modelShaders,
    scripts: scriptShaders,
    shaders: scriptTextures,
    skins: skinShaders,
    qvms: qvmFiles,
    menus: menuFiles,
    everything: everything,
  }
  console.log(`Game graph written to "${TEMP_NAME}"`)
  fs.writeFileSync(TEMP_NAME, JSON.stringify(gameState, null, 2))
  
  return Object.assign(game, gameState)
}

async function graphGame(gs, project, progress) {
  if(!gs) {
    gs = await loadGame(project, progress)
  }
  var graph = new DirectedGraph()
  
  // add all edges to the graph
  var notfound = []
  var inbaseq3 = []
  var everything = gs.everything.map(f => f.toLowerCase())
  var unknownTypes = gs.everything.map(f => path.extname(f).toLowerCase())
    .filter((t, i, arr) => arr.indexOf(t) === i)
    .filter(t => !allTypes.includes(t))

  // add all the vertices which are the keys of the variables above
  var vertices = []
    .concat(Object.values(gs.mapEntities).flat(1))
    .concat(Object.keys(gs.qvms))
    .concat(Object.keys(gs.maps))
    .concat(Object.keys(gs.scripts))
    .concat(Object.keys(gs.models))
    .concat(Object.keys(gs.skins))
    .concat(Object.values(gs.shaders).flat(1))
    .concat(Object.keys(gs.qvms))
    .concat(Object.values(gs.qvms).flat(1)) // can be filename or shaders
    .concat(Object.keys(gs.menus))
    .concat(Object.values(gs.menus).flat(1)) // can be filename or shaders
    .filter((v, i, arr) => v && arr.indexOf(v) == i)
    
  await progress([[1, 10 + Object.keys(gs.qvms).length,
    Object.keys(STEPS).length + Object.keys(gs.qvms).length,
    `Graphing ${vertices.length} vertices`]])
  
  var fileLookups = {}
  for(var i = 0; i < vertices.length; i++) {
    // everything in vertices should match a file
    if(!fs.existsSync(vertices[i])) {
      var index = searchMinimatch(vertices[i], everything)
      if(index == -1) inbaseq3.push(vertices[i])
      else if (index !== null) {
        fileLookups[vertices[i]] = graph.getVertex(gs.everything[index])
          || graph.addVertex(gs.everything[index], {
          name: gs.everything[index]
        })
      }
      else notfound.push(vertices[i])
    } else {
      fileLookups[vertices[i]] = graph.getVertex(vertices[i])
        || graph.addVertex(vertices[i], {
        name: vertices[i]
      })
    }
  }

  // lookup all shaders
  var everyShaderName = Object.values(gs.scripts)
    .flat(1)
    .map(s => s.replace(new RegExp(imageTypes.join('|'), 'ig'), ''))
    .filter((s, i, arr) => arr.indexOf(s) === i)
  var allShaders = []
    .concat(Object.values(gs.entities).flat(1)) // match with shaders or files so icons match up
    .concat(Object.values(gs.maps).flat(1))
    .concat(Object.values(gs.models).flat(1))
    .concat(Object.values(gs.scripts).flat(1)) // obviously all these should match the list above
    .concat(Object.values(gs.skins).flat(1))
    .concat(Object.values(gs.qvms).flat(1)) // can be filename or shaders
    .concat(Object.values(gs.menus).flat(1)) // can be filename or shaders
    .filter((v, i, arr) => v && arr.indexOf(v) == i)
    
  await progress([[1, 10 + Object.keys(gs.qvms).length + 1,
    Object.keys(STEPS).length + Object.keys(gs.qvms).length,
    `Graphing ${allShaders.length} shaders`]])
    
  var shaderLookups = {}
  for(var i = 0; i < allShaders.length; i++) {
    // matches without extension
    //   which is what we want because mods override shaders
    var nameNoExt = allShaders[i].replace(new RegExp(imageTypes.join('|'), 'ig'), '')
    var index = everyShaderName.indexOf(nameNoExt)
    if(index > -1) {
      shaderLookups[allShaders[i]] = graph.getVertex(everyShaderName[index])
        || graph.addVertex(everyShaderName[index], {
          name: everyShaderName[index]
        })
    } else {
      // try to match a filename directly
      index = searchMinimatch(allShaders[i], everything)
      if(index == -1) inbaseq3.push(allShaders[i])
      else if(index !== null) {
        shaderLookups[allShaders[i]] = graph.getVertex(gs.everything[index])
          || graph.addVertex(gs.everything[index], {
            name: gs.everything[index]
          })
      }
      else notfound.push(allShaders[i])
    }
  }
  
  // link all the vertices and follow all shaders through to their files
  Object.keys(gs.shaders).forEach(k => {
    gs.shaders[k].forEach(e => {
      if(typeof fileLookups[e] == 'undefined') return
      graph.addEdge(graph.getVertex(k.replace(new RegExp(imageTypes.join('|'), 'ig'), '')), fileLookups[e])
    })
  })
  Object.keys(gs.entities).forEach(k => {
    var entityEdges = gs.entities[k]
      .filter(e => typeof fileLookups[e] != 'undefined'
        || typeof shaderLookups[e] != 'undefined')
    if(entityEdges.length > 0) {
      fileLookups[k] = graph.addVertex(k, {name: k})
      entityEdges.forEach(e => {
        if(typeof fileLookups[e] != 'undefined') {
          graph.addEdge(fileLookups[k], fileLookups[e])
        }
        if(typeof shaderLookups[e] != 'undefined') {
          graph.addEdge(fileLookups[k], shaderLookups[e])
        }
      })
    }
  })
  Object.keys(gs.mapEntities).forEach(k => {
    gs.mapEntities[k].forEach(e => {
      if(typeof fileLookups[e] == 'undefined') return
      graph.addEdge(graph.getVertex(k), fileLookups[e])
    })
  })
  Object.keys(gs.maps).forEach(k => {
    gs.maps[k].forEach(e => {
      if(typeof shaderLookups[e] == 'undefined') return
      graph.addEdge(graph.getVertex(k), shaderLookups[e])
    })
  })
  Object.keys(gs.models).forEach(k => {
    gs.models[k].forEach(e => {
      if(typeof shaderLookups[e] == 'undefined') return
      graph.addEdge(graph.getVertex(k), shaderLookups[e])
    })
  })
  Object.keys(gs.skins).forEach(k => {
    gs.skins[k].forEach(e => {
      if(typeof shaderLookups[e] == 'undefined') return
      graph.addEdge(graph.getVertex(k), shaderLookups[e])
    })
  })
  Object.keys(gs.qvms).forEach(k => {
    gs.qvms[k].forEach(e => {
      if(typeof fileLookups[e] != 'undefined') {
        graph.addEdge(graph.getVertex(k), fileLookups[e])
      }
      if(typeof shaderLookups[e] != 'undefined') {
        graph.addEdge(graph.getVertex(k), shaderLookups[e])
      }
    })
  })
  Object.keys(gs.menus).forEach(k => {
    gs.menus[k].forEach(e => {
      if(typeof fileLookups[e] != 'undefined') {
        graph.addEdge(graph.getVertex(k), fileLookups[e])
      }
      if(typeof shaderLookups[e] != 'undefined') {
        graph.addEdge(graph.getVertex(k), shaderLookups[e])
      }
    })
  })
  // TODO: add arenas, configs, bot scripts, defi
  
  gs.graph = graph
  gs.notfound = notfound
  gs.baseq3 = inbaseq3
  
  return gs
}

function searchMinimatch(search, everything) {
  var lookup = search
    .replace(/\/\//ig, '/')
    .replace(/\\/g, '/')
    .replace(/\.[^\.]*$/, '') // remove extension
    .toLowerCase()
  if(lookup.length === 0) return null
  var name = everything.filter(f => f.includes(lookup)) //minimatch.filter('**/' + search + '*'))[0]
  if(!name[0]) {
    if(baseq3.filter(f => f.includes(lookup))[0]) { //minimatch.filter('**/' + search + '*'))[0]) {
      return -1
    }
    return null
  } else if (name.length > 1) {
    var type = [imageTypes, audioTypes, sourceTypes, fileTypes]
      .filter(type => type.includes(path.extname(search).toLowerCase()))[0]
    if(path.extname(search) && !type) {
      console.error('File type not found ' + search)
      return null
    }
    else if (!type) type = imageTypes // assuming its a shading looking for an image
    name = everything.filter(f => type.filter(t => f.includes(lookup + t)).length > 0)
    if(name.length == 0) {
      return null
    } else if(name.length > 1) {
      // TODO: error or something here? Duplicate files like where jpg is already included with the same name
    //  console.error('Duplicates found ' + search)
    }
  }
  return everything.indexOf(name[0])
}

module.exports = {
  deDuplicate,
  graphMaps,
  graphModels,
  graphShaders,
  graphSkins,
  graphGame,
  load: graphGame,
  TEMP_NAME: TEMP_NAME
}
