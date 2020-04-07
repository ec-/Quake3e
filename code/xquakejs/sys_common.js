var LibrarySysCommon = {
	$SYSC__deps: ['$Browser', '$FS', '$PATH', '$SYS', 'Com_Printf', 'Com_Error'],
	$SYSC: {
		Print: function (str) {
			str = allocate(intArrayFromString(str + '\n'), 'i8', ALLOC_STACK);

			_Com_Printf(str);
		},
		Error: function (level, err) {
			if (level === 'fatal') {
				level = 0;
			} else if (level === 'drop') {
				level = 1;
			} else if (level === 'serverdisconnect') {
				level = 2;
			} else if (level === 'disconnect') {
				level = 3;
			} else if (level === 'need_cd') {
				level = 4;
			} else {
				level = 0;
			}

			err = allocate(intArrayFromString(err + '\n'), 'i8', ALLOC_STACK);
			if(!err) err = UTF8ToString(err);

			Browser.safeCallback(() => _Com_Error(level, err))();
		},
		ProxyCallback: function () {
			Browser.safeCallback(() => {
				try {
					_Com_Frame_Proxy();
				} catch (e) {
					if (e instanceof ExitStatus) {
						return;
					}
					// TODO should we try and call back in using __Error?
					var message = _S_Malloc(e.message.length + 1);
					stringToUTF8(e.message, message, e.message.length+1);
					_Sys_ErrorDialog(message);
					throw e;
				}
			})()
		},
		DownloadAsset: function (asset, onprogress, onload) {
			var sv_dlURL = UTF8ToString(_Cvar_VariableString(
				allocate(intArrayFromString('sv_dlURL'), 'i8', ALLOC_STACK)));
			var name = asset.replace(/^\//, ''); //.replace(/(.+\/|)(.+?)$/, '$1' + asset.checksum + '-$2');
			var url = (sv_dlURL.includes('://')
				? sv_dlURL
				: window
					? (window.location.protocol + '//' + sv_dlURL)
					: ('https://' + sv_dlURL)) + '/' + name;

			SYS.DoXHR(url, {
				dataType: 'arraybuffer',
				onprogress: onprogress,
				onload: onload
			});
		},
		FS_Startup: function (callback) {
			callback();
		},
		FS_Shutdown: function (callback) {
			callback();
		},
		mkdirp: function (p) {
			try {
				FS.mkdir(p, 0777);
			} catch (e) {
				// make the subdirectory and then retry
				if ((e instanceof FS.ErrnoError) && e.errno === ERRNO_CODES.ENOENT) {
					SYSC.mkdirp(PATH.dirname(p));
					SYSC.mkdirp(p);
					return;
				}

				// if we got any other error, let's see if the directory already exists
				var stat;
				try {
					stat = FS.stat(p);
				}
				catch (e) {
					SYSC.Error('fatal', e.message || e);
					return;
				}

				if (!FS.isDir(stat.mode)) {
					SYSC.Error('fatal', e.message);
				}
			}
		},
	},
	Sys_DefaultHomePath: function () {
		return 0;
	},
	Sys_RandomBytes: function (string, len) {
		return false;
	},
	Sys_GetClipboardData: function () {
		return 0;
	},
	Sys_LowPhysicalMemory: function () {
		return false;
	},
	Sys_Basename__deps: ['$PATH'],
	Sys_Basename: function (path) {
		path = UTF8ToString(path);
		path = PATH.basename(path);
		var basename = allocate(intArrayFromString(path), 'i8', ALLOC_STACK);
		return basename;
	},
	Sys_DllExtension__deps: ['$PATH'],
	Sys_DllExtension: function (path) {
		path = UTF8ToString(path);
		return PATH.extname(path) == '.wasm';
	},
	Sys_Dirname__deps: ['$PATH'],
	Sys_Dirname: function (path) {
		path = UTF8ToString(path);
		path = PATH.dirname(path);
		var dirname = allocate(intArrayFromString(path), 'i8', ALLOC_STACK);
		return dirname;
	},
	Sys_Mkfifo: function (path) {
		return 0;
	},
	Sys_FreeFileList__deps: ['Z_Free'],
	Sys_FreeFileList: function (list) {
		if (!list) {
			return;
		}

		var ptr;
		for (var i = 0; (ptr = {{{ makeGetValue('list', 'i*4', 'i32') }}}); i++) {
			_Z_Free(ptr);
		}

		_Z_Free(list);
	},
	Sys_Mkdir: function (directory) {
		directory = UTF8ToString(directory);
		try {
			FS.mkdir(directory, 0777);
		} catch (e) {
			if (!(e instanceof FS.ErrnoError)) {
				SYSC.Error('drop', e.message);
			}
			return e.errno === ERRNO_CODES.EEXIST;
		}
		return true;
	},
	Sys_Cwd: function () {
		var cwd = allocate(intArrayFromString(FS.cwd()), 'i8', ALLOC_STACK);
		return cwd;
	},
	Sys_Sleep: function () {
	},
	Sys_SetEnv: function (name, value) {
		name = UTF8ToString(name);
		value = UTF8ToString(value);
	},
	Sys_PID: function () {
		return 0;
	},
	Sys_PIDIsRunning: function (pid) {
		return 1;
	},
	Sys_LoadLibrary__deps: [],
	Sys_LoadLibrary: function (name) {
		return 0;
		return loadDynamicLibrary(name) // passing memory address
			.then(handle => SYSC.proxyCallback(handle))
	},
	Sys_LoadFunction: function () {
		throw new Error('TODO: Load DLL files')
	},
	Sys_UnloadLibrary: function () {
		
	},
	Sys_SetAffinityMask: function () {
		throw new Error('TODO: support using background workers or not')
	},
	Sys_ShowConsole: function () {
		// not implemented
	},
	Sys_GetFileStats: function(filename, size, mtime, ctime) {
		try {
			var stat = FS.stat(UTF8ToString(name))
			{{{ makeSetValue('size', '0', 'stat.size', 'i32') }}}
			{{{ makeSetValue('mtime', '0', 'stat.mtime', 'i32') }}}
			{{{ makeSetValue('ctime', '0', 'stat.ctime', 'i32') }}}
			return true
		} catch (e) {
			if ((e instanceof FS.ErrnoError) && e.errno === ERRNO_CODES.ENOENT) {
				{{{ makeSetValue('size', '0', '0', 'i32') }}}
				{{{ makeSetValue('mtime', '0', '0', 'i32') }}}
				{{{ makeSetValue('ctime', '0', '0', 'i32') }}}
				return false
			}
			throw e
		}
	}
};

autoAddDeps(LibrarySysCommon, '$SYSC');
mergeInto(LibraryManager.library, LibrarySysCommon);
