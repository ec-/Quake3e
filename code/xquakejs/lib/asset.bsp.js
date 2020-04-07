var BitStream = require('./bit-buffer.js').BitStream;

var MAX_QPATH = 64;

var LUMP = {
	ENTITIES:     0,
	SHADERS:      1,
	PLANES:       2,
	NODES:        3,
	LEAFS:        4,
	LEAFSURFACES: 5,
	LEAFBRUSHES:  6,
	MODELS:       7,
	BRUSHES:      8,
	BRUSHSIDES:   9,
	DRAWVERTS:    10,
	DRAWINDEXES:  11,
	FOGS:         12,
	SURFACES:     13,
	LIGHTMAPS:    14,
	LIGHTGRID:    15,
	VISIBILITY:   16,
	NUM_LUMPS:    17
};

var MST = {
	BAD:           0,
	PLANAR:        1,
	PATCH:         2,
	TRIANGLE_SOUP: 3,
	FLARE:         4
};

var Bsp = function () {
	this.entities             = {};
	this.shaders              = null;
	this.planes               = null;
	this.nodes                = null;
	this.leafs                = null;
	this.leafSurfaces         = null;
	this.leafBrushes          = null;
	this.bmodels              = null;
	this.brushes              = null;
	this.brushSides           = null;
	this.verts                = null;
	this.indexes              = null;
	this.fogs                 = null;
	this.surfaces             = null;
	this.lightmaps            = null;
	this.lightGridOrigin      = [0, 0, 0];
	this.lightGridSize        = [64, 64, 128];
	this.lightGridInverseSize = [0, 0, 0];
	this.lightGridBounds      = [0, 0, 0];
	this.lightGridData        = null;
	this.numClusters          = 0;
	this.clusterBytes         = 0;
	this.vis                  = null;
};

var dheader_t = function () {
	this.ident    = null;                                  // byte * 4 (string)
	this.version  = 0;                                     // int32
	this.lumps    = new Array(LUMP.NUM_LUMPS);             // lumps_t * LUMP.NUM_LUMPS

	for (var i = 0; i < LUMP.NUM_LUMPS; i++) {
		this.lumps[i] = new lumps_t();
	}
};

var lumps_t = function () {
	this.fileofs  = 0;                                     // int32
	this.filelen = 0;                                      // int32
};

var dmodel_t = function () {
	this.bounds = [                                        // float32 * 6
		[0, 0, 0],
		[0, 0, 0]
	];
	this.firstSurface = 0;                                 // int32
	this.numSurfaces  = 0;                                 // int32
	this.firstBrush   = 0;                                 // int32
	this.numBrushes   = 0;                                 // int32
};
dmodel_t.size = 40;

var dshader_t = function () {
	this.shaderName   = null;                              // byte * MAX_QPATH (string)
	this.surfaceFlags = 0;                                 // int32
	this.contents     = 0;                                 // int32
};
dshader_t.size = 72;

var dplane_t = function () {
	this.normal = [0, 0, 0];                               // float32 * 3
	this.dist   = 0;                                       // float32
};
dplane_t.size = 16;

var dnode_t = function () {
	this.planeNum    = 0;                                  // int32
	this.childrenNum = [0, 0];                             // int32 * 2
	this.mins        = [0, 0, 0];                          // int32 * 3
	this.maxs        = [0, 0, 0];                          // int32 * 3
};
dnode_t.size = 36;

var dleaf_t = function () {
	this.cluster          = 0;                             // int32
	this.area             = 0;                             // int32
	this.mins             = [0, 0, 0];                     // int32 * 3
	this.maxs             = [0, 0, 0];                     // int32 * 3
	this.firstLeafSurface = 0;                             // int32
	this.numLeafSurfaces  = 0;                             // int32
	this.firstLeafBrush   = 0;                             // int32
	this.numLeafBrushes   = 0;                             // int32
};
dleaf_t.size = 48;

var dbrushside_t = function () {
	this.planeNum  = 0;                                    // int32
	this.shaderNum = 0;                                    // int32
};
dbrushside_t.size = 8;

