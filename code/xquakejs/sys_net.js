var LibrarySysNet = {
  $SYSN__deps: ['$SOCKFS'],
  $SYSN: {
    downloadLazy: [],
		downloadCount: 0,
		downloads: [],
		downloadSort: 0,
    lazyIterval: 0,
    LoadingDescription: function (desc) {
			var flipper = document.getElementById('flipper')
			var progress = document.getElementById('loading-progress')
			var description = progress.querySelector('.description')
			if (!desc) {
				progress.style.display = 'none'
				flipper.style.display = 'none'
				SYSN.LoadingProgress(0)
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
    DownloadLazyFinish: function (indexFilename, file) {
			SYSF.index[indexFilename].downloading = false
			if(file[1].match(/\.opus|\.wav|\.ogg/i)) {
				if(file[0]) {
					SYS.soundCallback.unshift(file[0].replace('/' + SYSF.fs_game + '/', ''))
				} else {
					SYS.soundCallback.unshift(file[1].replace('/' + SYSF.fs_game + '/', ''))
				}
				SYS.soundCallback = SYS.soundCallback.filter((s, i, arr) => arr.indexOf(s) === i)
			} else if(file[1].match(/\.md3|\.iqm|\.mdr/i)) {
				if(file[0]) {
					SYS.modelCallback.unshift(file[0].replace('/' + SYSF.fs_game + '/', ''))
				} else {
					SYS.modelCallback.unshift(file[1].replace('/' + SYSF.fs_game + '/', ''))
				}
				SYS.modelCallback = SYS.modelCallback.filter((s, i, arr) => arr.indexOf(s) === i)
			} else if(SYSF.index[indexFilename].shaders.length > 0) {
				if(file[0]) {
					SYS.shaderCallback.unshift.apply(SYS.shaderCallback, [file[0]].concat(SYSF.index[indexFilename].shaders))
				} else {
					SYS.shaderCallback.unshift.apply(SYS.shaderCallback, SYSF.index[indexFilename].shaders)
				}
				SYS.shaderCallback = SYS.shaderCallback.filter((s, i, arr) => arr.indexOf(s) === i)
			}
		},
		DownloadLazySort: function () {
			SYSN.downloadLazy.sort((a, b) => {
				var aIndex = typeof a == 'string'
					? PATH.join(SYSF.fs_basepath, a)
					: PATH.join(SYSF.fs_basepath, a[1])
				var aVal = typeof SYSF.index[aIndex.toLowerCase()] != 'undefined'
						? SYSF.index[aIndex.toLowerCase()].shaders.length + 1
						: 0
				var bIndex = typeof a == 'string'
					? PATH.join(SYSF.fs_basepath, b)
					: PATH.join(SYSF.fs_basepath, b[1])
				var bVal = typeof SYSF.index[bIndex.toLowerCase()] != 'undefined'
						? SYSF.index[bIndex.toLowerCase()].shaders.length + 1
						: 0
				return aVal - bVal
			})
		},
		DownloadLazy: function () {
			if(SYSN.downloadLazy.length == 0 || SYSN.downloads.length > 0) return
			// if we haven't sorted the list in a while, sort by number of references to file
			if(_Sys_Milliseconds() - SYSN.downloadSort > 1000) {
				SYSN.DownloadLazySort()
				SYSN.downloadSort = _Sys_Milliseconds()
			}
			var file = SYSN.downloadLazy.pop()
			if(!file) return
			if(typeof file == 'string') {
				file = [0, file]
			}
			var indexFilename = PATH.join(SYSF.fs_basepath, file[1]).toLowerCase()
			SYSC.mkdirp(PATH.join(SYSF.fs_basepath, PATH.dirname(file[1])))
			// if already exists somehow just call the finishing function
			try {
				var handle = FS.stat(PATH.join(SYSF.fs_basepath, file[1]))
				if(handle) {
					return SYSN.DownloadLazyFinish(indexFilename, file)
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
				FS.writeFile(PATH.join(SYSF.fs_basepath, file[1]), new Uint8Array(data), {
					encoding: 'binary', flags: 'w', canOwn: true })
				SYSN.DownloadLazyFinish(indexFilename, file)
			})
		},
		DownloadIndex: function (index, cb) {
			SYSC.DownloadAsset(index + '/index.json', SYSN.LoadingProgress, (err, data) => {
				if(err) {
					SYSN.LoadingDescription('')
					SYSC.ProxyCallback(cb)
					return
				}
				var moreIndex = (JSON.parse((new TextDecoder("utf-8")).decode(data)) || [])
				SYSF.index = Object.keys(moreIndex).reduce((obj, k) => {
					obj[k.toLowerCase()] = moreIndex[k]
					obj[k.toLowerCase()].name = PATH.join(index, moreIndex[k].name)
					obj[k.toLowerCase()].downloading = false
					obj[k.toLowerCase()].shaders = []
					return obj
				}, SYSF.index || {})
				var bits = intArrayFromString('{' + Object.keys(SYSF.index)
					.map(k => '"' + k + '":' + JSON.stringify(SYSF.index[k])).join(',')
					+ '}')
				FS.writeFile(PATH.join(SYSF.fs_basepath, index, "index.json"),
				 	Uint8Array.from(bits),
					{encoding: 'binary', flags: 'w', canOwn: true })
				cb()
			})
		},
  },
  Sys_BeginDownload__deps: ['$Browser', '$FS', '$PATH', '$IDBFS', '$SYSC'],
  Sys_BeginDownload: function () {
    var cl_downloadName = SYSC.Cvar_VariableString('cl_downloadName')
    var fs_basepath = SYSC.Cvar_VariableString('fs_basepath')
    var fs_game = SYSC.Cvar_VariableString('fs_game')
    
    SYSC.mkdirp(PATH.join(fs_basepath, PATH.dirname(cl_downloadName)))
    
    SYSC.DownloadAsset(cl_downloadName, (loaded, total) => {
      SYSC.Cvar_SetValue('cl_downloadSize', total);
      SYSC.Cvar_SetValue('cl_downloadCount', loaded);
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
}
autoAddDeps(LibrarySysNet, '$SYSN')
mergeInto(LibraryManager.library, LibrarySysNet);
