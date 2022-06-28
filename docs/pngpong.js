(function webpackUniversalModuleDefinition(root, factory) {
	if(typeof exports === 'object' && typeof module === 'object')
		module.exports = factory();
	else if(typeof define === 'function' && define.amd)
		define([], factory);
	else {
		var a = factory();
		for(var i in a) (typeof exports === 'object' ? exports : root)[i] = a[i];
	}
})(this, function() {
return /******/ (function(modules) { // webpackBootstrap
/******/ 	// The module cache
/******/ 	var installedModules = {};
/******/
/******/ 	// The require function
/******/ 	function __webpack_require__(moduleId) {
/******/
/******/ 		// Check if module is in cache
/******/ 		if(installedModules[moduleId]) {
/******/ 			return installedModules[moduleId].exports;
/******/ 		}
/******/ 		// Create a new module (and put it into the cache)
/******/ 		var module = installedModules[moduleId] = {
/******/ 			i: moduleId,
/******/ 			l: false,
/******/ 			exports: {}
/******/ 		};
/******/
/******/ 		// Execute the module function
/******/ 		modules[moduleId].call(module.exports, module, module.exports, __webpack_require__);
/******/
/******/ 		// Flag the module as loaded
/******/ 		module.l = true;
/******/
/******/ 		// Return the exports of the module
/******/ 		return module.exports;
/******/ 	}
/******/
/******/
/******/ 	// expose the modules object (__webpack_modules__)
/******/ 	__webpack_require__.m = modules;
/******/
/******/ 	// expose the module cache
/******/ 	__webpack_require__.c = installedModules;
/******/
/******/ 	// identity function for calling harmony imports with the correct context
/******/ 	__webpack_require__.i = function(value) { return value; };
/******/
/******/ 	// define getter function for harmony exports
/******/ 	__webpack_require__.d = function(exports, name, getter) {
/******/ 		if(!__webpack_require__.o(exports, name)) {
/******/ 			Object.defineProperty(exports, name, {
/******/ 				configurable: false,
/******/ 				enumerable: true,
/******/ 				get: getter
/******/ 			});
/******/ 		}
/******/ 	};
/******/
/******/ 	// getDefaultExport function for compatibility with non-harmony modules
/******/ 	__webpack_require__.n = function(module) {
/******/ 		var getter = module && module.__esModule ?
/******/ 			function getDefault() { return module['default']; } :
/******/ 			function getModuleExports() { return module; };
/******/ 		__webpack_require__.d(getter, 'a', getter);
/******/ 		return getter;
/******/ 	};
/******/
/******/ 	// Object.prototype.hasOwnProperty.call
/******/ 	__webpack_require__.o = function(object, property) { return Object.prototype.hasOwnProperty.call(object, property); };
/******/
/******/ 	// __webpack_public_path__
/******/ 	__webpack_require__.p = "";
/******/
/******/ 	// Load entry module and return exports
/******/ 	return __webpack_require__(__webpack_require__.s = 16);
/******/ })
/************************************************************************/
/******/ ([
/* 0 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.PNGColorType = exports.IHDRLength = void 0;
exports.readIHDR = readIHDR;
exports.writeIHDR = writeIHDR;

/**
 * The color type our image uses. PngPong currently only supports
 * Palette images, PNGColorType.Palette
 * 
 * @export
 * @enum {number}
 */
var PNGColorType;
/**
 * The attributes for an IHDR chunk as defined in 
 * http://www.libpng.org/pub/png/spec/1.2/PNG-Chunks.html#C.IHDR
 * 
 * @export
 * @interface IHDROptions
 */

exports.PNGColorType = PNGColorType;

(function (PNGColorType) {
  PNGColorType[PNGColorType["Grayscale"] = 0] = "Grayscale";
  PNGColorType[PNGColorType["RGB"] = 2] = "RGB";
  PNGColorType[PNGColorType["Palette"] = 3] = "Palette";
  PNGColorType[PNGColorType["GrayscaleWithAlpha"] = 4] = "GrayscaleWithAlpha";
  PNGColorType[PNGColorType["RGBA"] = 6] = "RGBA";
})(PNGColorType || (exports.PNGColorType = PNGColorType = {}));

function writeIHDR(walker, options) {
  // IHDR length is always 13 bytes
  walker.writeUint32(13);
  walker.startCRC();
  walker.writeString("IHDR");
  walker.writeUint32(options.width);
  walker.writeUint32(options.height);
  walker.writeUint8(options.bitDepth);
  walker.writeUint8(options.colorType);
  walker.writeUint8(options.compressionMethod);
  walker.writeUint8(options.filter);
  walker.writeUint8(options["interface"]);
  walker.writeCRC();
}
/**
 * Read out the values contained within IHDR. Does not let you edit these
 * values, as changing pretty much any of them would make the IDAT chunk
 * totally invalid.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 * @param {number} length 
 * @returns {IHDROptions} 
 */


function readIHDR(walker, length) {
  if (length !== 13) {
    throw new Error("IHDR length must always be 13");
  }

  var width = walker.readUint32();
  var height = walker.readUint32();
  var bitDepth = walker.readUint8();
  var colorType = walker.readUint8();
  var compressionMethod = walker.readUint8();
  var filter = walker.readUint8();
  var pngInterface = walker.readUint8(); // Don't do anything with this as we can't edit the header

  var crc = walker.readUint32();
  return {
    width: width,
    height: height,
    bitDepth: bitDepth,
    colorType: colorType,
    compressionMethod: compressionMethod,
    filter: filter,
    "interface": pngInterface
  };
}
/**
 *  IHDR length is always 13 bytes. So we can store this as a constant.
 */


var IHDRLength = 4 // Chunk length identifier
+ 4 // chunk header
+ 13 // actual IHDR length
+ 4; // CRC32 check;

exports.IHDRLength = IHDRLength;

/***/ }),
/* 1 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.Palette = void 0;
exports.calculatePaletteLength = calculatePaletteLength;
exports.readPalette = readPalette;
exports.writePalette = writePalette;

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } }

function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }

/**
 * Write both the PLTE and tRNS chunks of the PNG file.
 *
 * @export
 * @param {ArrayBufferWalker} walker
 * @param {Uint8ClampedArray} rgbPalette
 * @param {Uint8ClampedArray} alphaPalette
 */
function writePalette(walker, rgbPalette, alphaPalette) {
  // Write PTLE
  walker.writeUint32(rgbPalette.length);
  walker.startCRC();
  walker.writeString("PLTE");

  for (var i = 0; i < rgbPalette.length; i++) {
    walker.writeUint8(rgbPalette[i]);
  }

  walker.writeCRC(); // Write tRNS

  walker.writeUint32(alphaPalette.length);
  walker.startCRC();
  walker.writeString("tRNS");

  for (var _i = 0; _i < alphaPalette.length; _i++) {
    walker.writeUint8(alphaPalette[_i]);
  }

  walker.writeCRC();
}
/**
 * Testing showed that creating new UInt8Arrays was an expensive process, so instead
 * of slicing the array to mark the PLTE and tRNS arrays, we instead store their
 * offset and length.
 *
 * @export
 * @interface OffsetAndLength
 */


/**
 * A manager that handles both the PLTE and tRNS chunks, as they depend upon each other.
 *
 * @export
 * @class Palette
 */
var Palette = /*#__PURE__*/function () {
  function Palette(walker, rgbPalette, alphaPalette) {
    _classCallCheck(this, Palette);
  }

  _createClass(Palette, [{
    key: "checkColor",
    value: function checkColor(rgba) {
      if (rgba.length < 3 || rgba.length > 4) {
        throw new Error("Needs to be a 3 or 4 length array to check for color");
      }

      if (rgba.length === 3 && this.alphaPalette) {
        // If we need to search for alpha, just insert zero transparency
        rgba.push(255);
      }
    }
    /**
     * Return the RGBA color at the index provided. If there is no tRNS chunk the
     * color will always have an alpha value of 255.
     *
     * @param {number} idx
     * @returns The RGBA color at this index. If the color hasn't been specified it
     * will come back as [0,0,0,255].
     *
     * @memberof Palette
     */

  }, {
    key: "getColorAtIndex",
    value: function getColorAtIndex(idx) {
      var rgbStartingIndex = idx * 3;
      var rgba = [this.walker.array[this.rgbPalette.offset + rgbStartingIndex], this.walker.array[this.rgbPalette.offset + rgbStartingIndex + 1], this.walker.array[this.rgbPalette.offset + rgbStartingIndex + 2], 255];

      if (this.alphaPalette) {
        rgba[3] = this.walker.array[this.alphaPalette.offset + idx];
      }

      return rgba;
    }
    /**
     * Get the palette index for an existing color.
     *
     * @param {RGBA} rgba
     * @param {number} [startingIndex=0] - used internally to skip the first palette entry, which is always rgba(0,0,0,0)
     * @returns The index of the color, or -1 if the color has not yet been added to the palette.
     *
     * @memberof Palette
     */

  }, {
    key: "getColorIndex",
    value: function getColorIndex(rgba) {
      var startingIndex = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : 0;
      this.checkColor(rgba);

      for (var i = this.rgbPalette.offset + startingIndex * 3; i < this.rgbPalette.offset + this.rgbPalette.length; i = i + 3) {
        if (this.walker.array[i] === rgba[0] && this.walker.array[i + 1] === rgba[1] && this.walker.array[i + 2] === rgba[2]) {
          // Because this an array of RGB values, the actual index is / 3
          var index = (i - this.rgbPalette.offset) / 3;

          if (!this.alphaPalette) {
            // If we have no alpha palette then we've found our match.
            return index;
          } else if (this.alphaPalette && this.walker.array[this.alphaPalette.offset + index] === rgba[3]) {
            // Otherwise we need to check the alpha palette too.
            return index;
          }
        }
      }

      return -1;
    }
    /**
     * Add a color to the palette. Must be an RGBA color, even if we're not using tRNS (to do: fix that)
     *
     * @param {RGBA} rgba
     * @returns the index the color was added at.
     *
     * @memberof Palette
     */

  }, {
    key: "addColor",
    value: function addColor(rgba) {
      // need to save this to reset later.
      var currentWalkerOffset = this.walker.offset;
      this.checkColor(rgba); // We start at index 1 because the PNGWriter stores 0,0,0,0 at palette index #0
      // and we want to ignore that.

      var vacantSpace = this.getColorIndex([0, 0, 0, 0], 1);

      if (vacantSpace === -1) {
        if (this.rgbPalette.length < 255) {
          throw new Error("No space left in palette. You need to create a source image with more space.");
        }

        throw new Error("No space left in palette. You need to use fewer colours.");
      }

      var rgbStartWriteAt = this.rgbPalette.offset + vacantSpace * 3; // This feels like it kind of breaks the logic of using a walker
      // but the palette is this weird thing that we need to access at
      // different points...

      this.walker.offset = rgbStartWriteAt;
      this.walker.writeUint8(rgba[0]);
      this.walker.writeUint8(rgba[1]);
      this.walker.writeUint8(rgba[2]);

      if (this.alphaPalette) {
        this.walker.offset = this.alphaPalette.offset + vacantSpace;
        this.walker.writeUint8(rgba[3]);
      } else if (!this.alphaPalette && rgba[3] !== 255) {
        throw new Error("No alpha palette but color has alpha value.");
      }

      this.walker.offset = currentWalkerOffset;
      return vacantSpace;
    }
    /**
     * Re-calculate the CRC blocks for both palettes reflecting any changes made.
     *
     * @memberof Palette
     */

  }, {
    key: "writeCRCs",
    value: function writeCRCs() {
      // CRC writing for the palettes is a little complicated because we're actually dealing with
      // two separate chunks. First we move to the offset of the RGB palette (and back another 4
      // bytes so that we include the PLTE indentifier):
      this.walker.offset = this.rgbPalette.offset - 4; // Then tell the walker to mark the start point of the data to CRC

      this.walker.startCRC(); // This is a bit of a cheat - because we've already written the palette we actually just move
      // the position of the walker to the end of the palette...

      this.walker.skip(4 + this.rgbPalette.length); // Which means the CRC calculation will be based on the correct start and end points.

      this.walker.writeCRC(); // Then just do the same with tRNS.

      if (this.alphaPalette) {
        this.walker.offset = this.alphaPalette.offset - 4;
        this.walker.startCRC();
        this.walker.skip(4 + this.alphaPalette.length);
        this.walker.writeCRC();
      }
    }
  }]);

  return Palette;
}();
/**
 * Take an ArrayWalker and parse out the PLTE chunk and, if it exists, the tRNS chunk.
 * If it exists, the tRNS chunk MUST immediately follow the PLTE chunk.
 *
 * @export
 * @param {ArrayBufferWalker} walker
 * @param {number} length
 * @returns
 */


exports.Palette = Palette;

function readPalette(walker, length) {
  var rgbPaletteBounds = {
    offset: walker.offset,
    length: length
  };
  walker.skip(length);
  var rgbCRC = walker.readUint32(); // We might have a tRNS block next. But we also might not!

  var nextBlockLength = walker.readUint32();
  var nextBlockIdentifier = walker.readString(4);

  if (nextBlockIdentifier !== "tRNS") {
    // We want to move it back so that the transformer reader
    // can parse this block itself.
    walker.rewindString(4);
    walker.rewindUint32();
    return new Palette(walker, rgbPaletteBounds);
  } else {
    var alphaPalette = {
      offset: walker.offset,
      length: nextBlockLength
    };
    walker.skip(nextBlockLength);
    return new Palette(walker, rgbPaletteBounds, alphaPalette);
  }
}
/**
 * PNG files can have palettes of varying sizes, up to 256 colors. If we want
 * to try to save some space, we can use a smaller palette.
 *
 * @export
 * @param {number} numColors
 * @returns
 */


function calculatePaletteLength(numColors) {
  return numColors * 3 + // PLTE chunk size
  4 + // PLTE identifier
  4 + // PLTE CRC
  4 + // PLTE length
  numColors + // tRNS chunk Size
  4 + // tRNS identifier
  4 + // tRNS CRC
  4; // tRNS length
}

/***/ }),
/* 2 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.ArrayBufferWalker = void 0;

var _crc = __webpack_require__(14);

var _adler = __webpack_require__(13);

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } }

function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }

function _defineProperty(obj, key, value) { if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }

function swap16(val) {
  return (val & 0xFF) << 8 | val >> 8 & 0xFF;
}

function swap32(val) {
  return (val & 0xFF) << 24 | (val & 0xFF00) << 8 | val >> 8 & 0xFF00 | val >> 24 & 0xFF;
}
/**
 * A class that "walks" through an ArrayBuffer, either reading or writing
 * values as it goes. Intended as a less performance-draining alternative
 * to a DataView.
 * 
 * @export
 * @class ArrayBufferWalker
 */


