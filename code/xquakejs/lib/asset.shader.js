var balanced = require('balanced-match')
var SURF = {
	FLAGS: {
		NODAMAGE:    0x1,                                      // never give falling damage
		SLICK:       0x2,                                      // effects game physics
		SKY:         0x4,                                      // lighting from environment map
		LADDER:      0x8,
		NOIMPACT:    0x10,                                     // don't make missile explosions
		NOMARKS:     0x20,                                     // don't leave missile marks
		FLESH:       0x40,                                     // make flesh sounds and effects
		NODRAW:      0x80,                                     // don't generate a drawsurface at all
		HINT:        0x100,                                    // make a primary bsp splitter
		SKIP:        0x200,                                    // completely ignore, allowing non-closed brushes
		NOLIGHTMAP:  0x400,                                    // surface doesn't need a lightmap
		POINTLIGHT:  0x800,                                    // generate lighting info at vertexes
		METALSTEPS:  0x1000,                                   // clanking footsteps
		NOSTEPS:     0x2000,                                   // no footstep sounds
		NONSOLID:    0x4000,                                   // don't collide against curves with this set
		LIGHTFILTER: 0x8000,                                   // act as a light filter during q3map -light
		ALPHASHADOW: 0x10000,                                  // do per-pixel light shadow casting in q3map
		NODLIGHT:    0x20000,                                  // don't dlight even if solid (solid lava, skies)
		DUST:        0x40000                                   // leave a dust trail when walking on this surface
	},
	CONTENTS: {
		SOLID:         1,                                      // an eye is never valid in a solid
		LAVA:          8,
		SLIME:         16,
		WATER:         32,
		FOG:           64,

		NOTTEAM1:      0x0080,
		NOTTEAM2:      0x0100,
		NOBOTCLIP:     0x0200,

		AREAPORTAL:    0x8000,

		PLAYERCLIP:    0x10000,
		MONSTERCLIP:   0x20000,
		TELEPORTER:    0x40000,
		JUMPPAD:       0x80000,
		CLUSTERPORTAL: 0x100000,
		DONOTENTER:    0x200000,
		BOTCLIP:       0x400000,
		MOVER:         0x800000,

		ORIGIN:        0x1000000,                              // removed before bsping an entity

		BODY:          0x2000000,                              // should never be on a brush, only in game
		CORPSE:        0x4000000,
		DETAIL:        0x8000000,                              // brushes not used for the bsp
		STRUCTURAL:    0x10000000,                             // brushes used for the bsp
		TRANSLUCENT:   0x20000000,                             // don't consume surface fragments inside
		TRIGGER:       0x40000000,
		NODROP:        0x80000000                              // don't leave bodies or items (death fog, lava)
	}
}

var SORT = {
	BAD:            0,
	PORTAL:         1,                                     // mirrors, portals, viewscreens
	ENVIRONMENT:    2,                                     // sky box
	OPAQUE:         3,                                     // opaque
	DECAL:          4,                                     // scorch marks, etc.
	SEE_THROUGH:    5,                                     // ladders, grates, grills that may have small blended
	                                                       // edges in addition to alpha test
	BANNER:         6,
	FOG:            7,
	UNDERWATER:     8,                                     // for items that should be drawn in front of the water plane
	BLEND0:         9,                                     // regular transparency and filters
	BLEND1:         10,                                    // generally only used for additive type effects
	BLEND2:         11,
	BLEND3:         12,
	BLEND6:         13,
	STENCIL_SHADOW: 14,
	ALMOST_NEAREST: 15,                                    // gun smoke puffs
	NEAREST:        16                                     // blood blobs
};

