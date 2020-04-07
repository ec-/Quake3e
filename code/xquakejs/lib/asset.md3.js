var BitStream = require('./bit-buffer.js').BitStream;

var MAX_QPATH = 64;

var MD3_IDENT         = (('3'.charCodeAt() << 24) + ('P'.charCodeAt() << 16) + ('D'.charCodeAt() << 8) + 'I'.charCodeAt());
var MD3_VERSION       = 15;
var MD3_MAX_LODS      = 3;
var MD3_MAX_TRIANGLES = 8192;                              // per surface
var MD3_MAX_VERTS     = 4096;                              // per surface
var MD3_MAX_SHADERS   = 256;                               // per surface
var MD3_MAX_FRAMES    = 1024;                              // per model
var MD3_MAX_SURFACES  = 32;                                // per model
var MD3_MAX_TAGS      = 16;                                // per frame
var MD3_XYZ_SCALE     = (1.0/64);

var SHADER_MAX_VERTEXES = 4096;
var SHADER_MAX_INDEXES  = 6 * SHADER_MAX_VERTEXES;

var Md3 = function () {
	this.name      = null;
	this.flags     = 0;
	this.frames    = null;
	this.tags      = null;
	this.surfaces  = null;
	this.skins     = null;
};

var Md3Surface = function () {
	this.name        = null;
	this.numFrames   = 0;
	this.numVerts    = 0;
	this.shaders     = null;
	this.st          = null;
	this.indexes     = null;                               // triangles grouped in 3s
	this.xyz         = null;
	this.normals     = null;
	this.model       = null;
};

var Md3Frame = function () {
	this.bounds      = [                                   // float[6]
		[0, 0, 0],
		[0, 0, 0]
	];
	this.localOrigin = [0, 0, 0];                          // float[3]
	this.radius      = 0;                                  // float
	this.name        = null;                               // char[16]
};

var Md3Tag = function () {
	this.name   = null;                                    // char[MAX_QPATH]
	this.origin = [0, 0, 0];
	this.axis   = [
		[0, 0, 0],
		[0, 0, 0],
		[0, 0, 0]
	];
};

var Md3Header = function () {
	this.ident       = 0;                                  // int
	this.version     = 0;                                  // int
	this.name        = null;                               // char[MAX_QPATH], model name
	this.flags       = 0;                                  // int
	this.numFrames   = 0;                                  // int
	this.numTags     = 0;                                  // int
	this.numSurfaces = 0;                                  // int
	this.numSkins    = 0;                                  // int
	this.ofsFrames   = 0;                                  // int, offset for first frame
	this.ofsTags     = 0;                                  // int, numFrames * numTags
	this.ofsSurfaces = 0;                                  // int, first surface, others follow
	this.ofsEnd      = 0;                                  // int, end of file
};

var Md3SurfaceHeader = function () {
	this.ident         = 0;                                // int
	this.name          = null;                             // char[MAX_QPATH], polyset name
	this.flags         = 0;                                // int
	this.numFrames     = 0;                                // int, all surfaces in a model should have the same
	this.numShaders    = 0;                                // int, all surfaces in a model should have the same
	this.numVerts      = 0;                                // int
	this.numTriangles  = 0;                                // int
	this.ofsTriangles  = 0;                                // int
	this.ofsShaders    = 0;                                // int, offset from start of md3Surface_t
	this.ofsSt         = 0;                                // int, texture coords are common for all frames
	this.ofsXyzNormals = 0;                                // int, numVerts * numFrames
	this.ofsEnd        = 0;                                // int, next surface follows
};

