var path = require('path');
var fs = require('fs');
var glob = require('glob')
var minimatch = require('minimatch')
var {whitelist, findTypes, allTypes} = require('../bin/repack-whitelist.js')
var {execSync} = require('child_process');
var {getLeaves} = require('../lib/asset.graph.js')

var MATCH_JMPLIST = /^((0x[0-9a-f]{8})\s+[0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}\s+(0x[0-9a-f]{1,8}))\s*$/i
var MATCH_STRINGS = /^((0x[0-9a-f]{8})\s+"(.*?)"\s*$)/i
var MATCH_ENTS = /(ammo_|item_|team_|weapon_|holdable_)/i
var SIZEOF_GITEM = 13*4

function disassembleQVM(inputQVM, output) {
  // TODO: try to make sense of the jump list that is defined by bg_misc.c
  //   by tracing the addresses from the strings back to the entity array
  try {
    var disassembler = path.join(__dirname, '../lib/q3vm')
    var type = inputQVM.match(/cgame/) ? 'cgame' : inputQVM.match(/qagame/) ? 'game' : inputQVM.match(/ui/) ? 'ui' : ''
    execSync(`./qvmdis "${inputQVM}" ${type} > "${output}"`, {cwd: disassembler, stdio: 'pipe'})
  } catch (e) {
    console.error(e.message, (e.output || '').toString('utf-8').substr(0, 1000))
  }
}