var surfaceParams = {
	'nomipmaps':     { surface: 0, 											contents: SURF.CONTENTS.NOMIPMAPS },
	// server relevant contents
	'water':         { surface: 0,                      contents: SURF.CONTENTS.WATER },
	'slime':         { surface: 0,                      contents: SURF.CONTENTS.SLIME },         // mildly damaging
	'lava':          { surface: 0,                      contents: SURF.CONTENTS.LAVA },          // very damaging
	'playerclip':    { surface: 0,                      contents: SURF.CONTENTS.PLAYERCLIP },
	'monsterclip':   { surface: 0,                      contents: SURF.CONTENTS.MONSTERCLIP },
	'nodrop':        { surface: 0,                      contents: SURF.CONTENTS.NODROP },        // don't drop items or leave bodies (death fog, lava, etc)
	'nonsolid':      { surface: SURF.FLAGS.NONSOLID,    contents: 0 },                      // clears the solid flag

	// utility relevant attributes
	'origin':        { surface: 0,                      contents: SURF.CONTENTS.ORIGIN },        // center of rotating brushes
	'trans':         { surface: 0,                      contents: SURF.CONTENTS.TRANSLUCENT },   // don't eat contained surfaces
	'detail':        { surface: 0,                      contents: SURF.CONTENTS.DETAIL },        // don't include in structural bsp
	'structural':    { surface: 0,                      contents: SURF.CONTENTS.STRUCTURAL },    // force into structural bsp even if trnas
	'areaportal':    { surface: 0,                      contents: SURF.CONTENTS.AREAPORTAL },    // divides areas
	'clusterportal': { surface: 0,                      contents: SURF.CONTENTS.CLUSTERPORTAL }, // for bots
	'donotenter':    { surface: 0,                      contents: SURF.CONTENTS.DONOTENTER },    // for bots

	'fog':           { surface: 0,                      contents: SURF.CONTENTS.FOG},            // carves surfaces entering
	'sky':           { surface: SURF.FLAGS.SKY,         contents: 0 },                      // emit light from an environment map
	'lightfilter':   { surface: SURF.FLAGS.LIGHTFILTER, contents: 0 },                      // filter light going through it
	'alphashadow':   { surface: SURF.FLAGS.ALPHASHADOW, contents: 0 },                      // test light on a per-pixel basis
	'hint':          { surface: SURF.FLAGS.HINT,        contents: 0 },                      // use as a primary splitter

	// server attributes
	'slick':         { surface: SURF.FLAGS.SLICK,       contents: 0 },
	'noimpact':      { surface: SURF.FLAGS.NOIMPACT,    contents: 0 },                      // don't make impact explosions or marks
	'nomarks':       { surface: SURF.FLAGS.NOMARKS,     contents: 0 },                      // don't make impact marks, but still explode
	'ladder':        { surface: SURF.FLAGS.LADDER,      contents: 0 },
	'nodamage':      { surface: SURF.FLAGS.NODAMAGE,    contents: 0 },
	'metalsteps':    { surface: SURF.FLAGS.METALSTEPS,  contents: 0 },
	'flesh':         { surface: SURF.FLAGS.FLESH,       contents: 0 },
	'nosteps':       { surface: SURF.FLAGS.NOSTEPS,     contents: 0 },

	// drawsurf attributes
	'nodraw':        { surface: SURF.FLAGS.NODRAW,      contents: 0 },                      // don't generate a drawsurface (or a lightmap)
	'pointlight':    { surface: SURF.FLAGS.POINTLIGHT,  contents: 0 },                      // sample lighting at vertexes
	'nolightmap':    { surface: SURF.FLAGS.NOLIGHTMAP,  contents: 0 },                      // don't generate a lightmap
	'nodlight':      { surface: SURF.FLAGS.NODLIGHT,    contents: 0 },                      // don't ever add dynamic lights
	'dust':          { surface: SURF.FLAGS.DUST,        contents: 0 }                       // leave a dust trail when walking on this surface
};

var Shader = function () {
	this.name           = null;
	this.sort           = 0;
	this.surfaceFlags   = 0;
	this.contentFlags   = 0;
	this.cull           = 'front';
	this.sky            = false;
	this.cloudSize      = 0;
	this.innerBox       = [];
	this.outerBox       = [];
	this.fog            = false;
	this.polygonOffset  = false;
	this.entityMergable = false;
	this.positionLerp   = false;
	this.portalRange    = 0;
	this.vertexDeforms  = [];
	this.stages         = [];
	this.notStage 			= '';
};

var ShaderStage = function () {
	this.hasBlendFunc = false;
	this.blendSrc     = 'GL_ONE';
	this.blendDest    = 'GL_ZERO';
	this.depthWrite   = true;
	this.depthFunc    = 'lequal';

	this.maps         = [];
	this.animFreq     = 0;
	this.clamp        = false;
	this.tcGen        = 'base';
	this.rgbGen       = 'identity';
	this.rgbWave      = null;
	this.alphaGen     = '1.0';
	this.alphaFunc    = null;
	this.alphaWave    = null;
	this.isLightmap   = false;
	this.tcMods       = [];
};

var Deform = function () {
	this.type   = null;
	this.spread = 0.0;
	this.wave   = null;
};

var TexMod = function () {
	this.type       = null;
	this.scaleX     = 0.0;
	this.scaleY     = 0.0;
	this.sSpeed     = 0.0;
	this.tSpeed     = 0.0;
	this.wave       = null;
	this.turbulance = null;
};

var Waveform = function () {
	this.funcName = null;
	this.base     = 0.0;
	this.amp      = 0.0;
	this.phase    = 0.0;
	this.freq     = 0.0;
};

function load(buffer) {
	var file = buffer.toString('utf-8')
		// replace all comments in the script file
		.replace(/(^|\r\n|\t\t|\s+)\s*\/\/.*/ig, '')
  var match
  var current = file
  var result = {}
  while((match = balanced('{', '}', current))) {
		var name = match.pre.trim()
		result[name] = new Shader();
		result[name].name = name
    loadShader(match.body, result[name])
    current = match.post
  }
  return result
}

