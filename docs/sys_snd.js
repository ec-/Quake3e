const AUDIO_DRIVER = "Web Audio"

let soundEffects = {}


let SND = {
  SNDDMA_Init: function () {
    if(HEAPU32[first_click >> 2]) {
      return 0
    }
    HEAPU32[(dma >> 2) + 0] = 2
    HEAPU32[(dma >> 2) + 1] = 16384
    HEAPU32[(dma >> 2) + 2] = 16384 / 2
    HEAPU32[(dma >> 2) + 3] = 1
    HEAPU32[(dma >> 2) + 4] = 32
    HEAPU32[(dma >> 2) + 5] = 1
    HEAPU32[(dma >> 2) + 6] = 44100
    //HEAPU32[(dma >> 2) + 7] = Z_Malloc(16384 * 200)
    HEAPU32[(dma >> 2) + 8] = Z_Malloc(AUDIO_DRIVER.length + 1)
    stringToAddress(AUDIO_DRIVER, HEAPU32[(dma >> 2) + 7])
    return 1
  },
  SNDDMA_Shutdown: function () {
    if(HEAPU32[(dma >> 2) + 8]) {
      //Z_Free(HEAPU32[(dma >> 2) + 7])
      Z_Free(HEAPU32[(dma >> 2) + 8])
      //HEAPU32[(dma >> 2) + 7] = 0
      HEAPU32[(dma >> 2) + 8] = 0
    }
    HEAPU32[first_click >> 2] = 1
  },
  SNDDMA_BeginPainting: function () {},
  SNDDMA_Submit: function () {},
  SNDDMA_GetDMAPos: function () {
    return Sys_Milliseconds()
  },



  S_CodecCloseStream: function () {},
  S_CodecOpenStream: function () {},
  S_CodecReadStream: function () {},
  S_CodecLoad: function (name, info) {
    let filenameStr = addressToString(name)
    if(!filenameStr.endsWith('.ogg')) {
      filenameStr = filenameStr.replace(/\..*?$/, '.ogg')
    }
    let existing = Object.values(soundEffects)
    for(let i = 0; i < existing.length; i++) {
      if(existing[i][0].name == filenameStr) {
        soundEffects[name] = existing[i]
        return 1
      }
    }

    let buf = Z_Malloc(8) // pointer to pointer
    HEAPU32[buf >> 2] = 0
    
    if ((length = FS_ReadFile(stringToAddress(filenameStr), buf)) > 0 && HEAPU32[buf >> 2] > 0) {
      let thisAudio = document.createElement('AUDIO')
      thisAudio.onload = function (evt) {
        debugger
        //HEAP32[(evt.target.address - 4 * 4) >> 2] = evt.target.width
        //HEAP32[(evt.target.address - 3 * 4) >> 2] = evt.target.height
        //R_FinishImage3(evt.target.address - 7 * 4, 0x1908 /* GL_RGBA */, 0)
      }
      let audioView = Array.from(HEAPU8.slice(HEAPU32[buf >> 2], HEAPU32[buf >> 2] + length))
      let utfEncoded = audioView.map(function (c) { return String.fromCharCode(c) }).join('')
      thisAudio.src = 'data:audio/ogg;base64,' + btoa(utfEncoded)
      thisAudio.name = filenameStr
      thisAudio.address = name - 8
      soundEffects[name] = [thisAudio]
      HEAPU32[(info >> 2) + 4] = length
      FS_FreeFile(HEAPU32[buf >> 2])
      Z_Free(buf)
      return 1
    }
    Z_Free(buf)
    return 0
  },
  S_CodecInit: function () {},
  S_CodecShutdown: function () {},
  S_PaintChannel: S_PaintChannel,
}


function S_PaintChannel(ch, sfx) {
  if(HEAPU32[first_click >> 2]) {
    return
  }
  let name = sfx+28
  if(!soundEffects[name]) {
    return
  }
  let now = Date.now()
  for(let i = 0; i < soundEffects[name].length; i++) {
    if(!soundEffects[name][i].lastPlayed
      || soundEffects[name].lastPlayed + soundEffects[name].duration < now) {
      soundEffects[name][i].lastPlayed = now
      soundEffects[name][i].play()
      return
    }
  }
  let newInstance = soundEffects[name][0].cloneNode()
  soundEffects[name].push(newInstance)
  newInstance.lastPlayed = Date.now()
  newInstance.play()
}