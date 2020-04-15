var LibrarySysInput = {
  $SYSI__deps: ['$SDL'],
  $SYSI: {
    joysticks: [],
    inputInterface: 0,
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
      SYSI.InitJoystick(SYSI.joysticks[0], 1)
      SYSI.InitJoystick(SYSI.joysticks[1], 2)
      SYSI.InitJoystick(SYSI.joysticks[2], 3)
    },
    InputPushKeyEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(32)

      HEAP32[((event+0)>>2)]= evt.type == 'keydown' ? 0x300 : 0x301; //Uint32 type; ::SDL_KEYDOWN or ::SDL_KEYUP
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds();
      HEAP32[((event+8)>>2)]=0; // windowID
      HEAP32[((event+12)>>2)]=(1 << 2) + (evt.repeat ? 1 : 0); // ::SDL_PRESSED or ::SDL_RELEASED
      
      var key = SDL.lookupKeyCodeForEvent(evt);
      var scan;
      if (key >= 1024) {
        scan = key - 1024;
      } else {
        scan = SDL.scanCodes[key] || key;
      }

      HEAP32[((event+16)>>2)]=scan;
      HEAP32[((event+20)>>2)]=key;
      HEAP32[((event+24)>>2)]=SDL.modState;
      HEAP32[((event+28)>>2)]=0;
      if(evt.type == 'keydown')
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[0], event)
      if(evt.type == 'keyup')
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[1], event)
      stackRestore(stack)
    },
    InputTextInputEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(16)
      
      HEAP32[((event+0)>>2)]= 0x303; //Uint32 type; ::SDL_TEXTINPUT
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds();
      HEAP32[((event+8)>>2)]=0; // windowID
      HEAP32[((event+12)>>2)]=intArrayFromString(evt.key); // The input text
      
      Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[2], event)
      
      stackRestore(stack)
    },
    InputInit: function () {
      // TODO: clear JSEvents.eventHandlers
      var inputInterface = allocate(new Int32Array(20), 'i32', ALLOC_NORMAL);
      Browser.safeCallback(_IN_PushInit)(inputInterface)
      SYSI.inputInterface = []
      for(var ei = 0; ei < 20; ei++) {
        SYSI.inputInterface[ei] = getValue(inputInterface + 4 * ei, 'i32', true)
      }
      window.addEventListener('keydown', SYSI.InputPushKeyEvent, false)
      window.addEventListener('keyup', SYSI.InputPushKeyEvent, false)
      window.addEventListener('keypress', SYSI.InputTextInputEvent, false)
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
		setTimeout(SYSI.InitNippleJoysticks, 100)
	},
	Sys_GLimpSafeInit: function () {
	},
  
}
autoAddDeps(LibrarySysInput, '$SYSI')
mergeInto(LibraryManager.library, LibrarySysInput);
