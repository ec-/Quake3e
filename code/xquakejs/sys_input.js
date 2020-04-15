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
    InputPushTextEvent: function (evt) {
      var stack = stackSave()
      var event = stackAlloc(16)
      var text = intArrayFromString(evt.key)
      HEAP32[((event+0)>>2)]= 0x303; //Uint32 type; ::SDL_TEXTINPUT
      HEAP32[((event+4)>>2)]=_Sys_Milliseconds();
      HEAP32[((event+8)>>2)]=0; // windowID
      var cStr = intArrayFromString(String.fromCharCode(evt.charCode));
      for (var i = 0; i < cStr.length; ++i) {
        HEAP8[((event+(12 + i))>>0)]=cStr[i];
      }
      Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[2], event)
      stackRestore(stack)
    },
    InputPushMouseEvent: function (evt) {
      debugger
      var stack = stackSave()
      var event = stackAlloc(32)
      if (evt.type != 'mousemove') {
        var down = evt.type === 'mousedown';
        HEAP32[((event+0)>>2)]=down ? 0x401 : 0x402;
        HEAP32[((event+4)>>2)]=0; // timestamp
        HEAP32[((event+8)>>2)]=0; // windowid
        HEAP32[((event+12)>>2)]=0; // mouseid
        HEAP32[((event+16)>>2)]=evt.button+1; // DOM buttons are 0-2, SDL 1-3
        HEAP32[((event+17)>>2)]=down ? 1 : 0;
        // padding
        HEAP32[((event+20)>>2)]=Browser.mouseX;
        HEAP32[((event+24)>>2)]=Browser.mouseY;
      } else {
        HEAP32[((event+0)>>2)]=0x400;
        HEAP32[((event+4)>>2)]=0;
        HEAP32[((event+8)>>2)]=0;
        HEAP32[((event+12)>>2)]=0;
        HEAP32[((event+16)>>2)]=SDL.buttonState;
        HEAP32[((event+20)>>2)]=Browser.mouseX;
        HEAP32[((event+24)>>2)]=Browser.mouseY;
        HEAP32[((event+28)>>2)]=Browser.mouseMovementX;
        HEAP32[((event+32)>>2)]=Browser.mouseMovementY;
      }
      if (evt.type == 'mousemove')
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[3], event)
      else
        Browser.safeCallback(_IN_PushEvent)(SYSI.inputInterface[4], event)
      stackRestore(stack)
    },
    InputPushTouchEvent: function (evt) {
      /*
      var touch = event.touch;
      if (!Browser.touches[touch.identifier]) break;
      var w = Module['canvas'].width;
      var h = Module['canvas'].height;
      var x = Browser.touches[touch.identifier].x / w;
      var y = Browser.touches[touch.identifier].y / h;
      var lx = Browser.lastTouches[touch.identifier].x / w;
      var ly = Browser.lastTouches[touch.identifier].y / h;
      var dx = x - lx;
      var dy = y - ly;
      if (touch['deviceID'] === undefined) touch.deviceID = SDL.TOUCH_DEFAULT_ID;
      if (dx === 0 && dy === 0 && event.type === 'touchmove') return false; // don't send these if nothing happened
      HEAP32[(event>>2)]=SDL.DOMEventToSDLEvent[event.type];
      HEAP32[((event+(4))>>2)]=_SDL_GetTicks();
      (tempI64 = [touch.deviceID>>>0,(tempDouble=touch.deviceID,(+(Math_abs(tempDouble))) >= 1.0 ? (tempDouble > 0.0 ? ((Math_min((+(Math_floor((tempDouble)/4294967296.0))), 4294967295.0))|0)>>>0 : (~~((+(Math_ceil((tempDouble - +(((~~(tempDouble)))>>>0))/4294967296.0)))))>>>0) : 0)],HEAP32[((event+(8))>>2)]=tempI64[0],HEAP32[((event+(12))>>2)]=tempI64[1]);
      (tempI64 = [touch.identifier>>>0,(tempDouble=touch.identifier,(+(Math_abs(tempDouble))) >= 1.0 ? (tempDouble > 0.0 ? ((Math_min((+(Math_floor((tempDouble)/4294967296.0))), 4294967295.0))|0)>>>0 : (~~((+(Math_ceil((tempDouble - +(((~~(tempDouble)))>>>0))/4294967296.0)))))>>>0) : 0)],HEAP32[((event+(16))>>2)]=tempI64[0],HEAP32[((event+(20))>>2)]=tempI64[1]);
      HEAPF32[((event+(24))>>2)]=x;
      HEAPF32[((event+(28))>>2)]=y;
      HEAPF32[((event+(32))>>2)]=dx;
      HEAPF32[((event+(36))>>2)]=dy;
      if (touch.force !== undefined) {
        HEAPF32[((event+(40))>>2)]=touch.force;
      } else { // No pressure data, send a digital 0/1 pressure.
        HEAPF32[((event+(40))>>2)]=event.type == "touchend" ? 0 : 1;
      }
      */
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
      window.addEventListener('keypress', SYSI.InputPushTextEvent, false)
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