var dbrush_t = function () {
	this.side      = 0;                                    // int32
	this.numSides  = 0;                                    // int32
	this.shaderNum = 0;                                    // int32
};
dbrush_t.size = 12;

var dfog_t = function () {
	this.shaderName  = null;                               // byte * MAX_QPATH (string)
	this.brushNum    = 0;                                  // int32
	this.visibleSide = 0;                                  // int32
};
dfog_t.size = 72;

var drawVert_t = function () {
	this.pos      = [0, 0, 0];                             // float32 * 3
	this.texCoord = [0, 0];                                // float32 * 2
	this.lmCoord  = [0, 0];                                // float32 * 2
	this.normal   = [0, 0, 0];                             // float32 * 3
	this.color    = [0, 0, 0, 0];                          // uint8 * 4
};
drawVert_t.size = 44;

var dsurface_t = function () {
	this.shaderNum      = 0;                               // int32
	this.fogNum         = 0;                               // int32
	this.surfaceType    = 0;                               // int32
	this.vertex         = 0;                               // int32
	this.vertCount      = 0;                               // int32
	this.meshVert       = 0;                               // int32
	this.meshVertCount  = 0;                               // int32
	this.lightmapNum    = 0;                               // int32
	this.lightmapX      = 0;
	this.lightmapY      = 0;
	this.lightmapWidth  = 0;
	this.lightmapHeight = 0;
	this.lightmapOrigin = [0, 0, 0];
	this.lightmapVecs   = [
		[0, 0, 0],
		[0, 0, 0],
		[0, 0, 0]
	];
	this.patchWidth     = 0;                               // int32
	this.patchHeight    = 0;                               // int32
};
dsurface_t.size = 104;


function load(data, opts) {
	var bb = new BitStream(data);

	// Parse the header.
	var header = new dheader_t();
	header.ident = bb.readASCIIString(4);
	header.version = bb.readInt32();
	for (var i = 0; i < LUMP.NUM_LUMPS; i++) {
		header.lumps[i].fileofs = bb.readInt32();
		header.lumps[i].filelen = bb.readInt32();
	}

	if (header.ident !== 'IBSP' && header.version !== 46 && header.version !== 47) {
		throw new Error('Invalid BSP version: ' + header.version);
	}

	var bsp = new Bsp();

	var should = function (lump) {
		return !opts || !opts.lumps || opts.lumps.indexOf(lump) !== -1;
	};

	if (should(LUMP.ENTITIES)) loadEntities(bsp, data, header.lumps[LUMP.ENTITIES]);
	if (should(LUMP.SHADERS)) loadShaders(bsp, data, header.lumps[LUMP.SHADERS]);
	if (should(LUMP.PLANES)) loadPlanes(bsp, data, header.lumps[LUMP.PLANES]);
	if (should(LUMP.NODES)) loadNodes(bsp, data, header.lumps[LUMP.NODES]);
	if (should(LUMP.LEAFS)) loadLeafs(bsp, data, header.lumps[LUMP.LEAFS]);
	if (should(LUMP.LEAFSURFACES)) loadLeafSurfaces(bsp, data, header.lumps[LUMP.LEAFSURFACES]);
	if (should(LUMP.LEAFBRUSHES)) loadLeafBrushes(bsp, data, header.lumps[LUMP.LEAFBRUSHES]);
	if (should(LUMP.MODELS)) loadBrushModels(bsp, data, header.lumps[LUMP.MODELS]);
	if (should(LUMP.BRUSHES)) loadBrushes(bsp, data, header.lumps[LUMP.BRUSHES]);
	if (should(LUMP.BRUSHSIDES)) loadBrushSides(bsp, data, header.lumps[LUMP.BRUSHSIDES]);
	if (should(LUMP.DRAWVERTS)) loadVerts(bsp, data, header.lumps[LUMP.DRAWVERTS]);
	if (should(LUMP.DRAWINDEXES)) loadIndexes(bsp, data, header.lumps[LUMP.DRAWINDEXES]);
	if (should(LUMP.FOGS)) loadFogs(bsp, data, header.lumps[LUMP.FOGS]);
	if (should(LUMP.SURFACES)) loadSurfaces(bsp, data, header.lumps[LUMP.SURFACES]);
	if (should(LUMP.LIGHTMAPS)) loadLightmaps(bsp, data, header.lumps[LUMP.LIGHTMAPS]);
	if (should(LUMP.LIGHTGRID)) loadLightGrid(bsp, data, header.lumps[LUMP.LIGHTGRID]);
	if (should(LUMP.VISIBILITY)) loadVisibility(bsp, data, header.lumps[LUMP.VISIBILITY]);

	return bsp;
}

