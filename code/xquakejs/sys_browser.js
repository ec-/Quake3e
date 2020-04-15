var LibrarySys = {
	$SYS__deps: ['$SYSC', '$SYSF', '$SYSM', '$SYSN', '$SYSI', '$SDL'],
	$SYS: {
		resizeDelay: null,
		style: null,
		shaderCallback: [],
		soundCallback: [],
		modelCallback: [],
		quitGameOnUnload: function (e) {
			if(Module['canvas']) {
				_Cbuf_AddText(allocate(intArrayFromString('quit;'), 'i8', ALLOC_STACK));
				_Cbuf_Execute();
				Module['canvas'].remove()
				Module['canvas'] = null
			}
			return false
		},
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
}
autoAddDeps(LibrarySys, '$SYS')
mergeInto(LibraryManager.library, LibrarySys);