function loadShader(text, script) {
	var match
  var current = text
  while((match = balanced('{', '}', current))) {
		script.notStage += match.pre
    script.stages.push(match.body)
    current = match.post
  }
	script.notStage += current
	var words = script.notStage.split(/[\s]+/ig).map(w => w.toLowerCase())

	for(var i = 0; i < script.stages.length; i++) {
		script.stages[i] = parseStage(script.stages[i], script)
	}
	
	for(var w = 0; w < words.length; w++) {
		switch(words[w]) {
			case 'cull': script.cull = words[w+1]
			break
			case 'polygonoffset': script.polygonOffset = true
			break
			case 'entitymergable': script.entityMergable = true
			break
			case 'portal': script.sort = SORT.PORTAL
			break
			case 'fogparms':
				script.fog = true
				script.sort = SORT.FOG
			break
			case 'sort': script.sort = parseSort(words[w+1])
			break
			case 'deformvertexes': 
				script.vertexDeforms.push(parseDeform(words.slice(w+1)))
			break
			case 'surfaceparm':
				if(surfaceParams[words[w+1]]) {
					script.surfaceFlags |= surfaceParams[words[w+1]].surface
					script.contentFlags |= surfaceParams[words[w+1]].contents
				} else {
					console.log(`Unknown surface parm ${words[w+1]}`)
				}
			break
			case 'skyparms': 
				parseSkyparms(words.slice(w+1), script)
			break
		}
	}

	//
	// If the shader is using polygon offset,
	// it's a decal shader.
	//
	if (script.polygonOffset && !script.sort) {
		script.sort = SORT.DECAL;
	}

	for (var i = 0; i < script.stages.length; i++) {
		var stage = script.stages[i];

		//
		// Determine sort order and fog color adjustment
		//
		if (script.stages[0].hasBlendFunc && stage.hasBlendFunc) {
			// Don't screw with sort order if this is a portal or environment.
			if (!script.sort) {
				// See through item, like a grill or grate.
				if (stage.depthWrite) {
					script.sort = SORT.SEE_THROUGH;
				} else {
					script.sort = SORT.BLEND0;
				}
			}
		}
	}

	// There are times when you will need to manually apply a sort to
	// opaque alpha tested shaders that have later blend passes.
	if (!script.sort) {
		script.sort = SORT.OPAQUE;
	}

	return script;
}

function parseDeform(words) {
	var deform = new Deform();

	deform.type = words[0].toLowerCase();

	switch (deform.type) {
		case 'wave':
			deform.spread = 1.0 / parseFloat(words[1]);
			deform.wave = parseWaveForm(words.slice(2));
			break;
	}
	return deform
}

function parseSort(val) {
	switch (val) {
		case 'portal':     return SORT.PORTAL;         break;
		case 'sky':        return SORT.ENVIRONMENT;    break;
		case 'opaque':     return SORT.OPAQUE;         break;
		case 'decal':      return SORT.DECAL;          break;
		case 'seeThrough': return SORT.SEE_THROUGH;    break;
		case 'banner':     return SORT.BANNER;         break;
		case 'additive':   return SORT.BLEND1;         break;
		case 'nearest':    return SORT.NEAREST;        break;
		case 'underwater': return SORT.UNDERWATER;     break;
		default:           return parseInt(val, 10); 	 break;
	}
}

function parseSkyparms(words, script) {
	var suffixes = ['rt', 'bk', 'lf', 'ft', 'up', 'dn'];

	var innerBox = words[0].toLowerCase();
	var cloudSize = parseInt(words[1], 10);
	var outerBox = words[2].toLowerCase();

	script.sky = true;
	script.innerBox = innerBox === '-' ? [] : suffixes.map(function (suf) {
		return innerBox + '_' + suf + '.tga';
	});
	script.cloudSize = cloudSize;
	script.outerBox = outerBox === '-' ? [] : suffixes.map(function (suf) {
		return outerBox + '_' + suf + '.tga';
	});
	script.sort = SORT.ENVIRONMENT;
}