var ArrayBufferWalker = /*#__PURE__*/function () {
  /**
   * The current index our walker is sat at. Can be modified.
   * 
   * @memberof ArrayBufferWalker
   */

  /**
   * Creates an instance of ArrayBufferWalker.
   * @param {(ArrayBuffer | number)} bufferOrLength - either an existing ArrayBuffer
   * or the length of a new array you want to use.
   * 
   * @memberof ArrayBufferWalker
   */
  function ArrayBufferWalker(bufferOrLength) {
    _classCallCheck(this, ArrayBufferWalker);

    _defineProperty(this, "offset", 0);

    _defineProperty(this, "array", void 0);

    _defineProperty(this, "crcStartOffset", void 0);

    _defineProperty(this, "adlerStartOffset", void 0);

    _defineProperty(this, "savedAdlerValue", void 0);

    if (bufferOrLength instanceof ArrayBuffer) {
      this.array = new Uint8Array(bufferOrLength);
    } else {
      this.array = new Uint8Array(bufferOrLength);
    }
  }

  _createClass(ArrayBufferWalker, [{
    key: "writeUint32",
    value: function writeUint32(value) {
      var littleEndian = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;

      if (littleEndian) {
        value = swap32(value);
      }

      this.array[this.offset++] = value >> 24 & 255;
      this.array[this.offset++] = value >> 16 & 255;
      this.array[this.offset++] = value >> 8 & 255;
      this.array[this.offset++] = value & 255;
    }
  }, {
    key: "writeUint16",
    value: function writeUint16(value) {
      var littleEndian = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;

      if (littleEndian) {
        value = swap16(value);
      }

      this.array[this.offset++] = value >> 8 & 255;
      this.array[this.offset++] = value & 255;
    }
  }, {
    key: "writeUint8",
    value: function writeUint8(value) {
      this.array[this.offset++] = value & 255;
    }
  }, {
    key: "writeString",
    value: function writeString(value) {
      for (var i = 0, n = value.length; i < n; i++) {
        this.array[this.offset++] = value.charCodeAt(i);
      }
    }
  }, {
    key: "readUint32",
    value: function readUint32() {
      var littleEndian = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
      var val = this.array[this.offset++] << 24;
      val += this.array[this.offset++] << 16;
      val += this.array[this.offset++] << 8;
      val += this.array[this.offset++] & 255;
      return littleEndian ? swap32(val) : val;
    }
  }, {
    key: "readUint16",
    value: function readUint16() {
      var littleEndian = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
      var val = this.array[this.offset++] << 8;
      val += this.array[this.offset++] & 255;
      return littleEndian ? swap16(val) : val;
    }
  }, {
    key: "readUint8",
    value: function readUint8() {
      return this.array[this.offset++] & 255;
    }
  }, {
    key: "readString",
    value: function readString(length) {
      var result = "";
      var target = this.offset + length;

      while (this.offset < target) {
        result += String.fromCharCode(this.array[this.offset++]);
      }

      return result;
    }
    /**
     * Move around the array without writing or reading a value.
     * 
     * @param {any} length 
     * 
     * @memberof ArrayBufferWalker
     */

  }, {
    key: "skip",
    value: function skip(length) {
      this.offset += length;
    }
  }, {
    key: "rewindUint32",
    value: function rewindUint32() {
      this.offset -= 4;
    }
  }, {
    key: "rewindString",
    value: function rewindString(length) {
      this.offset -= length;
    }
  }, {
    key: "startCRC",
    value:
    /**
     * Mark the beginning of an area we want to calculate the CRC for.
     * 
     * @memberof ArrayBufferWalker
     */
    function startCRC() {
      if (this.crcStartOffset) {
        throw new Error("CRC already started");
      }

      this.crcStartOffset = this.offset;
    }
    /**
     * After using .startCRC() to mark the start of a block, use this to mark the
     * end of the block and write the UInt32 CRC value.
     * 
     * @memberof ArrayBufferWalker
     */

  }, {
    key: "writeCRC",
    value: function writeCRC() {
      if (this.crcStartOffset === undefined) {
        throw new Error("CRC has not been started, cannot write");
      }

      var crc = (0, _crc.crc32)(this.array, this.crcStartOffset, this.offset - this.crcStartOffset);
      this.crcStartOffset = undefined;
      this.writeUint32(crc);
    }
  }, {
    key: "startAdler",
    value:
    /**
     * Similar to .startCRC(), this marks the start of a block we want to calculate the
     * ADLER32 checksum of.
     * 
     * @memberof ArrayBufferWalker
     */
    function startAdler() {
      if (this.adlerStartOffset) {
        throw new Error("Adler already started");
      }

      this.adlerStartOffset = this.offset;
    }
    /**
     * ADLER32 is used in our ZLib blocks, but can span across multiple blocks. So sometimes
     * we need to pause it in order to start a new block.
     * 
     * @memberof ArrayBufferWalker
     */

  }, {
    key: "pauseAdler",
    value: function pauseAdler() {
      if (this.adlerStartOffset === undefined) {
        throw new Error("Adler has not been started, cannot pause");
      }

      this.savedAdlerValue = (0, _adler.adler32_buf)(this.array, this.adlerStartOffset, this.offset - this.adlerStartOffset, this.savedAdlerValue);
      this.adlerStartOffset = undefined;
    }
    /**
     * Similar to .writeCRC(), this marks the end of an ADLER32 checksummed block, and
     * writes the Uint32 checksum value to the ArrayBuffer.
     * 
     * @returns 
     * 
     * @memberof ArrayBufferWalker
     */

  }, {
    key: "writeAdler",
    value: function writeAdler() {
      if (this.adlerStartOffset === undefined && this.savedAdlerValue === undefined) {
        throw new Error("CRC has not been started, cannot write");
      }

      if (this.adlerStartOffset === undefined) {
        this.writeUint32(this.savedAdlerValue);
        this.savedAdlerValue = undefined;
        return;
      }

      var adler = (0, _adler.adler32_buf)(this.array, this.adlerStartOffset, this.offset - this.adlerStartOffset, this.savedAdlerValue);
      this.adlerStartOffset = undefined;
      this.writeUint32(adler);
    }
  }]);

  return ArrayBufferWalker;
}();

