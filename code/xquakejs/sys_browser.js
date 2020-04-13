var LibrarySys = {
	$SYS__deps: ['$SYSC', '$SDL'],
	$SYS: {
		index: [],
		fs_basepath: '/base',
		fs_game: 'baseq3-ccr',
		exited: false,
		timeBase: null,
		style: null,
		joysticks: [],
		shaderCallback: [],
		soundCallback: [],
		modelCallback: [],
		lasyIterval: 0,
		downloadLazy: [],
		downloadCount: 0,
		downloads: [],
		downloadSort: 0,
		// Lets make a list of supported mods, 'dirname-ccr' (-ccr means combined converted repacked)
		//   To the right is the description text, atomatically creates a placeholder.pk3dir with description.txt inside
		// We use a list here because Object keys have no guarantee of order
		mods: [
			['baseq3-ccr', 			'Quake III Arena'],
			['missionpack-ccr', '0 Choice: Team Arena'],
			['defrag-ccr',      '1 Choice: Defrag'],
			['baseq3r-ccr',     '2 Choice: Q3Rally'],
			['basemod-ccr', 		'3 Choice: Monkeys of Doom'],
			['generations-ccr', '4 Choice: Generations Arena'],
			['q3f2-ccr', 				'5 Choice: Q3 Fortress 2'],
			['cpma-ccr', 				'6 Choice: Challenge ProMode'],
			['q3ut4-ccr', 			'7 Choice: Urban Terror 4'],
			['freezetag-ccr', 	'8 Choice: Freeze Tag'],
			['corkscrew-ccr', 	'9 Choice: Corkscrew'],
			['baseoa-ccr', 			'Open Arena'],
			['bfpq3-ccr', 			'Bid For Power'],
			['excessive-ccr', 	'Excessive+'],
			['q3ut3-ccr', 			'Urban Terror 3'],
			['edawn-ccr', 			'eDawn'],
			['geoball-ccr', 		'Geoball'],
			['neverball-ccr', 	'Neverball'],
			['omissionpack-ccr','OpenArena Mission Pack'],
			['platformer-ccr', 	'Platformer'],
			['legoc-ccr', 			'Lego Carnage'],
			['osp-ccr', 				'Orange Smoothie Productions'],
			['quake2arena-ccr', 'Quake 2 Arena'],
			['smokin-ccr', 			'Smokin\' Guns'],
			['wfa-ccr', 				'Weapons Factory Arena'],
			['uberarena-ccr', 	'Uber Arena'],
			['demoq3-ccr', 			'Quake III Demo'],
			['mfdata-ccr', 			'Military Forces'],
			['conjunction-ccr', 'Dark Conjunction'],
			['chili-ccr', 			'Chili Quake XXL'],
			['hqq-ccr', 				'High Quality Quake'],
			['rocketarena-ccr', 'Coming Soon: Rocket Arena'],
			['gpp-ccr',         'Coming Soon: Tremulous'],
			['gppl-ccr',        'Coming Soon: Unvanquished'],
			['iortcw-ccr',      'Coming Soon: Return to Castle Wolfenstien'],
		],
		args: [
			'+set', 'fs_basepath', '/base',
			//'+set', 'sv_dlURL', '"http://localhost:8080/assets"',
			//'+set', 'cl_allowDownload', '1',
			'+set', 'fs_basegame', 'baseq3-ccr',
			'+set', 'fs_game', 'baseq3-ccr',
			//'+set', 'developer', '0',
			//'+set', 'fs_debug', '0',
			'+set', 'r_mode', '-1',
			'+set', 'r_customPixelAspect', '1',
			'+set', 'sv_pure', '0',
			//'+set', 'cg_simpleItems', '0',
			// these control the proxy server
			'+set', 'net_enabled', '1', // 1 for IPv4
			'+set', 'net_socksServer', '127.0.0.1',
			'+set', 'net_socksPort', '1081', // default 1080 but 1081 for websocket
			'+set', 'net_socksEnabled', '1',
			//'+set', 'com_hunkMegs', '256',
			//'+set', 'com_maxfps', '125',
			//'+set', 'com_maxfpsUnfocused', '10',
			//'+set', 'com_maxfpsMinimized', '10',
			// these settings were set by the emscripten build
			//'+set', 'r_normalMapping', '0',
			//'+set', 'r_specularMapping', '0',
			//'+set', 'r_deluxeMapping', '0',
			//'+set', 'r_hdr', '0',
			//'+set', 'r_lodbias', '0',
			//'+set', 'r_picmip', '4',
			//'+set', 'r_postProcess', '0',
			'+set', 'cg_drawfps', '1',
			//'+connect', 'proxy.quake.games:443',
			/*
			'+set', 'g_spVideos', '\\tier1\\1\\tier2\\2\\tier3\\3\\tier4\\4\\tier5\\5\\tier6\\6\\tier7\\7\\tier8\\8',
			'+set', 'g_spSkill', '5',
			'+set', 'g_spScores5', '\\l21\\5\\l14\\5\\l22\\5\\l25\\5\\l5\\5\\l3\\5\\l2\\5\\l20\\2\\l19\\1\\l1\\5\\l0\\5\\l24\\1',
			'+iamacheater',
			'+iamamonkey',
			'+exec', 'client.cfg',
			//	'+map', 'Q3DM17'
			*/
		],
		DoXHR: function (url, opts) {
			if (!url) {
				return opts.onload(new Error('Must provide a URL'))
			}

			var req = new XMLHttpRequest()
			req.open('GET', url, true)
			if (opts.dataType &&
				// responseType json not implemented in webkit, we'll do it manually later on
				opts.dataType !== 'json') {
				req.responseType = opts.dataType
			}
			req.onprogress = function (ev) {
				if (opts.onprogress) {
					opts.onprogress(ev.loaded, ev.total)
				}
			}
			var xhrError = null
			req.onload = function () {
				var data = req.response
				if (!(req.status >= 200 && req.status < 300 || req.status === 304)) {
					xhrError = new Error('Couldn\'t load ' + url + '. Status: ' + req.statusCode)
				} else {
					// manually parse out a request expecting a JSON response
					if (opts.dataType === 'json') {
						try {
							data = JSON.parse(data)
						} catch (e) {
							xhrError = e
						}
					}
				}

				if (opts.onload) {
					opts.onload(xhrError, data)
				}
			}
			req.onerror = function (req) {
				xhrError = new Error('Couldn\'t load ' + url + '. Status: ' + req.type)
				opts.onload(xhrError, req)
			}
			try {
				req.send(null)
			} catch(e) {console.log(e)}
		},
		getQueryCommands: function () {
			var search = /([^&=]+)/g
			var query  = window.location.search.substring(1)
			var args = []
			var match
			while (match = search.exec(query)) {
				var val = decodeURIComponent(match[1])
				val = val.split(' ')
				val[0] = '+' + val[0]
				args.push.apply(args, val)
			}
			args.unshift.apply(args, [
				'+set', 'sv_dlURL', '"' + window.location.origin + '/assets"',
				'+set', 'r_fullscreen', window.fullscreen ? '1' : '0',
				'+set', 'r_customHeight', '' + window.innerHeight,
				'+set', 'r_customWidth', '' + window.innerWidth,
			])
			if(navigator && navigator.userAgent
				&& navigator.userAgent.match(/mobile/i)) {
				args.unshift.apply(args, [
					'+set', 'in_joystick', '1',
					'+set', 'in_nograb', '1',
					'+set', 'in_mouse', '0',
					'+bind', 'mouse1', '+attack',
					'+bind', 'UPARROW', '+attack',
					'+bind', 'DOWNARROW', '+jump',
					'+bind', 'LEFTARROW', '-strafe',
					'+bind', 'RIGHTARROW', '+strafe',
					'+unbind', 'A',
					'+unbind', 'D',
				])
			} else {
				args.unshift.apply(args, [
					'+set', 'in_joystick', '0',
					'+set', 'in_nograb', '0',
					'+set', 'in_mouse', '1',
				])
			}
			if(window.location.hostname.match(/quake\.games/i)) {
				args.unshift.apply(args, [
					'+set', 'net_socksServer', 'proxy.quake.games',
					'+set', 'net_socksPort', '443',
				])
			} else {
				args.unshift.apply(args, [
					'+set', 'net_socksServer', window.location.hostname,
				])
			}
			return args
		},
		updateVideoCmd: function () {
			var update = 'set r_fullscreen %fs; set r_mode -1; set r_customWidth %w; set r_customHeight %h; vid_restart; '
				.replace('%fs', window.fullscreen ? '1' : '0')
				.replace('%w', window.innerWidth)
				.replace('%h', window.innerHeight)
			Module._Cbuf_AddText(allocate(intArrayFromString(update), 'i8', ALLOC_STACK));
			Module._Cbuf_Execute();
		},
		resizeDelay: null,
		resizeViewport: function () {
			if (!Module['canvas']) {
				// ignore if the canvas hasn't yet initialized
				return;
			}

			if (SYS.resizeDelay) clearTimeout(SYS.resizeDelay);
			SYS.resizeDelay = setTimeout(Browser.safeCallback(SYS.updateVideoCmd), 100);
		},
		quitGameOnUnload: function (e) {
			if(Module['canvas']) {
				Module._Cbuf_AddText(allocate(intArrayFromString('quit;'), 'i8', ALLOC_STACK));
				Module._Cbuf_Execute();
				Module['canvas'].remove()
				Module['canvas'] = null
			}
			return false
		},
		LoadingDescription: function (desc) {
			var flipper = document.getElementById('flipper')
			var progress = document.getElementById('loading-progress')
			var description = progress.querySelector('.description')
			if (!desc) {
				progress.style.display = 'none'
				flipper.style.display = 'none'
				SYS.LoadingProgress(0)
			} else {
				progress.style.display = 'block'
				flipper.style.display = 'block'
			}
			description.innerHTML = desc
		},
		LoadingProgress: function (progress, total) {
			var frac = progress / total
			var progress = document.getElementById('loading-progress')
			var bar = progress.querySelector('.bar')
			bar.style.width = (frac*100) + '%'
		},
		InitJoystick: function (joystick, id) {
			// tap into native event handlers because SDL events are too slow
			var start = JSEvents.eventHandlers.filter(e => e.eventTypeString == 'touchstart')[0]
			var end = JSEvents.eventHandlers.filter(e => e.eventTypeString == 'touchend')[0]
			var move = JSEvents.eventHandlers.filter(e => e.eventTypeString == 'touchmove')[0]
			var keydown = JSEvents.eventHandlers.filter(e => e.eventTypeString == 'keydown')[0]
			var keyup = JSEvents.eventHandlers.filter(e => e.eventTypeString == 'keyup')[0]
			joystick.on('start end move', function(evt, data) {
				var dx = data.angle ? (Math.cos(data.angle.radian) * data.distance) : 0
				var dy = data.angle ? (Math.sin(data.angle.radian) * data.distance) : 0
				var x = data.angle ? dx : Math.round(data.position.x)
				var y = data.angle ? dy : Math.round(data.position.y)
				//var id = joystick.ids.indexOf(data.identifier) + 1
				if(id == 1) {
					if (data.angle && Math.round(y / 40) > 0) {
						keydown.handlerFunc({repeat: true, keyCode: 87, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 87, preventDefault: () => {}});
					}
					if (data.angle && Math.round(y / 40) < 0) {
						keydown.handlerFunc({repeat: true, keyCode: 83, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 83, preventDefault: () => {}});
					}
					if (data.angle && Math.round(x / 40) < 0) {
						keydown.handlerFunc({repeat: true, keyCode: 65, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 65, preventDefault: () => {}});
					}
					if (data.angle && Math.round(x / 40) > 0) {
						keydown.handlerFunc({repeat: true, keyCode: 68, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 68, preventDefault: () => {}});
					}
				}
				
				if(id == 2) {
					if (data.angle && Math.round(y / 40) > 0) {
						keydown.handlerFunc({repeat: true, keyCode: 40, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 40, preventDefault: () => {}});
					}
					if (data.angle && Math.round(y / 40) < 0) {
						keydown.handlerFunc({repeat: true, keyCode: 38, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 38, preventDefault: () => {}});
					}
					if (data.angle && Math.round(x / 40) < 0) {
						keydown.handlerFunc({repeat: true, keyCode: 37, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 37, preventDefault: () => {}});
					}
					if (data.angle && Math.round(x / 40) > 0) {
						keydown.handlerFunc({repeat: true, keyCode: 39, preventDefault: () => {}});
					} else {
						keyup.handlerFunc({keyCode: 39, preventDefault: () => {}});
					}
				}
				
				var touches = [{
					identifier: id,
					screenX: x,
					screenY: y,
					clientX: x,
					clientY: y,
					pageX: x,
					pageY: y,
					movementX: dx,
					movementY: dy,
				}]
				var touchevent = {
					type: 'touch' + evt.type,
					touches: touches,
					preventDefault: () => {},
					changedTouches: touches,
					targetTouches: touches,
				}
				Object.assign(touchevent, touches[0])
				if(evt.type == 'start') start.handlerFunc(touchevent)
				else if (evt.type == 'end') end.handlerFunc(touchevent)
				else if (evt.type == 'move') move.handlerFunc(touchevent)
			})
		},
		InitNippleJoysticks: function() {
			var in_joystick = _Cvar_VariableIntegerValue(
				allocate(intArrayFromString('in_joystick'), 'i8', ALLOC_STACK))
			if(!in_joystick) {
				return
			}
			document.body.classList.add('joysticks')
			if(SYS.joysticks.length > 0) {
				for(var i = 0; i < SYS.joysticks.length; i++) {
					SYS.joysticks[i].destroy()
				}
			}
			SYS.joysticks[0] = nipplejs.create({
				zone: document.getElementById('left-joystick'),
				multitouch: false,
				mode: 'semi',
				size: 100,
				catchDistance: 50,
				maxNumberOfNipples: 1,
				position: {bottom: '50px', left: '50px'},
			})
			SYS.joysticks[1] = nipplejs.create({
				zone: document.getElementById('right-joystick'),
				multitouch: false,
				mode: 'semi',
				size: 100,
				catchDistance: 50,
				maxNumberOfNipples: 1,
				position: {bottom: '50px', right: '50px'},
			})
			SYS.joysticks[2] = nipplejs.create({
				dataOnly: true,
				zone: document.body,
				multitouch: false,
				mode: 'dynamic',
				size: 2,
				catchDistance: 2,
				maxNumberOfNipples: 1,
			})
			SYS.InitJoystick(SYS.joysticks[0], 1)
			SYS.InitJoystick(SYS.joysticks[1], 2)
			SYS.InitJoystick(SYS.joysticks[2], 3)
		},
		DownloadLazyFinish: function (indexFilename, file) {
			SYS.index[indexFilename].downloading = false
			if(file[1].match(/\.opus|\.wav|\.ogg/i)) {
				if(file[0]) {
					SYS.soundCallback.unshift(file[0].replace('/' + SYS.fs_game + '/', ''))
				} else {
					SYS.soundCallback.unshift(file[1].replace('/' + SYS.fs_game + '/', ''))
				}
				SYS.soundCallback = SYS.soundCallback.filter((s, i, arr) => arr.indexOf(s) === i)
			} else if(file[1].match(/\.md3|\.iqm|\.mdr/i)) {
				SYS.modelCallback.unshift(file[1].replace('/' + SYS.fs_game + '/', ''))
				SYS.modelCallback = SYS.modelCallback.filter((s, i, arr) => arr.indexOf(s) === i)
			} else if(SYS.index[indexFilename].shaders.length > 0) {
				if(file[0]) {
					SYS.shaderCallback.unshift.apply(SYS.shaderCallback, [file[0]].concat(SYS.index[indexFilename].shaders))
				} else {
					SYS.shaderCallback.unshift.apply(SYS.shaderCallback, SYS.index[indexFilename].shaders)
				}
				SYS.shaderCallback = SYS.shaderCallback.filter((s, i, arr) => arr.indexOf(s) === i)
			}
		},
		DownloadLazySort: function () {
			SYS.downloadLazy.sort((a, b) => {
				var aIndex = typeof a == 'string'
					? PATH.join(SYS.fs_basepath, a)
					: PATH.join(SYS.fs_basepath, a[1])
				var aVal = typeof SYS.index[aIndex.toLowerCase()] != 'undefined'
						? SYS.index[aIndex.toLowerCase()].shaders.length + 1
						: 0
				var bIndex = typeof a == 'string'
					? PATH.join(SYS.fs_basepath, b)
					: PATH.join(SYS.fs_basepath, b[1])
				var bVal = typeof SYS.index[bIndex.toLowerCase()] != 'undefined'
						? SYS.index[bIndex.toLowerCase()].shaders.length + 1
						: 0
				return aVal - bVal
			})
		},
		DownloadLazy: function () {
			if(SYS.downloadLazy.length == 0 || SYS.downloads.length > 0) return
			// if we haven't sorted the list in a while, sort by number of references to file
			if(_Sys_Milliseconds() - SYS.downloadSort > 1000) {
				SYS.DownloadLazySort()
				SYS.downloadSort = _Sys_Milliseconds()
			}
			var file = SYS.downloadLazy.pop()
			if(!file) return
			if(typeof file == 'string') {
				file = [0, file]
			}
			var indexFilename = PATH.join(SYS.fs_basepath, file[1]).toLowerCase()
			SYSC.mkdirp(PATH.join(SYS.fs_basepath, PATH.dirname(file[1])))
			// if already exists somehow just call the finishing function
			try {
				var handle = FS.stat(PATH.join(SYS.fs_basepath, file[1]))
				if(handle) {
					return SYS.DownloadLazyFinish(indexFilename, file)
				}
			} catch (e) {
				if (!(e instanceof FS.ErrnoError) || e.errno !== ERRNO_CODES.ENOENT) {
					SYSC.Error('fatal', e.message)
				}
			}
			SYSC.DownloadAsset(file[1], () => {}, (err, data) => {
				if(err) {
					return
				}
				FS.writeFile(PATH.join(SYS.fs_basepath, file[1]), new Uint8Array(data), {
					encoding: 'binary', flags: 'w', canOwn: true })
				SYS.DownloadLazyFinish(indexFilename, file)
			})
		},
		DownloadIndex: function (index, cb) {
			SYSC.DownloadAsset(index + '/index.json', SYS.LoadingProgress, (err, data) => {
				if(err) {
					SYS.LoadingDescription('')
					SYSC.ProxyCallback(cb)
					return
				}
				var moreIndex = (JSON.parse((new TextDecoder("utf-8")).decode(data)) || [])
				SYS.index = Object.keys(moreIndex).reduce((obj, k) => {
					obj[k.toLowerCase()] = moreIndex[k]
					obj[k.toLowerCase()].name = PATH.join(index, moreIndex[k].name)
					obj[k.toLowerCase()].downloading = false
					obj[k.toLowerCase()].shaders = []
					return obj
				}, SYS.index || {})
				var bits = ('{' + Object.keys(SYS.index)
					.map(k => '"' + k + '":' + JSON.stringify(SYS.index[k])).join(',')
					+ '}')
					.split('').map(c => c.charCodeAt(0))
				FS.writeFile(PATH.join(SYS.fs_basepath, index, "index.json"),
				 	Uint8Array.from(bits),
					{encoding: 'binary', flags: 'w', canOwn: true })
				cb()
			})
		},
	},
	Sys_PlatformInit: function () {
		SYS.loading = document.getElementById('loading')
		SYS.dialog = document.getElementById('dialog')
		
		// TODO: load this the same way demo does
		if(SYSC.eula) {
			// add eula frame to viewport
			var eula = document.createElement('div')
			eula.id = 'eula-frame'
			eula.innerHTML = '<div id="eula-frame-inner">' +
				'<p>In order to continue, the official Quake3 demo will need to be installed into the browser\'s persistent storage.</p>' +
				'<p>Please read through the demo\'s EULA and click "I Agree" if you agree to it and would like to continue.</p>' +
				'<pre id="eula">' + SYSC.eula + '</pre>' +
				'<button id="agree" class="btn btn-success">I Agree</button>' +
				'<button id="dont-agree" class="btn btn-success">I Don\'t Agree</button>' +
				'</div>'
			SYS.eula = Module['viewport'].appendChild(eula)
		}
		Object.assign(Module, {
			websocket: Object.assign(Module.websocket || {}, {
				url: window.location.search.includes('https://') || window.location.protocol.includes('https')
				? 'wss://'
				: 'ws://'
			})
		})
		window.addEventListener('resize', SYS.resizeViewport)
		SYS.lazyInterval = setInterval(SYS.DownloadLazy, 1)
	},
	Sys_PlatformExit: function () {
		flipper.style.display = 'block'
		flipper.style.animation = 'none'
		SYS.exited = true
		window.removeEventListener('resize', SYS.resizeViewport)

		if (Module['canvas']) {
			Module['canvas'].remove()
		}
		if(typeof Module.exitHandler != 'undefined') {
			Module.exitHandler()
		}
		clearInterval(SYS.lazyInterval)
	},
	Sys_GLimpInit__deps: ['$SDL', '$SYS'],
	Sys_GLimpInit: function () {
		var viewport = document.getElementById('viewport-frame')
		// create a canvas element at this point if one doesnt' already exist
		if (!Module['canvas']) {
			var canvas = document.createElement('canvas')
			canvas.id = 'canvas'
			canvas.width = viewport.offsetWidth
			canvas.height = viewport.offsetHeight
			Module['canvas'] = viewport.appendChild(canvas)
		}
		setTimeout(SYS.InitNippleJoysticks, 100)
	},
	Sys_GLimpSafeInit: function () {
	},
	Sys_BeginDownload__deps: ['$Browser', '$FS', '$PATH', '$IDBFS', '$SYSC'],
	Sys_BeginDownload: function () {
		var cl_downloadName = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('cl_downloadName'), 'i8', ALLOC_STACK)))
		var fs_basepath = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('fs_basepath'), 'i8', ALLOC_STACK)))
		var fs_game = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('fs_game'), 'i8', ALLOC_STACK)))
		
		SYSC.mkdirp(PATH.join(fs_basepath, PATH.dirname(cl_downloadName)))
		
		SYSC.DownloadAsset(cl_downloadName, (loaded, total) => {
			_Cvar_SetValue(allocate(intArrayFromString('cl_downloadSize'), 'i8', ALLOC_STACK), total );
			_Cvar_SetValue(allocate(intArrayFromString('cl_downloadCount'), 'i8', ALLOC_STACK), loaded );
		}, (err, data) => {
			if(err) {
				SYSC.Error('drop', 'Download Error: ' + err.message)
				return
			} else {
				FS.writeFile(PATH.join(fs_basepath, cl_downloadName), new Uint8Array(data), {
					encoding: 'binary', flags: 'w', canOwn: true })
			}
			FS.syncfs(false, Browser.safeCallback(_CL_NextDownload))
		})
	},
	Sys_FS_Startup__deps: ['$SYS', '$Browser', '$FS', '$PATH', '$IDBFS', '$SYSC'],
	Sys_FS_Startup: function (cb) {
		var fs_homepath = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('fs_homepath'), 'i8', ALLOC_STACK)))
		fs_basepath = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('fs_basepath'), 'i8', ALLOC_STACK)))
		SYS.fs_basepath = fs_basepath;
		var fs_basegame = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('fs_basegame'), 'i8', ALLOC_STACK)))
		var sv_pure = _Cvar_VariableIntegerValue(
			allocate(intArrayFromString('sv_pure'), 'i8', ALLOC_STACK))
		var fs_game = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('fs_game'), 'i8', ALLOC_STACK)))
		SYS.fs_game = fs_game;
		var mapname = UTF8ToString(_Cvar_VariableString(
			allocate(intArrayFromString('mapname'), 'i8', ALLOC_STACK)))
		var clcState = _CL_GetClientState()
		const blankFile = new Uint8Array(4)
		
		SYS.LoadingDescription('Loading Game UI...')
		var fsMountPath = fs_basegame
		if(fs_game && fs_game.localeCompare(fs_basegame) !== 0) {
			fsMountPath = fs_game // TODO: comment this out to test server induced downloading
		}
		
		// mount a persistable filesystem into base
		SYSC.mkdirp(fs_basepath)

		try {
			FS.mount(IDBFS, {}, fs_basepath)
		} catch (e) {
			if (!(e instanceof FS.ErrnoError) || e.errno !== ERRNO_CODES.EBUSY) {
				SYSC.Error('fatal', e.message)
			}
		}
		
		var start = Date.now()
		// read from drive
		FS.syncfs(true, function (err) {
			if (err) {
				SYSC.Print(err.message)
				return SYSC.Error('fatal', err.message)
			}

			SYSC.Print('initial sync completed in ' + ((Date.now() - start) / 1000).toFixed(2) + ' seconds')
			SYSC.mkdirp(PATH.join(fs_basepath, fs_basegame))
			SYSC.mkdirp(PATH.join(fs_basepath, fsMountPath))
			
			for(var i = 0; i < (SYS.mods || []).length; i++) {
				var desc = PATH.join(fs_basepath, SYS.mods[i][0], 'description.txt')
				SYSC.mkdirp(PATH.join(PATH.dirname(desc), '0000placeholder.pk3dir'))
				FS.writeFile(desc, Uint8Array.from(SYS.mods[i][1].split('').map(c => c.charCodeAt(0))), {
					encoding: 'binary', flags: 'w', canOwn: true })
			}

			// TODO: is this right? exit early without downloading anything so the server can force it instead
			// server will tell us what pk3s we need
			if(clcState < 4 && sv_pure && fs_game.localeCompare(fs_basegame) !== 0) {
				SYS.LoadingDescription('')
				FS.syncfs(false, () => SYSC.ProxyCallback(cb))
				return
			}

			SYS.downloads = []
			function downloadCurrentIndex() {
				// create virtual file entries for everything in the directory list
				var keys = Object.keys(SYS.index)
				// servers need some map and model info for hitboxes up front
				for(var i = 0; i < keys.length; i++) {
					var file = SYS.index[keys[i]]
					if(typeof file.size == 'undefined') { // create a directory
						SYSC.mkdirp(PATH.join(fs_basepath, file.name))
					} else {
						// TODO: remove this check when webworker is installed
						//   because it will check ETag and replace files
						// only download again if the file does not exist
						try {
							var handle = FS.stat(PATH.join(fs_basepath, file.name))
							if(handle) {
								continue
							}
						} catch (e) {
							if (!(e instanceof FS.ErrnoError) || e.errno !== ERRNO_CODES.ENOENT) {
								SYSC.Error('fatal', e.message)
							}
						}
						// temporary FIX
						// TODO: remove this with when Async file system loading works,
						//   renderer, client, deferred loading cg_deferPlayers|loaddeferred
						// always download these files beforehand
						if(file.name.match(/\.pk3dir/)) {
							// create the directory to make sure it makes it in to Q3s search paths
							SYSC.mkdirp(PATH.dirname(PATH.join(fs_basepath, file.name)))
						}
						if(file.name.match(/\.pk3$|\.wasm|\.qvm|\.cfg|\.arena|\.shader/i)
						// download files for menu system
							|| file.name.match(/\.menu|menus\.txt|ingame\.txt|arenas\.txt/i)
							|| file.name.match(/ui\/.*\.h|\.crosshair|logo512|banner5|\/hit\./i)
						// download required model and bot
							|| file.name.match(/\/sarge\/icon_|sarge\/.*\.skin|botfiles|\.bot|bots\.txt/i)
						// download the current map if it is referred to
							|| file.name.match(new RegExp('\/levelshots\/' + mapname, 'i'))
							|| file.name.match(new RegExp('\/' + mapname + '\.bsp', 'i'))
							|| file.name.match(new RegExp('\/' + mapname + '\.aas', 'i'))) {
							SYS.index[keys[i]].downloading = true
							SYS.downloads.push(file.name)
						} else if (
							// these files can be streamed in
							file.name.match(/(players|player)\/(sarge|major|sidepipe|athena|orion)\//i)
							// download levelshots and common graphics
							|| file.name.match(/levelshots|^ui\/|\/2d\/|common\/|icons\/|menu\/|gfx\/|sfx\//i)
							// stream player icons so they show up in menu
							|| file.name.match(/\/icon_|\.skin/i)
						) {
							SYS.index[keys[i]].downloading = true
							SYS.downloadLazy.push(file.name)
						} else {
							try {
							//	FS.writeFile(PATH.join(fs_basepath, fsMountPath, file.name), blankFile, {
							//		encoding: 'binary', flags: 'w', canOwn: true })
							} catch (e) {
								if (!(e instanceof FS.ErrnoError) || e.errno !== ERRNO_CODES.EEXIST) {
									SYSC.Error('fatal', e.message)
								}
							}
						}
					}
				}
				
				var totals = []
				var progresses = []
				if(SYS.downloads.length === 0) {
					SYS.LoadingDescription('')
					SYSC.ProxyCallback(cb)
				} else {
					Promise.all(SYS.downloads.map((file, i) => new Promise(resolve => {
						totals[i] = 0
						progresses[i] = 0
						SYS.LoadingDescription(file)
						try {
							SYSC.DownloadAsset(file, (progress, total) => {
								// assume its somewhere around 10 MB per pak
								totals[i] = Math.max(progress, total || 10*1024*1024) 
								progresses[i] = progress
								SYS.LoadingProgress(
									progresses.reduce((s, p) => s + p, 0),
									totals.reduce((s, p) => s + p, 0))
							}, (err, data) => {
								progresses[i] = totals[i]
								SYS.LoadingProgress(
									progresses.reduce((s, p) => s + p, 0),
									totals.reduce((s, p) => s + p, 0))
								if(err) return resolve(err)
								try {
									FS.writeFile(PATH.join(fs_basepath, file), new Uint8Array(data), {
										encoding: 'binary', flags: 'w', canOwn: true })
								} catch (e) {
									if (!(e instanceof FS.ErrnoError) || e.errno !== ERRNO_CODES.EEXIST) {
										SYSC.Error('fatal', e.message)
									}
								}
								resolve(file)
							})
						} catch (e) {resolve(e)}
						// save to drive
					}))).then(() => {
						SYS.downloads = []
						SYS.LoadingDescription('')
						FS.syncfs(false, () => SYSC.ProxyCallback(cb))
					})
				}
				
				// TODO: create an icon for the favicon so we know we did it right
				/*
				var buf = FS.readFile('/foo/bar')
		    var blob = new Blob([buf],  {"type" : "application/octet-stream" })
		    var url = URL.createObjectURL(blob)
				var link = document.querySelector("link[rel*='icon']") || document.createElement('link')
		    link.type = 'image/x-icon'
		    link.rel = 'shortcut icon'
		    link.href = url
		    document.getElementsByTagName('head')[0].appendChild(link)
				*/
			}
			
			if(fsMountPath != fs_basegame) {
				SYS.DownloadIndex(fs_basegame, () => {
					SYS.DownloadIndex(fsMountPath, downloadCurrentIndex)
				})
			} else {
				SYS.DownloadIndex(fsMountPath, downloadCurrentIndex)
			}
		})
	},
	Sys_FOpen__deps: ['$SYS', '$FS', '$PATH', 'fopen'],
	Sys_FOpen: function (ospath, mode) {
		var handle = 0
		try {
			var filename = UTF8ToString(ospath).replace(/\/\//ig, '/')
			ospath = allocate(intArrayFromString(filename), 'i8', ALLOC_STACK)
			mode = allocate(intArrayFromString(UTF8ToString(mode)
				.replace('b', '')), 'i8', ALLOC_STACK);
			handle = _fopen(ospath, mode)
			//if(handle === 0) {
				// use the index to make a case insensitive lookup
				var indexFilename = filename.toLowerCase()
				if(SYS.index && typeof SYS.index[indexFilename] != 'undefined') {
					var altName = filename.substr(0, filename.length
					  - SYS.index[indexFilename].name.length) 
						+ SYS.index[indexFilename].name
					if(handle === 0 && altName != filename) {
						handle = _fopen(allocate(intArrayFromString(altName), 'i8', ALLOC_STACK), mode)
						//if(handle > 0) {
						//	return handle
						//}
					}
					var loading = UTF8ToString(_Cvar_VariableString(
						allocate(intArrayFromString('r_loadingShader'), 'i8', ALLOC_STACK)))
					if(loading.length === 0) {
						loading = UTF8ToString(_Cvar_VariableString(
							allocate(intArrayFromString('snd_loadingSound'), 'i8', ALLOC_STACK)))
					} 
					if(!SYS.index[indexFilename].downloading) {
						SYS.downloadLazy.push([loading, SYS.index[indexFilename].name])
						SYS.index[indexFilename].shaders.push(loading)
						SYS.index[indexFilename].downloading = true
					} else if (!SYS.index[indexFilename].shaders.includes(loading)) {
						SYS.index[indexFilename].shaders.push(loading)
					}
				}
			//}
		} catch (e) {
			// short for fstat check in sys_unix.c!!!
			if(e.code == 'ENOENT') {
				return 0
			}
			throw e
		}
		return handle
	},
	Sys_UpdateShader: function () {
		var nextFile = SYS.shaderCallback.pop()
		if(!nextFile) return 0;
		nextFile = nextFile.replace(/.*?\.pk3dir\//i, '') // relative paths only not to exceed MAX_QPATH
		var filename = _S_Malloc(nextFile.length + 1);
		stringToUTF8(nextFile + '\0', filename, nextFile.length+1);
		return filename
	},
	Sys_UpdateSound: function () {
		var nextFile = SYS.soundCallback.pop()
		if(!nextFile) return 0;
		nextFile = nextFile.replace(/.*?\.pk3dir\//i, '') // relative paths only not to exceed MAX_QPATH
		var filename = _S_Malloc(nextFile.length + 1);
		stringToUTF8(nextFile + '\0', filename, nextFile.length+1);
		return filename
	},
	Sys_UpdateModel: function () {
		var nextFile = SYS.modelCallback.pop()
		if(!nextFile) return 0;
		nextFile = nextFile.replace(/.*?\.pk3dir\//i, '') // relative paths only not to exceed MAX_QPATH
		var filename = _S_Malloc(nextFile.length + 1);
		stringToUTF8(nextFile + '\0', filename, nextFile.length+1);
		return filename
	},
	Sys_ListFiles__deps: ['$PATH', 'Z_Malloc', 'S_Malloc'],
	Sys_ListFiles: function (directory, ext, filter, numfiles, dironly) {
		directory = UTF8ToString(directory);
		ext = UTF8ToString(ext);
		if (ext === '/') {
			ext = null;
			dironly = true;
		}

		// TODO support filter
		
		var contents;
		try {
			contents = FS.readdir(directory)
				.filter(f => !dironly || FS.isDir(FS.stat(PATH.join(directory, f)).mode))
			contents = contents.concat(Object.keys(SYS.index)
				.filter(k => k.match(new RegExp(directory + '\\/[^\\/]+\\/?$', 'i'))
					&& (!dironly || typeof SYS.index[k].size == 'undefined'))
				.map(k => PATH.basename(SYS.index[k].name)))
				.filter((f, i, arr) => f && arr.indexOf(f) === i)
			if(contents.length > 5000) {
				debugger
			}
		} catch (e) {
			{{{ makeSetValue('numfiles', '0', '0', 'i32') }}};
			return null;
		}

		var matches = [];
		for (var i = 0; i < contents.length; i++) {
			var name = contents[i].toLowerCase();
			if (!ext || name.lastIndexOf(ext) === (name.length - ext.length)
				|| (ext.match(/tga/i) && name.lastIndexOf('png') === (name.length - ext.length))
				|| (ext.match(/tga/i) && name.lastIndexOf('jpg') === (name.length - ext.length))
			) {
				matches.push(contents[i]);
			}
		}

		{{{ makeSetValue('numfiles', '0', 'matches.length', 'i32') }}};

		if (!matches.length) {
			return null;
		}

		// return a copy of the match list
		var list = _Z_Malloc((matches.length + 1) * 4);

		var i;
		for (i = 0; i < matches.length; i++) {
			var filename = _S_Malloc(matches[i].length + 1);

			stringToUTF8(matches[i], filename, matches[i].length+1);

			// write the string's pointer back to the main array
			{{{ makeSetValue('list', 'i*4', 'filename', 'i32') }}};
		}

		// add a NULL terminator to the list
		{{{ makeSetValue('list', 'i*4', '0', 'i32') }}};

		return list;
	},
	Sys_FS_Shutdown__deps: ['$Browser', '$FS', '$SYSC'],
	Sys_FS_Shutdown: function (cb) {
		// save to drive
		FS.syncfs(function (err) {
			SYSC.FS_Shutdown(function (err) {
				if (err) {
					// FIXME cb_free_context(context)
					SYSC.Error('fatal', err)
					return
				}
				
				SYSC.ProxyCallback(cb)
			})
		})
	},
	Sys_SocksConnect__deps: ['$Browser', '$SOCKFS'],
	Sys_SocksConnect: function () {
		var timer = setTimeout(Browser.safeCallback(_SOCKS_Frame_Proxy), 10000)
		var callback = () => {
			clearTimeout(timer)
			Browser.safeCallback(_SOCKS_Frame_Proxy)
		}
		Module['websocket'].on('open', callback)
		Module['websocket'].on('message', callback)
		Module['websocket'].on('error', callback)
	},
	Sys_SocksMessage__deps: ['$Browser', '$SOCKFS'],
	Sys_SocksMessage: function () {
	},
	Sys_Milliseconds: function () {
		if (!SYS.timeBase) {
			SYS.timeBase = Date.now()
		}

		if (window.performance.now) {
			return parseInt(window.performance.now(), 10)
		} else if (window.performance.webkitNow) {
			return parseInt(window.performance.webkitNow(), 10)
		} else {
			return Date.now() - SYS.timeBase()
		}
	},
	Sys_GetCurrentUser: function () {
		var stack = stackSave()
		var ret = allocate(intArrayFromString('player'), 'i8', ALLOC_STACK)
		stackRestore(stack)
		return ret
	},
	Sys_Dialog: function (type, message, title) {
		SYSC.Error('SYS_Dialog not implemented')
	},
	Sys_ErrorDialog: function (error) {
		var errorStr = UTF8ToString(error)
		// print call stack so we know where the error came from
		try {
			throw new Error(errorStr)
		} catch (e) {
			console.log(e)
		}
		var title = SYS.dialog.querySelector('.title')
		if(title) {
			title.className = 'title error'
			title.innerHTML = 'Error'
			var description = SYS.dialog.querySelector('.description')
			description.innerHTML = errorStr
			SYS.dialog.style.display = 'block'
		}
		if (typeof Module.exitHandler != 'undefined') {
			SYS.exited = true
			Module.exitHandler(errorStr)
			return
		}
	},
	Sys_CmdArgs__deps: ['stackAlloc'],
	Sys_CmdArgs: function () {
		var argv = ['ioq3'].concat(SYS.args).concat(SYS.getQueryCommands())
		var argc = argv.length
		// merge default args with query string args
		var list = stackAlloc((argc + 1) * {{{ Runtime.POINTER_SIZE }}})
		for (var i = 0; i < argv.length; i++) {
			HEAP32[(list >> 2) + i] = allocateUTF8OnStack(argv[i])
		}
		HEAP32[(list >> 2) + argc] = 0
		return list
	},
	Sys_CmdArgsC: function () {
		return SYS.args.length + SYS.getQueryCommands().length + 1
	},
	Sys_DefaultBasePath: function () {
		return allocate(intArrayFromString('/base'), 'i8', ALLOC_STACK)
	},
	Sys_Pwd: function () {
		return allocate(intArrayFromString('/base'), 'i8', ALLOC_STACK)
	}
}
autoAddDeps(LibrarySys, '$SYS')
mergeInto(LibraryManager.library, LibrarySys);
