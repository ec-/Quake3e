// Launcher program for web browser and .wasm builds
let isStreaming = false

let ENV = {}
ENV.ENV = ENV


function initEnvironment(ENGINE) {
	if(!ENV.table) {
		const importTable = new WebAssembly.Table({ 
			initial: 2000, 
			element: 'anyfunc', 
			maximum: 10000 
		})
		ENV.table = ENV.__indirect_function_table = importTable
	}
	if(!ENV.memory) {
		ENV.memory = new WebAssembly.Memory({ 
			initial: 2100, 
			maximum: 16000,
			/* 'shared': true */
		})
	}

	// weird stuff WebAssembly requires
	ENV.env = ENV.wasi_snapshot_preview1 = ENV
	ENV.imports = ENV

	// set window module because of LEGACY SDL audio
	ENGINE.Module = ENV

	Object.assign(ENV, ENGINE)
	let startKeys = Object.keys(ENGINE)
	let startValues = Object.values(ENGINE)
	updateGlobalFunctions(ENGINE) // ASSIGN STD, FS, GL, ETC
	for(let i = 0; i < startKeys.length; i++) {
		updateGlobalFunctions(startValues[i])
	}

	// THIS IS ALSO KIND OF A TEST THAT WINDOW.INIT WORKS
	NET.cacheBuster = Date.now() // for comparing times
	return ENV
}


// BECAUSE IT'S FUCKING PRETTIER, OKAY?
function updateGlobalFunctions(GLOBAL) {

	// assign everything to env because this __attribute(import) BS don't work
	let startKeys = Object.keys(GLOBAL)
	let startValues = Object.values(GLOBAL)
	if(typeof window != 'undefined') {
		for(let i = 0; i < startKeys.length; i++) {
			ENV[startKeys[i]] =
			window[startKeys[i]] = startValues[i] //.apply(ENV.exports)
		}
		Object.assign(ENV, GLOBAL)
	} else if (typeof global != 'undefined') {
		for(let i = 0; i < startKeys.length; i++) {
			ENV[startKeys[i]] =
			global[startKeys[i]] = startValues[i] //.apply(ENV.exports)
		}
		Object.assign(ENV, GLOBAL)
	}

}

// write it non-async just in case wasm is support but not es6?
function initWasm(bytes, env) {
	if(isStreaming) {
		return WebAssembly.instantiateStreaming(bytes, env)
	} else {
		return WebAssembly.instantiate(bytes, env)
	}
}

function updateEnvironment(program, ENV) {
	// share the game with window for hackers
	if(!program) {
		throw new Error("no program!")
	}
	let previousErr
	if(typeof stderr != 'undefined') {
		previousErr = FS.pointers[HEAPU32[stderr>>2]]
	} else {
		previousErr = null // used below
	}

	let previousOut
	if(typeof stdout != 'undefined') {
		previousOut = FS.pointers[HEAPU32[stdout>>2]]
	} else {
		previousOut = null
	}
	// THIS IS JUST INTENDED TO MEET MULTIPLE EXPECTATIONS FROM A PROGRAMMER
	ENV.program = program || {}
	ENV.instance = ENV.program.instance || {}
	ENV.exports = ENV.instance.exports || {}
	//ENV.memory = ENV.exports.memory
	updateGlobalFunctions(ENV.exports)
	updateGlobalBufferAndViews()
	// THIS IS ALSO STILL KIND OF A TEST THAT WINDOW INIT WORKS
	//   STDLIB WOULD ALLOC AS THE LIBRARY LOADS, 
	//   THIS IS KIND OF LIKE SHELL SPACE ALLOC
	// reserve some memory at the beginning for passing shit back and forth with JS
	//   not to use a complex HEAP, just loop around on bytes[b % 128] and if 
	//   something isn't cleared out, crash
	// store some strings and crap
	if(ENV.exports.stdout) {
		FS.pointers[HEAPU32[stdout>>2]] = [
			0,
			'rw',
			// this is weird, multiple WASMs in same scope, globals are messed up
			//   because libc loads multiple times, there are multiple stderr
			//   the OS only loads libc once and all kernels and programms call
			//   into the system function, so it doesn't have to load a whole
			//   space just for static string operations.
			previousOut ? previousOut[2] : { 
				timestamp: new Date(),
				mode: FS_FILE,
				contents: new Uint8Array()
			},
			1,
			HEAPU32[stdout>>2]
		]
	}
	if(ENV.exports.stderr) {
		FS.pointers[HEAPU32[stderr>>2]] = [
			0,
			'rw',
			previousErr ? previousErr[2] : {
				timestamp: new Date(),
      	mode: FS_FILE,
      	contents: new Uint8Array()
			},
			2,
			HEAPU32[stderr>>2]
		]
	}
	return program
}