exports.ArrayBufferWalker = ArrayBufferWalker;

/***/ }),
/* 3 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
Object.defineProperty(exports, "ArrayBufferWalker", {
  enumerable: true,
  get: function get() {
    return _arraybufferWalker.ArrayBufferWalker;
  }
});
Object.defineProperty(exports, "IHDROptions", {
  enumerable: true,
  get: function get() {
    return _ihdr.IHDROptions;
  }
});
Object.defineProperty(exports, "Palette", {
  enumerable: true,
  get: function get() {
    return _palette.Palette;
  }
});
Object.defineProperty(exports, "PngPong", {
  enumerable: true,
  get: function get() {
    return _pngPong.PngPong;
  }
});
Object.defineProperty(exports, "PngPongImageCopyTransformer", {
  enumerable: true,
  get: function get() {
    return _copyImage.PngPongImageCopyTransformer;
  }
});
Object.defineProperty(exports, "PngPongShapeTransformer", {
  enumerable: true,
  get: function get() {
    return _shape.PngPongShapeTransformer;
  }
});
Object.defineProperty(exports, "createFromRGBAArray", {
  enumerable: true,
  get: function get() {
    return _writer.createFromRGBAArray;
  }
});
Object.defineProperty(exports, "createWithMetadata", {
  enumerable: true,
  get: function get() {
    return _writer.createWithMetadata;
  }
});

var _pngPong = __webpack_require__(9);

var _writer = __webpack_require__(15);

var _arraybufferWalker = __webpack_require__(2);

var _palette = __webpack_require__(1);

var _ihdr = __webpack_require__(0);

var _shape = __webpack_require__(12);

var _copyImage = __webpack_require__(11);

/***/ }),
/* 4 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.checkPreheader = checkPreheader;
exports.length = void 0;
exports.writePreheader = writePreheader;
var PRE_HEADER = '\x89PNG\r\n\x1A\n';
/**
 * PNG files have a very basic header that identifies the PNG
 * file as... a PNG file. We need to write that out.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 */