function loadEntities(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var entityStr = ''
	var len = Math.ceil(lump.filelen / 1000)
	for(var i = 0; i < len; i++) {
		var max = i == len - 1 ? (lump.filelen % 1000) : 1000
		entityStr += bb.readASCIIString(max);
	}

	var entities = bsp.entities = [];

	entityStr.replace(/\{([^}]*)\}/mg, function($0, entitySrc) {
		var entity = {
			classname: 'unknown'
		};

		entitySrc.replace(/"(.+)" "(.+)"$/mg, function($0, key, value) {
			entity[key] = value;
		});

		entities.push(entity);
	});

	// Parse worldspawn.
	var worldspawn = entities[0];
	if (worldspawn.classname !== 'worldspawn') {
		throw new Error('worldspawn isn\'t the first entity');
	}

	// Check for a different grid size
	if (worldspawn.gridsize) {
		var split = worldspawn.gridsize.split(' ');
		bsp.lightGridSize[0] = parseFloat(split[0]);
		bsp.lightGridSize[1] = parseFloat(split[1]);
		bsp.lightGridSize[2] = parseFloat(split[2]);
	}
}

function loadShaders(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var shaders = bsp.shaders = new Array(lump.filelen / dshader_t.size);

	for (var i = 0; i < shaders.length; i++) {
		var shader = shaders[i] = new dshader_t();

		shader.shaderName = bb.readASCIIString(MAX_QPATH);
		shader.surfaceFlags = bb.readInt32();
		shader.contents = bb.readInt32();
	}
}

function loadPlanes(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var planes = bsp.planes = new Array(lump.filelen / dplane_t.size);

	for (var i = 0; i < planes.length; i++) {
		var plane = planes[i] = new dplane_t();

		plane.normal[0] = bb.readFloat32();
		plane.normal[1] = bb.readFloat32();
		plane.normal[2] = bb.readFloat32();
		plane.dist = bb.readFloat32();
		// plane.signbits = QMath.GetPlaneSignbits(plane.normal);
		// plane.type = QMath.PlaneTypeForNormal(plane.normal);
	}
}

function loadNodes(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var nodes = bsp.nodes = new Array(lump.filelen / dnode_t.size);

	for (var i = 0; i < nodes.length; i++) {
		var node = nodes[i] = new dnode_t();

		node.planeNum = bb.readInt32();
		node.childrenNum[0] = bb.readInt32();
		node.childrenNum[1] = bb.readInt32();
		node.mins[0] = bb.readInt32();
		node.mins[1] = bb.readInt32();
		node.mins[2] = bb.readInt32();
		node.maxs[0] = bb.readInt32();
		node.maxs[1] = bb.readInt32();
		node.maxs[2] = bb.readInt32();
	}
}

function loadLeafs(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var leafs = bsp.leafs = new Array(lump.filelen / dleaf_t.size);

	for (var i = 0; i < leafs.length; i++) {
		var leaf = leafs[i] = new dleaf_t();

		leaf.cluster = bb.readInt32();
		leaf.area = bb.readInt32();
		leaf.mins[0] = bb.readInt32();
		leaf.mins[1] = bb.readInt32();
		leaf.mins[2] = bb.readInt32();
		leaf.maxs[0] = bb.readInt32();
		leaf.maxs[1] = bb.readInt32();
		leaf.maxs[2] = bb.readInt32();
		leaf.firstLeafSurface = bb.readInt32();
		leaf.numLeafSurfaces = bb.readInt32();
		leaf.firstLeafBrush = bb.readInt32();
		leaf.numLeafBrushes = bb.readInt32();
	}
}