function parseStage(text, script) {
	var stage = new ShaderStage();
	var words = text.split(/[\s]+/ig).map(w => w.toLowerCase())
	for(var w = 0; w < words.length; w++) {
		switch (words[w]) {
			case 'clampmap':
				stage.clamp = true;
			case 'map':
				var map = words[w+1]
				if (!map) {
					throw new Exception('WARNING: missing parameter for \'map\'');
				}
				if (map === '$whiteimage') {
					map = '*white';
				} else if (map == '$lightmap') {
					stage.isLightmap = true;
					//if (lightmapIndex < 0) {
					//	map = '*white';
					//} else {
						map = '*lightmap';
					//}
				}
				stage.maps.push(map);
				break;

			case 'animmap':
				stage.animFreq = parseFloat(words[++w]);
				var nextMap = words[++w];
				while (nextMap.match(/\.[^\/.]+$/)) {
					stage.maps.push(nextMap);
					nextMap = words[++w];
				}
				--w
				break;

			case 'rgbgen':
				stage.rgbGen = words[w+1].toLowerCase();
				switch (stage.rgbGen) {
					case 'wave':
						stage.rgbWave = parseWaveForm(words.slice(w+2));
						if (!stage.rgbWave) { stage.rgbGen = 'identity'; }
						break;
				}
				break;

			case 'alphagen':
				stage.alphaGen = words[w+1].toLowerCase();
				switch (stage.alphaGen) {
					case 'wave':
						stage.alphaWave = parseWaveForm(words.slice(w+2));
						if (!stage.alphaWave) { stage.alphaGen = '1.0'; }
						break;
					case 'portal':
						script.portalRange = parseFloat(words[w+1].toLowerCase());
						break;
					default: break;
				}
				break;

			case 'alphafunc':
				stage.alphaFunc = words[w+1].toUpperCase();
				break;

			case 'blendfunc':
				stage.blendSrc = words[w+1].toUpperCase();
				stage.hasBlendFunc = true;
				if (!stage.depthWriteOverride) {
					stage.depthWrite = false;
				}
				switch (stage.blendSrc) {
					case 'ADD':
						stage.blendSrc = 'GL_ONE';
						stage.blendDest = 'GL_ONE';
						break;

					case 'BLEND':
						stage.blendSrc = 'GL_SRC_ALPHA';
						stage.blendDest = 'GL_ONE_MINUS_SRC_ALPHA';
						break;

					case 'FILTER':
						stage.blendSrc = 'GL_DST_COLOR';
						stage.blendDest = 'GL_ZERO';
						break;

					default:
						stage.blendDest = words[w+2].toUpperCase();
						break;
				}
				break;

			case 'depthfunc':
				stage.depthFunc = words[w+1].toLowerCase();
				break;

			case 'depthwrite':
				stage.depthWrite = true;
				stage.depthWriteOverride = true;
				break;

			case 'tcmod':
				stage.tcMods.push(parseTexMod(words.slice(w+1)))
				break;

			case 'tcgen':
				stage.tcGen = words[w+1];
				break;

			default: break;
		}
	}
	
	if (stage.blendSrc == 'GL_ONE' && stage.blendDest == 'GL_ZERO') {
		stage.hasBlendFunc = false;
		stage.depthWrite = true;
	}

	// I really really really don't like doing this, which basically just forces lightmaps to use the 'filter' blendmode
	// but if I don't a lot of textures end up looking too bright. I'm sure I'm just missing something, and this shouldn't
	// be needed.
	if (stage.isLightmap && stage.hasBlendFunc) {
		stage.blendSrc = 'GL_DST_COLOR';
		stage.blendDest = 'GL_ZERO';
	}
	return stage
}

function parseTexMod(words) {
	var tcMod = {
		type: words[0].toLowerCase()
	};

	switch (tcMod.type) {
		case 'rotate':
			tcMod.angle = parseFloat(words[1]) * (3.1415/180);
			break;

		case 'scale':
			tcMod.scaleX = parseFloat(words[1]);
			tcMod.scaleY = parseFloat(words[2]);
			break;

		case 'scroll':
			tcMod.sSpeed = parseFloat(words[1]);
			tcMod.tSpeed = parseFloat(words[2]);
			break;

		case 'stretch':
			tcMod.wave = parseWaveForm(words.slice(1));
			if (!tcMod.wave) { tcMod.type = null; }
			break;

		case 'turb':
			tcMod.turbulance = new Waveform();
			tcMod.turbulance.base = parseFloat(words[1]);
			tcMod.turbulance.amp = parseFloat(words[2]);
			tcMod.turbulance.phase = parseFloat(words[3]);
			tcMod.turbulance.freq = parseFloat(words[4]);
			break;

		default:
			tcMod.type = null;
			break;
	}
}

function parseWaveForm(words) {
	var wave = new Waveform();

	wave.funcName = words[0].toLowerCase();
	wave.base = parseFloat(words[1]);
	wave.amp = parseFloat(words[2]);
	wave.phase = parseFloat(words[3]);
	wave.freq = parseFloat(words[4]);

	return wave;
}

module.exports = {
	SORT:        SORT,

	Shader:      Shader,
	ShaderStage: ShaderStage,
	Deform:      Deform,
	TexMod:      TexMod,
	Waveform:    Waveform,

	load:  load,
};