function writePreheader(walker) {
  walker.writeString(PRE_HEADER);
}
/**
 * Make sure that we're dealing with a PNG file. Throws an error
 * if the file does not start with the standard PNG header.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 */


function checkPreheader(walker) {
  var value = walker.readString(PRE_HEADER.length);

  if (value !== PRE_HEADER) {
    throw new Error("Buffer does not have a PNG file header.");
  }
}

var length = PRE_HEADER.length;
exports.length = length;

/***/ }),
/* 5 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.ZlibWriter = exports.ZLIB_WINDOW_SIZE = exports.BLOCK_SIZE = void 0;
exports.calculateZlibbedLength = calculateZlibbedLength;
exports.readZlib = readZlib;

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } }

function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }

function _defineProperty(obj, key, value) { if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }

var ZLIB_WINDOW_SIZE = 1024 * 32; // 32KB

exports.ZLIB_WINDOW_SIZE = ZLIB_WINDOW_SIZE;
var BLOCK_SIZE = 65535;
/**
 * Zlibbed data takes up more space than the raw data itself - we aren't
 * compressing it but we do need to add block headers and the like.
 * 
 * @export
 * @param {number} dataLength 
 * @returns 
 */

exports.BLOCK_SIZE = BLOCK_SIZE;

function calculateZlibbedLength(dataLength) {
  var numberOfBlocks = Math.ceil(dataLength / BLOCK_SIZE);
  return 1 // Compression method/flags code
  + 1 // Additional flags/check bits
  + 5 * numberOfBlocks // Number of Zlib block headers we'll need
  + 4 // ADLER checksum
  + dataLength; // The actual data.
}

;
/**
 * Our tool for writing IDAT chunks. Although Zlib is used for compression,
 * we aren't compressing anything here. Basically, this writes a Zlib chunk
 * as if it is set to a compression level of 0.
 * 
 * @export
 * @class ZlibWriter
 */

var ZlibWriter = /*#__PURE__*/function () {
  function ZlibWriter(walker, dataLength) {
    _classCallCheck(this, ZlibWriter);

    _defineProperty(this, "bytesLeftInWindow", 0);

    _defineProperty(this, "bytesLeft", void 0);

    this.bytesLeft = dataLength;
    this.writeZlibHeader();
    this.startBlock();
  }

  _createClass(ZlibWriter, [{
    key: "writeZlibHeader",
    value: function writeZlibHeader() {
      // http://stackoverflow.com/questions/9050260/what-does-a-zlib-header-look-like
      var cinfo = Math.LOG2E * Math.log(ZLIB_WINDOW_SIZE) - 8;
      var compressionMethod = 8; // DEFLATE, only valid value.

      var zlibHeader = new Uint8Array(2);
      var cmf = cinfo << 4 | compressionMethod; // Lifted a lot of this code from here: https://github.com/imaya/zlib.js/blob/master/src/deflate.js#L110

      var fdict = 0; // not totally sure what this is for

      var flevel = 0; // compression level. We don't want to compress at all

      var flg = flevel << 6 | fdict << 5;
      var fcheck = 31 - (cmf * 256 + flg) % 31;
      flg |= fcheck;
      this.walker.writeUint8(cmf);
      this.walker.writeUint8(flg);
    }
  }, {
    key: "startBlock",
    value: function startBlock() {
      // Whether this is the final block. If we've got less than 32KB to write, then yes.
      var bfinal = this.bytesLeft < BLOCK_SIZE ? 1 : 0; // Compression type. Will always be zero = uncompressed

      var btype = 0; // Again, this logic comes from: https://github.com/imaya/zlib.js/blob/master/src/deflate.js#L110

      var blockLength = Math.min(this.bytesLeft, BLOCK_SIZE);
      this.walker.writeUint8(bfinal | btype << 1);
      var nlen = ~blockLength + 0x10000 & 0xffff; // IMPORTANT: these values must be little-endian.

      this.walker.writeUint16(blockLength, true);
      this.walker.writeUint16(nlen, true);
      this.bytesLeftInWindow = Math.min(this.bytesLeft, BLOCK_SIZE);
      this.walker.startAdler();
    }
  }, {
    key: "writeUint8",
    value: function writeUint8(val) {
      if (this.bytesLeft <= 0) {
        throw new Error("Ran out of space");
      }

      if (this.bytesLeftInWindow === 0 && this.bytesLeft > 0) {
        this.walker.pauseAdler();
        this.startBlock();
      }

      this.walker.writeUint8(val);
      this.bytesLeftInWindow--;
      this.bytesLeft--;
    }
  }, {
    key: "end",
    value: function end() {
      this.walker.writeAdler();
    }
  }]);

  return ZlibWriter;
}();

exports.ZlibWriter = ZlibWriter;

/**
 * Utility function to parse out a Zlib-encoded block (at a compression level of 0 only). Will
 * skip over Zlib headers and block markers, and call the dataCallback repeatedly when actual
 * data is available.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 * @param {ZlibReadCallback} dataCallback 
 */
function readZlib(walker, dataCallback) {
  // Disregard Zlib header
  walker.skip(2);
  var bfinal = false;
  var dataOffset = 0;

  while (bfinal === false) {
    // Start of first block
    bfinal = walker.readUint8() === 1;
    var blockLength = walker.readUint16(true); // console.log(`zlib block: ${blockLength} bytes, final: ${bfinal}`)
    // skip nlen

    walker.skip(2); // data might change during this time, so we recalc the adler

    walker.startAdler();
    dataCallback(walker.array, walker.offset, dataOffset, blockLength);
    walker.offset += blockLength;
    dataOffset += blockLength;
    walker.pauseAdler();
  }

  walker.writeAdler();
}