function loadQVMStrings(buffer, topdirs) {
  var qvmstrings = buffer
    .toString('utf-8')
    .split('\n')
    .map(line => ((/background\s+([^"].*?)(\s|$)/ig).exec(line) || [])[1]
        || ((/['""'](.*?)['""']/ig).exec(line) || [])[1])
    .filter(string => string)
    // assuming a single character is the path seperator,
    //   TODO: could be a number or something, derive from QVM function call with ascii character nearby?
    //   TODO: might need something for %i matching lightmaps names or animation frames
    .map(f => [f.replace(/%[0-9\-\.]*[sdif]/ig, '*')
                .replace(/%[c]/ig, '/'),
               f.replace(/%[0-9\-\.]*[sdicf]/ig, '*')]
              .concat(f.match(new RegExp(allTypes.join('|').replace(/\./g, '\\.')))
                ? ['*' + f] : []))
    .flat(1)
    .map(w => w.includes('*') ? w.replace(/\\/ig, '/') : w)

  // now for some filtering fun
  var filteredstrings = qvmstrings.filter((file, i, arr) =>
      // make sure there is only one occurrence
      arr.indexOf(file) === i
      // the shortest strings match from the file system is probably
      //   greater than 5 characters, because even vm is actually vm%c%s
      && file.length > 5
      // should include a slash or a %c
      //  TODO: doesn't work, not all shaders have slashes
      //&& (file.includes('%') || file.includes('/'))
      // colons aren't allowed in filenames
      && !file.match(/[:]|\\n|\.\.|^[\s\*]*$|EV_/ig)
    // it also has to include one of the top directories
    //   shaders in QUAKED skip the textures folder
    //   this will be faster than minimatch lookups
    //  TODO: doesn't work, not all shaders have slashes
    //  && topdirs.filter(dir => file.includes(dir)).length > 0
    )
  return filteredstrings
}

function graphQVM(project) {
  var result = {}
  var qvms = findTypes(['.qvm'], project)
  var topdirs = glob.sync('**/', {cwd: project})
    .map(dir => path.basename(dir))
  for(var i = 0; i < qvms.length; i++) {
    var disassembly = qvms[i].replace(/\.qvm/i, '.dis')
    var buffer
    if(fs.existsSync(disassembly)) {
      buffer = fs.readFileSync(disassembly)
    } else {
      buffer = fs.readFileSync(qvms[i])
        .toString('utf-8')
        .split('\0')
        .filter(s => s.match(/[a-z0-9]/) && s.length < 4096)
        .map(s => `"${s}"\n`)
        .join('')
    }
    // TODO: add arenas, configs, bot scripts, defi
    var qvmstrings = loadQVMStrings(buffer, topdirs)
      .concat([
        'console', 'white', 'gfx/2d/bigchars',
        'botfiles/**', '*.cfg', '*.shader', '*.menu',
        'ui/*.txt', '*.h', 'ui/assets/**', 'fonts/**', disassembly
      ])
    result[qvms[i]] = qvmstrings
  }
  console.log(`Found ${qvms.length} QVMs and ${Object.values(result).reduce((t, o) => t += o.length, 0)} strings`)
  return result
}

function getGameAssets(disassembly) {
  var lines = fs.readFileSync(disassembly).toString('utf-8').split('\n')
  var entMatches = lines
    .map(l => MATCH_STRINGS.exec(l))
    .filter(l => l)
  // now map the entity strings use to read a .map to the jumplist for bg_misc.c bg_itemlist[]
  var entityJmplist = lines
    .map(l => MATCH_JMPLIST.exec(l))
    .filter(j => j)
  // get the earliest/latest parts of the list matching the entities above
  var bg_itemlist = entMatches
    .filter(l => l[3].match(MATCH_ENTS))
    .reduce((obj, l) => {
      var itemoffset = entityJmplist
        .filter(j => parseInt(j[3], 16) === parseInt(l[2], 16))[0]
      if(!itemoffset) return obj
      var itemstart = parseInt(itemoffset[2], 16)
      // map the jump list on to 13bytes of gitem_s
      var itemstrings = entityJmplist
        .filter(j => parseInt(j[2], 16) >= itemstart && parseInt(j[2], 16) < itemstart + SIZEOF_GITEM)
        .map(j => entMatches.filter(s => parseInt(s[2], 16) === parseInt(j[3], 16))[0])
        .filter(s => s && s[3].length > 0 && !s[3].match(/EV_/))
        .map(s => s[3].replace(/(\.wav)\s+/ig, '$1{SPLIT}').split(/\{SPLIT\}/ig))
        .flat(1)
      if(itemstrings.length > 0) {
        obj[l[3]] = itemstrings
      }
      return obj
    }, {})
  console.log(`Found ${Object.keys(bg_itemlist).length} game entities`)
  return bg_itemlist
}

async function graphMenus(project, progress) {
  var result = {}
  var menus = findTypes(['.menu', '.txt'], project)
  for(var i = 0; i < menus.length; i++) {
    progress([[2, i, menus.length, menus[i].replace(project, '')]])
    var buffer = fs.readFileSync(menus[i])
    try {
      var menu = loadQVMStrings(buffer)
      result[menus[i]] = menu
    } catch (e) {
      console.error(`Error loading menu ${menus[i]}: ${e.message}`, e)
    }
  }
  console.log(`Found ${Object.keys(result).length} menus with ${Object.values(result).flat(1).length} strings`)
  return result
}

function mapGameAssets(qvmVertex) {
  // don't include disassembly in new pak
  // don't include maps obviously because they are listed below
  var gameVertices = qvmVertex
    .outEdges
    .map(e => e.inVertex)
    .filter(v => !v.id.match(/\.dis|\.bsp|\.aas|maps\/|\.qvm/i))
  var gameAssets = [qvmVertex.id]
    .concat(gameVertices.map(v => v.id))
    // include shader scripts, but do not include the assets they point to
    .concat(gameVertices.filter(v => !v.id.match(/\.shader/i)).map(v => getLeaves(v)).flat(2))
    .filter((f, i, arr) => arr.indexOf(f) === i && !f.match(/\.bsp|\.aas|maps\//i))
  return gameAssets
}

module.exports = {
  getGameAssets: getGameAssets,
  loadQVMStrings: loadQVMStrings,
  graphQVM: graphQVM,
  graphMenus: graphMenus,
  disassembleQVM: disassembleQVM,
  mapGameAssets: mapGameAssets,
  MATCH_ENTS: MATCH_ENTS,
}