function load(data) {
	var md3 = new Md3();

	var bb = new BitStream(data);

	// Load the md3's header.
	var header = new Md3Header();
	header.ident = bb.readInt32();
	header.version = bb.readInt32();

	if (header.version !== MD3_VERSION) {
		throw new Error('LoadMd3: Wrong version (' + header.version + ' should be ' + MD3_VERSION + ')');
	}

	header.name = bb.readASCIIString(MAX_QPATH);
	header.flags = bb.readInt32();
	header.numFrames = bb.readInt32();
	header.numTags = bb.readInt32();
	header.numSurfaces = bb.readInt32();
	header.numSkins = bb.readInt32();
	header.ofsFrames = bb.readInt32();
	header.ofsTags = bb.readInt32();
	header.ofsSurfaces = bb.readInt32();
	header.ofsEnd = bb.readInt32();

	// Validate the header.
	if (header.numFrames < 1) {
		throw new Error('LoadMd3: 0 frames');
	}

	md3.name = header.name;
	md3.frames = new Array(header.numFrames);
	md3.tags = new Array(header.numFrames * header.numTags);
	md3.surfaces = new Array(header.numSurfaces);
	md3.skins = new Array(header.numSkins);

	// Read all of the frames.
	bb.byteIndex = header.ofsFrames;

	for (var i = 0; i < header.numFrames; i++) {
		var frame = md3.frames[i] = new Md3Frame();
		for (var j = 0; j < 6; j++) {
			frame.bounds[Math.floor(j/3)][j % 3] = bb.readFloat32();
		}
		for (var j = 0; j < 3; j++) {
			frame.localOrigin[j] = bb.readFloat32();
		}
		frame.radius = bb.readFloat32();
		frame.name = bb.readASCIIString(16);
	}

	// Read all of the tags.
	bb.byteIndex = header.ofsTags;

	for (var i = 0; i < header.numFrames * header.numTags; i++) {
		var tag = md3.tags[i] = new Md3Tag();
		tag.name = bb.readASCIIString(MAX_QPATH);

		for (var j = 0; j < 3; j++) {
			tag.origin[j] = bb.readFloat32();
		}
		for (var j = 0; j < 9; j++) {
			tag.axis[Math.floor(j/3)][j % 3] = bb.readFloat32();
		}
	}

	// Read all of the meshes.
	var meshOffset = header.ofsSurfaces;

	for (var i = 0; i < header.numSurfaces; i++) {
		bb.byteIndex = meshOffset;

		// Load this surface's header.
		var surfheader = new Md3SurfaceHeader();
		surfheader.ident = bb.readInt32();
		surfheader.name = bb.readASCIIString(MAX_QPATH);
		surfheader.flags = bb.readInt32();
		surfheader.numFrames = bb.readInt32();
		surfheader.numShaders = bb.readInt32();
		surfheader.numVerts = bb.readInt32();
		surfheader.numTriangles = bb.readInt32();
		surfheader.ofsTriangles = bb.readInt32();
		surfheader.ofsShaders = bb.readInt32();
		surfheader.ofsSt = bb.readInt32();
		surfheader.ofsXyzNormals = bb.readInt32();
		surfheader.ofsEnd = bb.readInt32();

		// Calidate the surface's header.
		if (surfheader.numVerts > SHADER_MAX_VERTEXES) {
			throw new Error('LoadMd3: More than ' + SHADER_MAX_VERTEXES + ' verts on a surface (' + surfheader.numVerts + ')');
		}

		if (surfheader.numTriangles * 3 > SHADER_MAX_INDEXES) {
			throw new Error('LoadMd3: More than ' + (SHADER_MAX_INDEXES / 3) + ' triangles on a surface (' + surfheader.numTriangles + ')');
		}

		//
		var surf = md3.surfaces[i] = new Md3Surface();
		// Strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess.
		surf.name = surfheader.name.toLowerCase().replace(/_\d+/, '');
		surf.numFrames = surfheader.numFrames;
		surf.numVerts = surfheader.numVerts;
		surf.shaders = new Array(surfheader.numShaders);
		surf.indexes = new Array(surfheader.numTriangles * 3);
		surf.st = new Array(surfheader.numVerts);
		surf.xyz = new Array(surfheader.numFrames * surfheader.numVerts);
		surf.normals = new Array(surfheader.numFrames * surfheader.numVerts);

		// Register all the shaders.
		bb.byteIndex = meshOffset + surfheader.ofsShaders;
		for (var j = 0; j < surfheader.numShaders; j++) {
			bb.byteIndex = meshOffset + surfheader.ofsShaders + (MAX_QPATH + 4) * j
			var name = bb.readASCIIString(MAX_QPATH);
			surf.shaders[j] = name;
		}

		// Read all of the triangles.
		bb.byteIndex = meshOffset + surfheader.ofsTriangles;

		for (var j = 0; j < surfheader.numTriangles; j++) {
			for (var k = 0; k < 3; k++) {
				surf.indexes[j * 3 + k] = bb.readInt32();
			}
		}

		// Read all of the ST coordinates.
		bb.byteIndex = meshOffset + surfheader.ofsSt;

		for (var j = 0; j < surfheader.numVerts; j++) {
			var st = surf.st[j] = [0, 0];

			st[0] = bb.readFloat32();
			st[1] = bb.readFloat32();
		}

		// Read all of the xyz normals.
		bb.byteIndex = meshOffset + surfheader.ofsXyzNormals;

		for (var j = 0; j < surfheader.numFrames * surfheader.numVerts; j++) {
			var xyz = surf.xyz[j] = [0, 0, 0];
			var normal = surf.normals[j] = [0, 0, 0];

			for (var k = 0; k < 3; k++) {
				xyz[k] = bb.readInt16() * MD3_XYZ_SCALE;
			}

			// Convert from spherical coordinates to normalized vec3.
			var zenith = bb.readInt8();
			var azimuth = bb.readInt8();

			var lat = zenith * (2 * Math.PI) / 255;
			var lng = azimuth * (2 * Math.PI) / 255;

			normal[0] = Math.cos(lng) * Math.sin(lat);
			normal[1] = Math.sin(lng) * Math.sin(lat);
			normal[2] = Math.cos(lat);

			// normalize
			var len = normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2];
			if (len > 0) {
				len = 1 / Math.sqrt(len);
				normal[0] = normal[0] * len;
				normal[1] = normal[1] * len;
				normal[2] = normal[2] * len;
			}
		}

		meshOffset += surfheader.ofsEnd;
	}

	return md3;
}

function getTag(md3, frame, tagName) {
	if (frame >= md3.frames.length) {
		// It is possible to have a bad frame while changing models, so don't error.
		frame = md3.frames.length - 1;
	}

	var numTags = md3.tags.length / md3.frames.length;
	var offset = frame * numTags;

	for (var i = 0; i < numTags; i++) {
		var tag = md3.tags[offset + i];
		if (tag.name === tagName) {
			return tag;  // found it
		}
	}

	return null;
}

module.exports = {
	load: load,
	getTag: getTag
};