/***/ }),
/* 6 */
/***/ (function(module, exports) {

/******/ (function(modules) { // webpackBootstrap
/******/ 	// The module cache
/******/ 	var installedModules = {};
/******/
/******/ 	// The require function
/******/ 	function __webpack_require__(moduleId) {
/******/
/******/ 		// Check if module is in cache
/******/ 		if(installedModules[moduleId]) {
/******/ 			return installedModules[moduleId].exports;
/******/ 		}
/******/ 		// Create a new module (and put it into the cache)
/******/ 		var module = installedModules[moduleId] = {
/******/ 			i: moduleId,
/******/ 			l: false,
/******/ 			exports: {}
/******/ 		};
/******/
/******/ 		// Execute the module function
/******/ 		modules[moduleId].call(module.exports, module, module.exports, __webpack_require__);
/******/
/******/ 		// Flag the module as loaded
/******/ 		module.l = true;
/******/
/******/ 		// Return the exports of the module
/******/ 		return module.exports;
/******/ 	}
/******/
/******/
/******/ 	// expose the modules object (__webpack_modules__)
/******/ 	__webpack_require__.m = modules;
/******/
/******/ 	// expose the module cache
/******/ 	__webpack_require__.c = installedModules;
/******/
/******/ 	// identity function for calling harmony imports with the correct context
/******/ 	__webpack_require__.i = function(value) { return value; };
/******/
/******/ 	// define getter function for harmony exports
/******/ 	__webpack_require__.d = function(exports, name, getter) {
/******/ 		if(!__webpack_require__.o(exports, name)) {
/******/ 			Object.defineProperty(exports, name, {
/******/ 				configurable: false,
/******/ 				enumerable: true,
/******/ 				get: getter
/******/ 			});
/******/ 		}
/******/ 	};
/******/
/******/ 	// getDefaultExport function for compatibility with non-harmony modules
/******/ 	__webpack_require__.n = function(module) {
/******/ 		var getter = module && module.__esModule ?
/******/ 			function getDefault() { return module['default']; } :
/******/ 			function getModuleExports() { return module; };
/******/ 		__webpack_require__.d(getter, 'a', getter);
/******/ 		return getter;
/******/ 	};
/******/
/******/ 	// Object.prototype.hasOwnProperty.call
/******/ 	__webpack_require__.o = function(object, property) { return Object.prototype.hasOwnProperty.call(object, property); };
/******/
/******/ 	// __webpack_public_path__
/******/ 	__webpack_require__.p = "";
/******/
/******/ 	// Load entry module and return exports
/******/ 	return __webpack_require__(__webpack_require__.s = 0);
/******/ })
/************************************************************************/
/******/ ([
/* 0 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";

Object.defineProperty(exports, "__esModule", { value: true });
var png_pong_1 = __webpack_require__(!(function webpackMissingModule() { var e = new Error("Cannot find module \"./png-pong\""); e.code = 'MODULE_NOT_FOUND'; throw e; }()));
exports.PngPong = png_pong_1.PngPong;
var writer_1 = __webpack_require__(!(function webpackMissingModule() { var e = new Error("Cannot find module \"./writer\""); e.code = 'MODULE_NOT_FOUND'; throw e; }()));
exports.createFromRGBAArray = writer_1.createFromRGBAArray;
exports.createWithMetadata = writer_1.createWithMetadata;
var arraybuffer_walker_1 = __webpack_require__(!(function webpackMissingModule() { var e = new Error("Cannot find module \"./util/arraybuffer-walker\""); e.code = 'MODULE_NOT_FOUND'; throw e; }()));
exports.ArrayBufferWalker = arraybuffer_walker_1.ArrayBufferWalker;
var palette_1 = __webpack_require__(!(function webpackMissingModule() { var e = new Error("Cannot find module \"./chunks/palette\""); e.code = 'MODULE_NOT_FOUND'; throw e; }()));
exports.Palette = palette_1.Palette;
var shape_1 = __webpack_require__(!(function webpackMissingModule() { var e = new Error("Cannot find module \"./transformers/shape\""); e.code = 'MODULE_NOT_FOUND'; throw e; }()));
exports.PngPongShapeTransformer = shape_1.PngPongShapeTransformer;
var copy_image_1 = __webpack_require__(!(function webpackMissingModule() { var e = new Error("Cannot find module \"./transformers/copy-image\""); e.code = 'MODULE_NOT_FOUND'; throw e; }()));
exports.PngPongImageCopyTransformer = copy_image_1.PngPongImageCopyTransformer;


/***/ })
/******/ ]);

/***/ }),
/* 7 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.calculateIDATLength = calculateIDATLength;
exports.writeIDAT = writeIDAT;
exports.writeIDATConstant = writeIDATConstant;

var _zlib = __webpack_require__(5);

/**
 * Write an IDAT chunk all at once. Typically used when creating a new blank image.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 * @param {Uint8ClampedArray} data 
 * @param {number} width 
 */
function writeIDAT(walker, data, width) {
  // We need to account for a row filter pixel in our chunk length
  var height = data.length / width; // Zlibbed data will take up more space than the raw data

  walker.writeUint32((0, _zlib.calculateZlibbedLength)(data.length + height));
  walker.startCRC();
  walker.writeString("IDAT");
  var zlibWriter = new _zlib.ZlibWriter(walker, data.length + height);
  var currentX = 0; // Write our row filter byte

  zlibWriter.writeUint8(0);

  for (var i = 0; i < data.length; i++) {
    if (currentX === width) {
      currentX = 0; // Write our row filter byte

      zlibWriter.writeUint8(0);
    }

    zlibWriter.writeUint8(data[i]);
    currentX++;
  }

  zlibWriter.end();
  walker.writeCRC();
}
/**
 * Write an IDAT chunk without wasting memory on a source ArrayBuffer - if we want it all to be one
 * palette index.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 * @param {number} value - The palette index we want all the pixels to be
 * @param {number} width 
 * @param {number} height 
 */


function writeIDATConstant(walker, value, width, height) {
  var overallSize = (width + 1) * height; // +1 for row filter byte

  walker.writeUint32((0, _zlib.calculateZlibbedLength)(overallSize));
  walker.startCRC();
  walker.writeString("IDAT");
  var zlibWriter = new _zlib.ZlibWriter(walker, overallSize);
  var currentX = 0; // Write our row filter byte

  zlibWriter.writeUint8(0);

  for (var i = 0; i < width * height; i++) {
    if (currentX === width) {
      currentX = 0; // Write our row filter byte

      zlibWriter.writeUint8(0);
    }

    zlibWriter.writeUint8(value);
    currentX++;
  }

  zlibWriter.end();
  walker.writeCRC();
}
/**
 * Calculate the length of an IDAT chunk. Because it uses both ZLib chunking
 * and a row filter byte at the start of each row, it isn't as simple as
 * width * height.
 * 
 * @export
 * @param {number} width 
 * @param {number} height 
 * @returns 
 */


function calculateIDATLength(width, height) {
  // +1 for row filter byte at the start of each row
  var bytes = (width + 1) * height;
  return 4 // Chunk length
  + 4 // Identifier
  + (0, _zlib.calculateZlibbedLength)(bytes) + 4; // CRC
}

/***/ }),
/* 8 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.length = void 0;
exports.writeIEND = writeIEND;
var identifier = "IEND";
/**
 * There is no actual content in an IEND chunk, just the identifier
 * and CRC.
 * 
 * @export
 * @param {ArrayBufferWalker} walker 
 */

function writeIEND(walker) {
  walker.writeUint32(0);
  walker.startCRC();
  walker.writeString(identifier);
  walker.writeCRC();
}

var length = identifier.length // identifier
+ 4 // CRC
+ 4; // length

exports.length = length;

/***/ }),
/* 9 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.PngPong = void 0;

var _arraybufferWalker = __webpack_require__(2);

var _preHeader = __webpack_require__(4);

var _ihdr = __webpack_require__(0);

var _palette = __webpack_require__(1);

var _zlib = __webpack_require__(5);

function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } }

function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

function _defineProperty(obj, key, value) { if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }

var EventPayloads = /*#__PURE__*/_createClass(function EventPayloads() {
  _classCallCheck(this, EventPayloads);

  _defineProperty(this, "header", void 0);

  _defineProperty(this, "palette", void 0);

  _defineProperty(this, "data", void 0);
});

;

/**
 * The core class for any image manipulation. Create an instance of this class
 * with the ArrayBuffer of your original PNG image, then apply your transforms
 * to it. Then execute PngPng.run() to apply those transforms.
 * 
 * @export
 * @class PngPong
 */