function loadLeafSurfaces(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var leafSurfaces = bsp.leafSurfaces = new Array(lump.filelen / 4);

	for (var i = 0; i < leafSurfaces.length; i++) {
		leafSurfaces[i] = bb.readInt32();
	}
}

function loadLeafBrushes(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var leafBrushes = bsp.leafBrushes = new Array(lump.filelen / 4);

	for (var i = 0; i < leafBrushes.length; i++) {
		leafBrushes[i] = bb.readInt32();
	}
}

function loadBrushModels(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var models = bsp.bmodels = new Array(lump.filelen / dmodel_t.size);

	for (var i = 0; i < models.length; i++) {
		var model = models[i] = new dmodel_t();

		model.bounds[0][0] = bb.readFloat32();
		model.bounds[0][1] = bb.readFloat32();
		model.bounds[0][2] = bb.readFloat32();

		model.bounds[1][0] = bb.readFloat32();
		model.bounds[1][1] = bb.readFloat32();
		model.bounds[1][2] = bb.readFloat32();

		model.firstSurface = bb.readInt32();
		model.numSurfaces = bb.readInt32();
		model.firstBrush = bb.readInt32();
		model.numBrushes = bb.readInt32();
	}
}

function loadBrushes(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var brushes = bsp.brushes = new Array(lump.filelen / dbrush_t.size);

	for (var i = 0; i < brushes.length; i++) {
		var brush = brushes[i] = new dbrush_t();

		brush.side = bb.readInt32();
		brush.numSides = bb.readInt32();
		brush.shaderNum = bb.readInt32();
	}
}

function loadBrushSides(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var brushSides = bsp.brushSides = new Array(lump.filelen / dbrushside_t.size);

	for (var i = 0; i < brushSides.length; i++) {
		var side = brushSides[i] = new dbrushside_t();

		side.planeNum = bb.readInt32();
		side.shaderNum = bb.readInt32();
	}
}

function loadVerts(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var verts = bsp.verts = new Array(lump.filelen / drawVert_t.size);

	for (var i = 0; i < verts.length; i++) {
		var vert = verts[i] = new drawVert_t();

		vert.pos[0] = bb.readFloat32();
		vert.pos[1] = bb.readFloat32();
		vert.pos[2] = bb.readFloat32();
		vert.texCoord[0] = bb.readFloat32();
		vert.texCoord[1] = bb.readFloat32();
		vert.lmCoord[0] = bb.readFloat32();
		vert.lmCoord[1] = bb.readFloat32();
		vert.normal[0] = bb.readFloat32();
		vert.normal[1] = bb.readFloat32();
		vert.normal[2] = bb.readFloat32();
		vert.color[0] = bb.readUint8();
		vert.color[1] = bb.readUint8();
		vert.color[2] = bb.readUint8();
		vert.color[3] = bb.readUint8();
	}
}

function loadIndexes(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var indexes = bsp.indexes = new Array(lump.filelen / 4);

	for (var i = 0; i < indexes.length; i++) {
		indexes[i] = bb.readInt32();
	}
}

function loadFogs(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var fogs = bsp.fogs = new Array(lump.filelen / dfog_t.size);

	for (var i = 0; i < fogs.length; i++) {
		var fog = fogs[i] = new dfog_t();

		fog.shaderName = bb.readASCIIString(MAX_QPATH);
		fog.brushNum = bb.readInt32();
		fog.visibleSide = bb.readInt32();
	}
}

