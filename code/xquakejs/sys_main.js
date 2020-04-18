var LibrarySysMain = {
  $SYSM: {
    exited: false,
    loading: null,
    dialog: null,
    eula: null,
    timeBase: null,
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
			_Cbuf_AddText(allocate(intArrayFromString(update), 'i8', ALLOC_STACK));
			_Cbuf_Execute();
		},
    resizeViewport: function () {
			if (!Module['canvas']) {
				// ignore if the canvas hasn't yet initialized
				return;
			}

			if (SYSM.resizeDelay) clearTimeout(SYSM.resizeDelay);
			SYSM.resizeDelay = setTimeout(Browser.safeCallback(SYSM.updateVideoCmd), 100);
		},
  },
  Sys_PlatformInit__deps: ['stackAlloc'],
  Sys_PlatformInit: function () {
    SYSC.varStr = allocate(new Int8Array(4096), 'i8', ALLOC_DYNAMIC)
    SYSM.loading = document.getElementById('loading')
    SYSM.dialog = document.getElementById('dialog')
    
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
      SYSM.eula = Module['viewport'].appendChild(eula)
    }
    Object.assign(Module, {
      websocket: Object.assign(Module.websocket || {}, {
        url: window.location.search.includes('https://') || window.location.protocol.includes('https')
        ? 'wss://'
        : 'ws://'
      })
    })
    window.addEventListener('resize', SYSM.resizeViewport)
    SYSN.lazyInterval = setInterval(SYSN.DownloadLazy, 10)
  },
  Sys_PlatformExit: function () {
    flipper.style.display = 'block'
    flipper.style.animation = 'none'
    SYSM.exited = true
    window.removeEventListener('resize', SYSM.resizeViewport)

    if (Module['canvas']) {
      Module['canvas'].remove()
    }
    if(typeof Module.exitHandler != 'undefined') {
      Module.exitHandler()
    }
    clearInterval(SYSN.lazyInterval)
  },
  Sys_Milliseconds: function () {
		if (!SYSM.timeBase) {
			SYSM.timeBase = Date.now()
		}

		if (window.performance.now) {
			return parseInt(window.performance.now(), 10)
		} else if (window.performance.webkitNow) {
			return parseInt(window.performance.webkitNow(), 10)
		} else {
			return Date.now() - SYSM.timeBase()
		}
	},
	Sys_GetCurrentUser: function () {
		return allocate(intArrayFromString('player'), 'i8', ALLOC_STACK)
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
    var title = SYSM.dialog.querySelector('.title')
    if(title) {
      title.className = 'title error'
      title.innerHTML = 'Error'
      var description = SYSM.dialog.querySelector('.description')
      description.innerHTML = errorStr
      SYSM.dialog.style.display = 'block'
    }
    if (typeof Module.exitHandler != 'undefined') {
      SYSM.exited = true
      Module.exitHandler(errorStr)
      return
    }
  },
  Sys_CmdArgs__deps: ['stackAlloc'],
  Sys_CmdArgs: function () {
    var argv = ['ioq3'].concat(SYSM.args).concat(SYSM.getQueryCommands())
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
    return SYSM.args.length + SYSM.getQueryCommands().length + 1
  },
}
autoAddDeps(LibrarySysMain, '$SYSM')
mergeInto(LibraryManager.library, LibrarySysMain);