var PngPong = /*#__PURE__*/function () {
  /**
   * Creates an instance of PngPong.
   * @param {ArrayBuffer} source: The ArrayBuffer you want to apply
   * transforms to.
   * 
   * @memberof PngPong
   */
  function PngPong(source) {
    _classCallCheck(this, PngPong);

    _defineProperty(this, "walker", void 0);

    _defineProperty(this, "headerListeners", []);

    _defineProperty(this, "paletteListeners", []);

    _defineProperty(this, "dataListeners", []);

    _defineProperty(this, "width", void 0);

    this.walker = new _arraybufferWalker.ArrayBufferWalker(source);
  }

  _createClass(PngPong, [{
    key: "readData",
    value: function readData(dataLength) {
      var _this = this;

      // Need to include the chunk identifier in the CRC. Need a better
      // way to do this.
      this.walker.skip(-4);
      this.walker.startCRC();
      this.walker.skip(4);
      var rowFilterBytesSkipped = 1; // Our PNG rows can be split across chunks, so we need to track
      // overall data length

      var dataReadSoFar = 0;
      (0, _zlib.readZlib)(this.walker, function (arr, readOffset, dataOffset, length) {
        // The PNG stream has a row filter flag at the start of every row
        // which we want to disregard and not send to any listeners. So
        // we split up the data as we receive it into chunks, around that
        // flag.
        // ignore our first row flag
        var blockReadOffset = 0;

        var _loop = function _loop() {
          // In order to match rows across blocks and also ignore row flags,
          // we need to keep track of our current coordinate.
          var xAtThisPoint = dataReadSoFar % _this.width;

          if (blockReadOffset === 0 && xAtThisPoint === 0) {
            // If we're starting a new block AND it's the start of a row,
            // we need to skip our row filter byte
            blockReadOffset++;
          }

          var yAtThisPoint = (dataReadSoFar - xAtThisPoint) / _this.width; // If we have an entire image row we can read, do that. If we have a partial
          // row, do that. If we have the end of a block, do that.

          var amountToRead = Math.min(_this.width - xAtThisPoint, length - blockReadOffset);

          _this.dataListeners.forEach(function (d) {
            return d(_this.walker.array, readOffset + blockReadOffset, xAtThisPoint, yAtThisPoint, amountToRead);
          }); // update our offsets to match the pixel amounts we just read


          dataReadSoFar += amountToRead;
          blockReadOffset += amountToRead; // now ALSO update our block offset to skip the next row filter byte

          blockReadOffset++;
        };

        while (blockReadOffset < length) {
          _loop();
        }
      });
      this.walker.writeCRC(); // this.walker.skip(4)

      this.readChunk();
    }
  }, {
    key: "readChunk",
    value: function readChunk() {
      var length = this.walker.readUint32();
      var identifier = this.walker.readString(4);

      if (identifier === "IHDR") {
        var hdr = (0, _ihdr.readIHDR)(this.walker, length);
        this.width = hdr.width;
        this.headerListeners.forEach(function (l) {
          l(hdr);
        });
        this.readChunk();
      } else if (identifier === "PLTE") {
        var plte = (0, _palette.readPalette)(this.walker, length);
        this.paletteListeners.forEach(function (l) {
          return l(plte);
        });
        plte.writeCRCs();
        this.readChunk();
      } else if (identifier === "IDAT") {
        this.readData(length);
      } else if (identifier === "IEND") {// we're done
      } else {
        throw new Error("Did not recognise ".concat(length, " byte chunk: ").concat(identifier));
      }
    }
    /**
     * Apply the transforms you've created to the original ArrayBuffer.
     * 
     * @memberof PngPong
     */

  }, {
    key: "run",
    value: function run() {
      (0, _preHeader.checkPreheader)(this.walker);
      this.readChunk();
    }
    /**
     * Add a callback to be run when the IHDR chunk of the PNG file has been
     * successfully read. You cannot edit the contents of the IHDR, but can
     * read values out of it.
     * 
     * @param {Callback<IHDROptions>} listener 
     * 
     * @memberof PngPong
     */

  }, {
    key: "onHeader",
    value: function onHeader(listener) {
      this.headerListeners.push(listener);
    }
    /**
     * Add a callback when the image palette has been processed. During this
     * callback you are able to add colors to the palette. If you save the
     * palette variable outside of the callback, you can also use it to later
     * get the index of palette colors while processing data.
     * 
     * @param {Callback<Palette>} listener 
     * 
     * @memberof PngPong
     */

  }, {
    key: "onPalette",
    value: function onPalette(listener) {
      this.paletteListeners.push(listener);
    }
    /**
     * Add a callback that will be run multiple times as PngPong runs through
     * the image data.
     * 
     * @param {DataCallback} listener 
     * 
     * @memberof PngPong
     */

  }, {
    key: "onData",
    value: function onData(listener) {
      this.dataListeners.push(listener);
    }
  }]);

  return PngPong;
}();

exports.PngPong = PngPong;

/***/ }),
/* 10 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.RGBAtoPalettedArray = RGBAtoPalettedArray;

function findRGBA(rgba, offset, rgbPalette, alphaPalette) {
  for (var i = 3; i < rgbPalette.length; i = i + 3) {
    // Because we already shortcut when looking for rgba(0,0,0,0) we know that if we
    // run into it here it's the end of our current palette. So we can end the loop early.
    if (rgbPalette[i] === 0 && rgbPalette[i + 1] === 0 && rgbPalette[i + 2] === 0 && alphaPalette[i / 3] === 0) {
      return -1;
    }

    if (rgbPalette[i] === rgba[offset] && rgbPalette[i + 1] === rgba[offset + 1] && rgbPalette[i + 2] === rgba[offset + 2] && rgba[offset + 3] === alphaPalette[i / 3]) {
      return i / 3;
    }
  }

  return -1;
}

function findOrAddColor(rgba, offset, rgbPalette, alphaPalette) {
  // ISSUE: when reading back, we don't actually know how much of the array has been used. So if we'd 
  // specified rgba(0,0,0,0) as a colour we'd assume it is an empty byte, and use it. To cover this,
  // we are going to reserve the first entry in the array as rgba(0,0,0,0). Hopefully we don't end up
  // with images that need all 255 palette entries...
  if (rgba[offset] === 0 && rgba[offset + 1] === 0 && rgba[offset + 2] === 0 && rgba[offset + 3] === 0) {
    return 0;
  } // Technically we're passing a 4-length array here, but the function ignores it


  var rgbIndex = findRGBA(rgba, offset, rgbPalette, alphaPalette);

  if (rgbIndex > -1) {
    return rgbIndex;
  } else {
    // The colour is not yet in the palette. So we go through the palette array until we find
    // one (that isn't at index zero) that matches rgba(0,0,0,0). 
    var vacantIndex = 3;

    while (rgbPalette[vacantIndex] !== 0 || rgbPalette[vacantIndex + 1] !== 0 || rgbPalette[vacantIndex + 2] !== 0 || alphaPalette[vacantIndex / 3] !== 0) {
      vacantIndex += 3;

      if (vacantIndex > 255) {
        throw new Error("No room left in the palette");
      }
    }

    rgbPalette[vacantIndex] = rgba[offset];
    rgbPalette[vacantIndex + 1] = rgba[offset + 1];
    rgbPalette[vacantIndex + 2] = rgba[offset + 2];
    alphaPalette[vacantIndex / 3] = rgba[offset + 3]; // The actual index we use is that of the red value.

    return vacantIndex / 3;
  }
}

function RGBAtoPalettedArray(rgba, extraPaletteSpaces) {
  if (rgba.byteLength % 4 !== 0) {
    throw new Error("This is not divisible by 4, can't be an RGBA array");
  }

  var data = new Uint8ClampedArray(rgba.byteLength / 4);
  var rgbPalette = new Uint8ClampedArray(255 * 3);
  var alphaPalette = new Uint8ClampedArray(255);
  var maxColor = 0;

  for (var i = 0; i < rgba.length; i = i + 4) {
    var color = findOrAddColor(rgba, i, rgbPalette, alphaPalette);
    maxColor = Math.max(maxColor, color);
    data[i / 4] = color;
  } // maxColor is the index, we want the length, so bump it up by one


  maxColor++;
  return {
    data: data,
    rgbPalette: rgbPalette.slice(0, maxColor * 3 + extraPaletteSpaces * 3),
    alphaPalette: alphaPalette.slice(0, maxColor + extraPaletteSpaces)
  };
}

/***/ }),
/* 11 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.PngPongImageCopyTransformer = void 0;

var _ = __webpack_require__(3);

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } }

function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }

function _defineProperty(obj, key, value) { if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }

function alphaBlend(color1, color2, alpha) {
  var alphaMultiply = alpha / 255;
  var redDiff = color1[0] - color2[0];
  var greenDiff = color1[1] - color2[1];
  var blueDiff = color1[2] - color2[2];
  var newColor = [color1[0] - Math.round(redDiff * alphaMultiply), color1[1] - Math.round(greenDiff * alphaMultiply), color1[2] - Math.round(blueDiff * alphaMultiply)];
  return newColor;
}
/**
 * A transformer to copy one or more sections of an image onto another.
 * 
 * @export
 * @class PngPongImageCopyTransformer
 */