function loadSurfaces(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	var surfaces = bsp.surfaces = new Array(lump.filelen / dsurface_t.size);

	for (var i = 0; i < surfaces.length; i++) {
		var surface = surfaces[i] = new dsurface_t();

		surface.shaderNum = bb.readInt32();
		surface.fogNum = bb.readInt32();
		surface.surfaceType = bb.readInt32();
		surface.vertex = bb.readInt32();
		surface.vertCount = bb.readInt32();
		surface.meshVert = bb.readInt32();
		surface.meshVertCount = bb.readInt32();
		surface.lightmapNum = bb.readInt32();
		surface.lightmapX = bb.readInt32();
		surface.lightmapY = bb.readInt32();
		surface.lightmapWidth = bb.readInt32();
		surface.lightmapHeight = bb.readInt32();
		surface.lightmapOrigin[0] = bb.readFloat32();
		surface.lightmapOrigin[1] = bb.readFloat32();
		surface.lightmapOrigin[2] = bb.readFloat32();
		surface.lightmapVecs[0][0] = bb.readFloat32();
		surface.lightmapVecs[0][1] = bb.readFloat32();
		surface.lightmapVecs[0][2] = bb.readFloat32();
		surface.lightmapVecs[1][0] = bb.readFloat32();
		surface.lightmapVecs[1][1] = bb.readFloat32();
		surface.lightmapVecs[1][2] = bb.readFloat32();
		surface.lightmapVecs[2][0] = bb.readFloat32();
		surface.lightmapVecs[2][1] = bb.readFloat32();
		surface.lightmapVecs[2][2] = bb.readFloat32();
		surface.patchWidth = bb.readInt32();
		surface.patchHeight = bb.readInt32();
	}
}

/**
 * LoadLightmaps
 */
function loadLightmaps(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	bsp.lightmaps = new Uint8Array(lump.filelen);

	for (var i = 0; i < lump.filelen; i++) {
		bsp.lightmaps[i] = bb.readUint8();
	}
}

function loadLightGrid(bsp, buffer, lump) {
	bsp.lightGridInverseSize[0] = 1 / bsp.lightGridSize[0];
	bsp.lightGridInverseSize[1] = 1 / bsp.lightGridSize[1];
	bsp.lightGridInverseSize[2] = 1 / bsp.lightGridSize[2];

	var wMins = bsp.bmodels[0].bounds[0];
	var wMaxs = bsp.bmodels[0].bounds[1];

	for (var i = 0; i < 3; i++) {
		bsp.lightGridOrigin[i] = bsp.lightGridSize[i] * Math.ceil(wMins[i] / bsp.lightGridSize[i]);
		var t = bsp.lightGridSize[i] * Math.floor(wMaxs[i] / bsp.lightGridSize[i]);
		bsp.lightGridBounds[i] = (t - bsp.lightGridOrigin[i])/bsp.lightGridSize[i] + 1;
	}

	var numGridPoints = bsp.lightGridBounds[0] * bsp.lightGridBounds[1] * bsp.lightGridBounds[2];

	if (lump.filelen !== numGridPoints * 8) {
		// log('WARNING: light grid mismatch', lump.filelen, numGridPoints * 8);
		bsp.lightGridData = null;
		return;
	}

	// Read the actual light grid data.
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	bsp.lightGridData = new Uint8Array(lump.filelen);
	for (var i = 0; i < lump.filelen; i++) {
		bsp.lightGridData[i] = bb.readUint8();
	}
}

function loadVisibility(bsp, buffer, lump) {
	var bb = new BitStream(buffer);
	bb.byteIndex = lump.fileofs;

	bsp.numClusters = bb.readInt32();
	bsp.clusterBytes = bb.readInt32();

	var size = bsp.numClusters * bsp.clusterBytes;
	bsp.vis = new Uint8Array(size);

	for (var i = 0; i < size; i++) {
		bsp.vis[i] = bb.readUint8();
	}
}

module.exports = {
	LUMP:         LUMP,
	MST:          MST,

	dmodel_t:     dmodel_t,
	dshader_t:    dshader_t,
	dplane_t:     dplane_t,
	dnode_t:      dnode_t,
	dleaf_t:      dleaf_t,
	dbrushside_t: dbrushside_t,
	dbrush_t:     dbrush_t,
	dfog_t:       dfog_t,
	drawVert_t:   drawVert_t,
	dsurface_t:   dsurface_t,

	load:         load
};