function initEngine(program) {
	// ALL THE VARIABLES WE NEED SHOULD BE ASSIGNED TO GLOBAL BY NOW
	if(!program) {
		throw new Error("no program!")
	}
	try {
		SYS.exited = false
		if(typeof window['Z_Malloc'] == 'undefined') {
			window.Z_Malloc = window['Z_MallocDebug']
		}
		// Startup args is expecting a char **
		let startArgs = getQueryCommands()
		_start(startArgs.length, stringsToMemory(startArgs))
		// should have Cvar system by now
		INPUT.fpsUnfocused = Cvar_VariableIntegerValue(stringToAddress('com_maxfpsUnfocused'));
		INPUT.fps = Cvar_VariableIntegerValue(stringToAddress('com_maxfps'))
		// this might help prevent this thing that krunker.io does where it lags when it first starts up
		SYS.frameInterval = setInterval(Sys_Frame, 
			1000 / (HEAP32[gw_active >> 2] ? INPUT.fps : INPUT.fpsUnfocused));
	} catch (e) {
		console.log(e)
		Sys_Exit(1)
		throw e
	}
}


async function initBrowser() {
	const viewport = document.getElementById('viewport-frame')
	GL.canvas = document.getElementsByTagName('canvas')[0]
	const ENGINE = initEnvironment({
		SYS: SYS,
		GL: GL,
		EMGL: EMGL,
		INPUT: INPUT,
	})
	ENGINE.canvas = GL.canvas

	// slight delay to let window settle from big wasm data
	function initPreload() {
		return new Promise(function (resolve) {
			setTimeout(function () {
				resolve(FS.virtual['quake3e.wasm'].contents)
			}, 200)
		})
	}

	// no delay on remote loads
	function initStreaming() {
		return fetch('./quake3e.wasm?time=' + NET.cacheBuster)
			.catch(function (e) { console.error(e) })
			.then(function (response) {
				if(response && response.status == 200) {
					if(isStreaming) {
						return response
					} else {
						return response.arrayBuffer()
					}
				}
			})
	}

	isStreaming = false
	// might as well start this early, transfer 
	//    IndexedDB from disk/memory to application memory
	let bytes
	if(typeof FS.virtual['quake3e.wasm'] != 'undefined') {
		bytes = await Promise.resolve(initPreload())
	} else {
		isStreaming = true
		bytes = await Promise.resolve(initStreaming())
	}
	let program = await initWasm(bytes, ENGINE)
	await updateEnvironment(program, ENGINE)
	await readAll()
  await readPreFS()
	initEngine(program)

}

if(typeof window != 'undefined') {
	// TODO: change when hot reloading works
	window.addEventListener('load', function () {
		if(typeof global != 'undefined') {
			const {
				MATHS,
				STD,
				DATE,
			} = require('./sys_std.js')
			const NET = require('./sys_net.js')
			const FS = require('./sys_fs.js')
			Object.assign(ENV, {
				MATH: MATHS,
				FS: FS,
				NET: NET,
				STD: STD,
				DATE: DATE,
			})
		
		} else {
			// in browser mode scripts are combined together in order
			Object.assign(ENV, {
				MATH: MATHS,
				FS: FS,
				NET: NET,
				STD: STD,
				DATE: DATE,
				SND: SND,
			})
		}
		let startKeys = Object.keys(ENV)
		let startValues = Object.values(ENV)
		updateGlobalFunctions(ENV) // ASSIGN STD, FS, GL, ETC
		for(let i = 0; i < startKeys.length; i++) {
			updateGlobalFunctions(startValues[i])
		}
		
		
		if(typeof window.initAce != 'undefined') {
			initAce()
		}

		if(typeof Module == 'undefined') {
			initBrowser()
		}
	}, false)

} else if (typeof module != 'undefined') {
	module.exports = {
		initEnvironment,
		initWasm,
		updateEnvironment,
	}

}



/*
if(!bytes) {
	if(GL.canvas) {
		GL.canvas.transferControlToOffscreen()
	}
	// try it in a service worker
	if((window.location.protocol == 'http:' 
		|| window.location.protocol == 'https:'
		|| window.location.protocol == 'chrome-extension:')
		&& 'serviceWorker' in navigator) {
		navigator.serviceWorker.register('quake3e.js', { scope: '/' })
			.then(function (registration) {
				if(typeof SYS != 'undefined') SYS.servicable = true
			})
			.catch(function (err) {
				if(typeof SYS != 'undefined') SYS.servicable = false
				console.log('Service Worker registration failed: ', err)
			})
	}
	document.body.classList.add('no-gl')
	throw new Error('Couldn\'t find wasm!')
}
*/