var PngPongImageCopyTransformer = /*#__PURE__*/function () {
  /**
   * Creates an instance of PngPongImageCopyTransformer.
   * @param {ArrayBuffer} sourceImage - the source PNG ArrayBuffer to read from. Must be
   * a PngPong suitable PNG.
   * @param {PngPong} targetTransformer - the transformer to add this image to.
   * 
   * @memberof PngPongImageCopyTransformer
   */
  function PngPongImageCopyTransformer(sourceImage, targetTransformer) {
    _classCallCheck(this, PngPongImageCopyTransformer);

    _defineProperty(this, "operations", []);

    this.targetTransformer.onPalette(this.onPalette.bind(this));
    this.targetTransformer.onData(this.onData.bind(this));
  }
  /**
   * Add a copy operation to the transformer. Must be done before running PngPong.run().
   * 
   * @param {number} sourceX 
   * @param {number} sourceY 
   * @param {number} sourceWidth 
   * @param {number} sourceHeight 
   * @param {number} targetX 
   * @param {number} targetY 
   * @param {ColorMaskOptions} [mask] - Optional argument to ignore the RGB value of the source image
   * and instead apply a color mask.
   * 
   * @memberof PngPongImageCopyTransformer
   */


  _createClass(PngPongImageCopyTransformer, [{
    key: "copy",
    value: function copy(sourceX, sourceY, sourceWidth, sourceHeight, targetX, targetY, mask) {
      var pixelsRequired = sourceWidth * sourceHeight;
      var pixels = new Uint8Array(pixelsRequired);
      this.operations.push({
        sourceX: sourceX,
        sourceY: sourceY,
        sourceWidth: sourceWidth,
        sourceHeight: sourceHeight,
        targetX: targetX,
        targetY: targetY,
        pixels: pixels,
        mask: mask
      });
    }
  }, {
    key: "onPalette",
    value: function onPalette(targetPalette) {
      var _this = this;

      // We need to grab our source image and add the new colors to the palette. At the same time
      // we record the new data arrays, to insert into the data later.
      var sourceTransformer = new _.PngPong(this.sourceImage); // grab the palette to do lookups

      var sourcePalette;
      sourceTransformer.onPalette(function (p) {
        return sourcePalette = p;
      });
      sourceTransformer.onData(function (array, readOffset, x, y, length) {
        var _loop = function _loop(i) {
          _this.operations.forEach(function (operation) {
            if (y < operation.sourceY || y >= operation.sourceY + operation.sourceHeight || x < operation.sourceX || x >= operation.sourceX + operation.sourceWidth) {
              return;
            }

            var relativeX = x - operation.sourceX;
            var relativeY = y - operation.sourceY;
            var sourcePixel = sourcePalette.getColorAtIndex(array[readOffset + i]);

            if (operation.mask) {
              var maskColor = alphaBlend(operation.mask.backgroundColor, operation.mask.maskColor, sourcePixel[3]);
              sourcePixel[0] = maskColor[0];
              sourcePixel[1] = maskColor[1];
              sourcePixel[2] = maskColor[2];
            }

            var targetPaletteIndex = targetPalette.getColorIndex(sourcePixel);

            if (targetPaletteIndex === -1) {
              targetPaletteIndex = targetPalette.addColor(sourcePixel);
            }

            var arrayIndex = relativeY * operation.sourceWidth + relativeX;
            operation.pixels[arrayIndex] = targetPaletteIndex;
          });

          x++;
        };

        for (var i = 0; i < length; i++) {
          _loop(i);
        }
      });
      sourceTransformer.run();
    }
  }, {
    key: "onData",
    value: function onData(array, readOffset, x, y, length) {
      var _this2 = this;

      var _loop2 = function _loop2(i) {
        _this2.operations.forEach(function (operation) {
          if (y < operation.targetY || y >= operation.targetY + operation.sourceHeight || x < operation.targetX || x >= operation.targetX + operation.sourceWidth) {
            return;
          }

          var relativeX = x - operation.targetX;
          var relativeY = y - operation.targetY;
          var sourcePixel = operation.pixels[relativeY * operation.sourceWidth + relativeX];
          array[readOffset + i] = sourcePixel;
        });

        x++;
      };

      for (var i = 0; i < length; i++) {
        _loop2(i);
      }
    }
  }]);

  return PngPongImageCopyTransformer;
}();

exports.PngPongImageCopyTransformer = PngPongImageCopyTransformer;

/***/ }),
/* 12 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.PngPongShapeTransformer = void 0;

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } }

function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }

function _defineProperty(obj, key, value) { if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }

/**
 * A transformer to draw basic shapes onto an image. Currently only draws rectangles.
 * 
 * @export
 * @class PngPongShapeTransformer
 */
var PngPongShapeTransformer = /*#__PURE__*/function () {
  /**
   * Creates an instance of PngPongShapeTransformer.
   * @param {PngPong} baseTransformer - the transformer you want to draw onto.
   * 
   * @memberof PngPongShapeTransformer
   */
  function PngPongShapeTransformer(baseTransformer) {
    var _this = this;

    _classCallCheck(this, PngPongShapeTransformer);

    _defineProperty(this, "operations", []);

    _defineProperty(this, "operationPaletteIndexes", []);

    _defineProperty(this, "imageWidth", void 0);

    baseTransformer.onHeader(function (h) {
      _this.imageWidth = h.width;
    });
    baseTransformer.onPalette(this.onPalette.bind(this));
    baseTransformer.onData(this.onData.bind(this));
  }

  _createClass(PngPongShapeTransformer, [{
    key: "onPalette",
    value: function onPalette(palette) {
      this.operationPaletteIndexes = this.operations.map(function (o) {
        var idx = palette.getColorIndex(o.color);

        if (idx === -1) {
          idx = palette.addColor(o.color);
        }

        return idx;
      });
    }
  }, {
    key: "onData",
    value: function onData(array, readOffset, x, y, length) {
      for (var idx = 0; idx < this.operations.length; idx++) {
        var o = this.operations[idx];

        if (y < o.y1 || y >= o.y2) {
          continue;
        }

        for (var i = Math.max(x, o.x1); i < Math.min(o.x2, x + length); i++) {
          array[readOffset - x + i] = this.operationPaletteIndexes[idx];
        }
      }
    }
    /**
     * Add a rectangle to the list of draw operations. Must use this before running PngPong.run()
     * 
     * @param {number} x 
     * @param {number} y 
     * @param {number} width 
     * @param {number} height 
     * @param {RGB} color 
     * 
     * @memberof PngPongShapeTransformer
     */

  }, {
    key: "drawRect",
    value: function drawRect(x, y, width, height, color) {
      var x2 = x + width;
      var y2 = y + height;
      this.operations.push({
        x1: x,
        x2: x2,
        y1: y,
        y2: y2,
        color: color
      });
    }
  }]);

  return PngPongShapeTransformer;
}();

