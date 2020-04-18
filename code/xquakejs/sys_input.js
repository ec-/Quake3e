var LibrarySysInput = {
  $SYSI__deps: ['$SDL'],
  $SYSI: {
    joysticks: [],
    inputInterface: 0,
    InputPushKeyEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(32)

      HEAP32[((event+0)>>2)]= evt.type == 'keydown' ? 0x300 : 0x301; //Uint32 type; ::SDL_KEYDOWN or ::SDL_KEYUP
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds()
      HEAP32[((event+8)>>2)]=0; // windowID
      HEAP32[((event+12)>>2)]=(1 << 2) + (evt.repeat ? 1 : 0); // ::SDL_PRESSED or ::SDL_RELEASED
      
      var key = SDL.lookupKeyCodeForEvent(evt)
      var scan
      if (key >= 1024) {
        scan = key - 1024
      } else {
        scan = SDL.scanCodes[key] || key
      }

      HEAP32[((event+16)>>2)]=scan
      HEAP32[((event+20)>>2)]=key
      HEAP32[((event+24)>>2)]=SDL.modState
      HEAP32[((event+28)>>2)]=0
      if(evt.type == 'keydown')
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[0], event)
      if(evt.type == 'keyup')
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[1], event)
      stackRestore(stack)
    },
    InputPushTextEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(24)
      HEAP32[((event+0)>>2)]= 0x303; //Uint32 type; ::SDL_TEXTINPUT
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds()
      HEAP32[((event+8)>>2)]=0; // windowID
      var text = intArrayFromString(String.fromCharCode(evt.charCode))
      var j = 0
      for (var i = 12; i < 24; i+=4) {
        HEAP32[((event+i)>>2)]=text[j]
        j++
      }
      Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[2], event)
      stackRestore(stack)
    },
    InputPushMouseEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(36)
      if (evt.type != 'mousemove') {
        var down = evt.type == 'mousedown'
        HEAP32[((event+0)>>2)]=down ? 0x401 : 0x402
        HEAP32[((event+4)>>2)]=_Sys_Milliseconds(); // timestamp
        HEAP32[((event+8)>>2)]=0; // windowid
        HEAP32[((event+12)>>2)]=0; // mouseid
        HEAP32[((event+16)>>2)]=((down ? 1 : 0) << 8) + (evt.button+1); // DOM buttons are 0-2, SDL 1-3
        HEAP32[((event+20)>>2)]=evt.pageX
        HEAP32[((event+24)>>2)]=evt.pageY
      } else {
        HEAP32[((event+0)>>2)]=0x400
        HEAP32[((event+4)>>2)]=_Sys_Milliseconds()
        HEAP32[((event+8)>>2)]=0
        HEAP32[((event+12)>>2)]=0
        HEAP32[((event+16)>>2)]=SDL.buttonState
        HEAP32[((event+20)>>2)]=evt.pageX
        HEAP32[((event+24)>>2)]=evt.pageY
        HEAP32[((event+28)>>2)]=Browser.getMovementX(evt)
        HEAP32[((event+32)>>2)]=Browser.getMovementY(evt)
      }
      if (evt.type == 'mousemove')
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[3], event)
      else
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[4], event)
      stackRestore(stack)
    },
    InputPushWheelEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(24)
      HEAP32[((event+0)>>2)]=0x403;
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds(); // timestamp
      HEAP32[((event+8)>>2)]=0; // windowid
      HEAP32[((event+12)>>2)]=0; // mouseid
      HEAP32[((event+16)>>2)]=evt.deltaX;
      HEAP32[((event+20)>>2)]=evt.deltaY;
      Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[5], event)
      stackRestore(stack)
    },
    InputPushTouchEvent: function (joystick, id, evt, data) {
      var stack = stackSave()
      var event = stackAlloc(44)

      if(id == 1) {
        if (evt.angle && Math.round(y / 40) > 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 87})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 87})
        }
        if (evt.angle && Math.round(y / 40) < 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 83})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 83})
        }
        if (evt.angle && Math.round(x / 40) < 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 65})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 65})
        }
        if (evt.angle && Math.round(x / 40) > 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 68})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 68})
        }
      }
      
      if(id == 2) {
        if (evt.angle && Math.round(y / 40) > 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 40})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 40})
        }
        if (evt.angle && Math.round(y / 40) < 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 38})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 38})
        }
        if (evt.angle && Math.round(x / 40) < 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 37})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 37})
        }
        if (evt.angle && Math.round(x / 40) > 0) {
          SYSI.InputPushKeyEvent({type: 'keydown', repeat: true, keyCode: 39})
        } else {
          SYSI.InputPushKeyEvent({type: 'keyup', keyCode: 39})
        }
      }

      var w = Module['canvas'].width;
      var h = Module['canvas'].height;
      var dx = data.angle ? (Math.cos(data.angle.radian) * data.distance) : 0
      var dy = data.angle ? (Math.sin(data.angle.radian) * data.distance) : 0
      var x = data.angle ? dx : Math.round(data.position.x)
      var y = data.angle ? dy : Math.round(data.position.y)

      HEAP32[((event+0)>>2)]=evt.type == 'start' ? 0x700 : evt.type == 'end' ? 0x701 : 0x702
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds()
      HEAP32[((event+8)>>2)] = id
      HEAP32[((event+12)>>2)] = 0
      HEAP32[((event+16)>>2)] = id
      HEAP32[((event+20)>>2)] = 0
      HEAPF32[((event+24)>>2)]=x / w
      HEAPF32[((event+28)>>2)]=y / h
      HEAPF32[((event+32)>>2)]=dx / w
      HEAPF32[((event+36)>>2)]=dy / h
      if (data.force !== undefined) {
        HEAPF32[((event+(40))>>2)]=data.force
      } else { // No pressure data, send a digital 0/1 pressure.
        HEAPF32[((event+(40))>>2)]=data.type == 'end' ? 0 : 1
      }
      Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[6], event)
      stackRestore(stack)
    },
    InputInit: function () {
      // TODO: clear JSEvents.eventHandlers
      var inputInterface = allocate(new Int32Array(20), 'i32', ALLOC_DYNAMIC)
      Browser.safeCallback(_IN_PushInit)(inputInterface)
      SYSI.inputInterface = []
      for(var ei = 0; ei < 20; ei++) {
        SYSI.inputInterface[ei] = getValue(inputInterface + 4 * ei, 'i32', true)
      }
      window.addEventListener('keydown', SYSI.InputPushKeyEvent, false)
      window.addEventListener('keyup', SYSI.InputPushKeyEvent, false)
      window.addEventListener('keypress', SYSI.InputPushTextEvent, false)
      
      Module['canvas'].addEventListener('mousemove', SYSI.InputPushMouseEvent, false)
      Module['canvas'].addEventListener('mousedown', SYSI.InputPushMouseEvent, false)
      Module['canvas'].addEventListener('mouseup', SYSI.InputPushMouseEvent, false)
      Module['canvas'].addEventListener('mousewheel', SYSI.InputPushWheelEvent, false)
      /*
      let nipple handle touch events
      Module['canvas'].addEventListener('touchstart', SYSI.InputPushTouchEvent, false)
      Module['canvas'].addEventListener('touchend', SYSI.InputPushTouchEvent, false)
      Module['canvas'].addEventListener('touchmove', SYSI.InputPushTouchEvent, false)
      Module['canvas'].addEventListener('touchcancel', SYSI.InputPushTouchEvent, false)
      */
      SYSI.InitNippleJoysticks()
    },
    InitNippleJoysticks: function() {
      var in_joystick = SYSC.Cvar_VariableIntegerValue('in_joystick')
      if(!in_joystick) {
        return
      }
      document.body.classList.add('joysticks')
      if(SYSI.joysticks.length > 0) {
        for(var i = 0; i < SYSI.joysticks.length; i++) {
          SYSI.joysticks[i].destroy()
        }
      }
      SYSI.joysticks[0] = nipplejs.create({
        zone: document.getElementById('left-joystick'),
        multitouch: false,
        mode: 'semi',
        size: 100,
        catchDistance: 50,
        maxNumberOfNipples: 1,
        position: {bottom: '50px', left: '50px'},
      })
      SYSI.joysticks[1] = nipplejs.create({
        zone: document.getElementById('right-joystick'),
        multitouch: false,
        mode: 'semi',
        size: 100,
        catchDistance: 50,
        maxNumberOfNipples: 1,
        position: {bottom: '50px', right: '50px'},
      })
      SYSI.joysticks[2] = nipplejs.create({
        dataOnly: true,
        zone: document.body,
        multitouch: false,
        mode: 'dynamic',
        size: 2,
        catchDistance: 2,
        maxNumberOfNipples: 1,
      })
      SYSI.joysticks[0].on('start end move', SYSI.InputPushTouchEvent.bind(null, SYSI.joysticks[0], 1))
      SYSI.joysticks[1].on('start end move', SYSI.InputPushTouchEvent.bind(null, SYSI.joysticks[1], 2))
      SYSI.joysticks[2].on('start end move', SYSI.InputPushTouchEvent.bind(null, SYSI.joysticks[2], 3))
    },
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
		setTimeout(SYSI.InputInit, 100)
	},
	Sys_GLimpSafeInit: function () {
	},
  
}
autoAddDeps(LibrarySysInput, '$SYSI')
mergeInto(LibraryManager.library, LibrarySysInput)