exports.PngPongShapeTransformer = PngPongShapeTransformer;

/***/ }),
/* 13 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.adler32_buf = adler32_buf;

/**
 * Calculate the ADLER32 checksum of a section of a buffer. Code largely taken from:
 * https://github.com/SheetJS/js-adler32
 * 
 * @export
 * @param {(Uint8Array | Uint8ClampedArray)} buf 
 * @param {number} offset 
 * @param {number} length 
 * @param {number} [seed] 
 * @returns 
 */
function adler32_buf(buf, offset, length, seed) {
  var a = 1,
      b = 0,
      L = offset + length,
      M = 0;

  if (typeof seed === 'number') {
    a = seed & 0xFFFF;
    b = seed >>> 16 & 0xFFFF;
  }

  for (var i = offset; i < L;) {
    M = Math.min(L - i, 3850) + i;

    for (; i < M; i++) {
      a += buf[i] & 0xFF;
      b += a;
    }

    a = 15 * (a >>> 16) + (a & 65535);
    b = 15 * (b >>> 16) + (b & 65535);
  }

  return b % 65521 << 16 | a % 65521;
}

/***/ }),
/* 14 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.crc32 = crc32;
// Generated by `./pycrc.py --algorithm=table-driven --model=crc-32 --generate=c`
var TABLE = new Int32Array([0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d]);
/**
 * Calculate the CRC value of a selected slice of an ArrayBuffer. Code from:
 * https://github.com/alexgorbatchev/node-crc/blob/master/src/crc32.js
 * 
 * @export
 * @param {(Uint8Array | Uint8ClampedArray)} buf 
 * @param {number} offset 
 * @param {number} length 
 * @param {number} [previous] 
 * @returns {number} 
 */

function crc32(buf, offset, length, previous) {
  var crc = previous === 0 ? 0 : ~~previous ^ -1;

  for (var index = offset; index < offset + length; index++) {
    var _byte = buf[index];
    crc = TABLE[(crc ^ _byte) & 0xff] ^ crc >>> 8;
  }

  return crc ^ -1;
}

;

/***/ }),
/* 15 */
/***/ (function(module, exports, __webpack_require__) {

"use strict";


Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.createFromRGBAArray = createFromRGBAArray;
exports.createWithMetadata = createWithMetadata;

var _preHeader = __webpack_require__(4);

var _ihdr = __webpack_require__(0);

var _palette = __webpack_require__(1);

var _iend = __webpack_require__(8);

var _idat = __webpack_require__(7);

var _arraybufferWalker = __webpack_require__(2);

var _rgbaToPaletteArray = __webpack_require__(10);

function calculateBufferLength(width, height, numColors) {
  // Before we write anything we need to work out the size of ArrayBuffer
  // we need. This is a combination of a whole load of factors, so we
  // separate out the logic into different chunks.
  return _preHeader.length + _ihdr.IHDRLength + (0, _palette.calculatePaletteLength)(numColors) + (0, _idat.calculateIDATLength)(width, height) + _iend.length;
}
/**
 * Create a PngPong-suitable PNG ArrayBuffer from an existing RGBA array. Combine
 * this with PNGJS to transform an existing PNG image into something PngPong can use.
 * 
 * @export
 * @param {number} width 
 * @param {number} height 
 * @param {Uint8ClampedArray} rgbaData 
 * @param {number} extraPaletteSpaces - How many extra palette entries should we make available for new colors, after we've added the colors from the existing array?
 * @returns 
 */


function createFromRGBAArray(width, height, rgbaData) {
  var extraPaletteSpaces = arguments.length > 3 && arguments[3] !== undefined ? arguments[3] : 0;

  var _RGBAtoPalettedArray = (0, _rgbaToPaletteArray.RGBAtoPalettedArray)(rgbaData, extraPaletteSpaces),
      rgbPalette = _RGBAtoPalettedArray.rgbPalette,
      alphaPalette = _RGBAtoPalettedArray.alphaPalette,
      data = _RGBAtoPalettedArray.data;

  var arrayBufferLength = calculateBufferLength(width, height, alphaPalette.length + extraPaletteSpaces);
  var buffer = new ArrayBuffer(arrayBufferLength);
  var walker = new _arraybufferWalker.ArrayBufferWalker(buffer);
  (0, _preHeader.writePreheader)(walker);
  (0, _ihdr.writeIHDR)(walker, {
    width: width,
    height: height,
    colorType: _ihdr.PNGColorType.Palette,
    bitDepth: 8,
    compressionMethod: 0,
    filter: 0,
    "interface": 0
  });
  (0, _palette.writePalette)(walker, rgbPalette, alphaPalette);
  (0, _idat.writeIDAT)(walker, data, width);
  (0, _iend.writeIEND)(walker);
  return buffer;
}
/**
 * Create a PngPong-suitable ArrayBuffer based on the arguments provided.
 * 
 * @export
 * @param {number} width 
 * @param {number} height 
 * @param {number} paletteSize - Must be at least 1, and at least 2 if specifying a background color.
 * @param {RGB} [backgroundColor] 
 * @returns 
 */


function createWithMetadata(width, height, paletteSize, backgroundColor) {
  var length = calculateBufferLength(width, height, paletteSize);
  var buffer = new ArrayBuffer(length);
  var walker = new _arraybufferWalker.ArrayBufferWalker(buffer);
  (0, _preHeader.writePreheader)(walker);
  (0, _ihdr.writeIHDR)(walker, {
    width: width,
    height: height,
    colorType: _ihdr.PNGColorType.Palette,
    bitDepth: 8,
    compressionMethod: 0,
    filter: 0,
    "interface": 0
  });
  var rgbColors = new Uint8ClampedArray(paletteSize * 3);
  var alphaValues = new Uint8ClampedArray(paletteSize);

  if (backgroundColor) {
    rgbColors[3] = backgroundColor[0];
    rgbColors[4] = backgroundColor[1];
    rgbColors[5] = backgroundColor[2];
    alphaValues[1] = 255;
  }

  (0, _palette.writePalette)(walker, rgbColors, alphaValues);

  if (backgroundColor) {
    // The background color will be palette entry #1, as RGBA(0,0,0,0) is
    // always entry #0
    (0, _idat.writeIDATConstant)(walker, 1, width, height);
  } else {
    (0, _idat.writeIDATConstant)(walker, 0, width, height);
  }

  (0, _iend.writeIEND)(walker);
  return buffer;
}

/***/ }),
/* 16 */
/***/ (function(module, exports, __webpack_require__) {

__webpack_require__(3);
module.exports = __webpack_require__(6);


/***/ })
/******/ ]);
});