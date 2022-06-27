const DB_STORE_NAME = 'FILE_DATA';

function openDatabase(noWait) {
  if(FS.database) {
    return Promise.resolve(FS.database)
  }
  if(!FS.database && (!FS.open || Date.now() - FS.openTime > 3000)) {
    FS.openTime = Date.now()
    // TODO: make a separate /home store for content to upload submissions to NPM-style packaging system
    // TODO: synchronize saved game states and config files out of /home database
    // TODO: on Native /base is manually configured, manually downloaded, /home is auto-downloaded
    //   on web /base is auto-downloaded and home is manually configured/drag-drop, fix this
    return new Promise(function (resolve) {
      FS.open = indexedDB.open('/base', 22)
      FS.open.onsuccess = function (evt) {
        FS.database = evt.target.result
        resolve(FS.database)
        //if(!Array.from(FS.database.objectStoreNames).includes(DB_STORE_NAME)) {
        //  FS.database.createObjectStore(DB_STORE_NAME)
        //}
      }
      FS.open.onupgradeneeded = function () {
        let fileStore = FS.open.result.createObjectStore(DB_STORE_NAME)
        if (!fileStore.indexNames.contains('timestamp')) {
          fileStore.createIndex('timestamp', 'timestamp', { unique: false });
        }
      }
      FS.open.onerror = function (error) {
        console.error(error)
        resolve(error)
      }
    })
  } else if (!noWait) {
    return new Promise(function (resolve) { 
      let count = 0
      let interval
      interval = setInterval(function () {
        if(FS.database || count == 10) {
          clearInterval(interval)
          openDatabase(true).then(resolve)
        } else {
          count++
        }
      }, 300)
    })
  } else {
    throw new Error('no database')
  }
}


function loadEntry(cursor) {
  if(!cursor) {
    return resolve()
  }
  if(cursor.key.endsWith('default.cfg')) {
    FS.hadDefault = cursor.key
  }
  // already exists on filesystem, 
  //   it must have come with page
  if(FS.virtual[cursor.key]
    && FS.virtual[cursor.key].timestamp 
      > cursor.value.timestamp) {
    // embedded file is newer, start with that
    return cursor.continue()
  }
  FS.virtual[cursor.key] = {
    timestamp: cursor.value.timestamp,
    mode: cursor.value.mode,
    contents: cursor.value.contents
  }
  return cursor.continue()
}



async function readAll() {
  let startTime = Date.now()
  FS.isSyncing = 1
  // FIX FOR "QKEY could not open" ERROR
  FS.virtual['home'] = {
    timestamp: new Date(),
    mode: FS_DIR,
  }
  console.log('sync started at ', new Date())
  let db = await openDatabase()
  let transaction = db.transaction([DB_STORE_NAME], 'readonly')
  let objStore = transaction.objectStore(DB_STORE_NAME)
  let tranCursor = objStore.openCursor()
  await new Promise(function (resolve) {
    tranCursor.onsuccess = function (event) {
      let cursor = event.target.result
      if(!cursor) {
        return resolve()
      }
      loadEntry(cursor)
    }
    tranCursor.onerror = function (error) {
      console.error(error)
      resolve(error)
    }
  })

  transaction.commit()
  let tookTime = Date.now() - startTime

  console.log('sync completed', new Date())
  console.log('sync took', 
    (tookTime > 60 * 1000 ? (Math.floor(tookTime / 1000 / 60) + ' minutes, ') : '')
    + Math.floor(tookTime / 1000) % 60 + ' seconds, '
    + (tookTime % 1000) + ' milliseconds')

  FS.isSyncing = 0

}



function readStore(key) {
  return openDatabase()
  .then(function (db) {
    let transaction = db.transaction([DB_STORE_NAME], 'readwrite');
    let objStore = transaction.objectStore(DB_STORE_NAME);
    return new Promise(function (resolve) {
      let tranCursor = objStore.get(key)
      tranCursor.onsuccess = function (event) {
        resolve(event.target.result)
      }
      tranCursor.onerror = function (error) {
        console.error(error)
        resolve(error)
      }
      transaction.commit()
    })
  })
  .catch(function (e) {})
}

function writeStore(value, key) {
  return openDatabase()
  .then(function (db) {
    let transaction = db.transaction([DB_STORE_NAME], 'readwrite');
    let objStore = transaction.objectStore(DB_STORE_NAME);
    return new Promise(function (resolve) {
      let storeValue  
      if(value === false) {
        storeValue = objStore.delete(key)
      } else {
        storeValue = objStore.put(value, key)
      }
      storeValue.onsuccess = function () {}
      transaction.oncomplete = function (event) {
        resolve(event.target.result)
        //FS.database.close()
        //FS.database = null
        //FS.open = null
      }
      storeValue.onerror = function (error) {
        console.error(error, value, key)
      }
      transaction.commit()
    })
  })
  .catch(function (e) {})
}

function _base64ToArrayBuffer(base64) {
	var binary_string = window.atob(base64);
	var len = binary_string.length;
	var bytes = new Uint8Array(len);
	for (var i = 0; i < len; i++) {
			bytes[i] = binary_string.charCodeAt(i);
	}
	return bytes;
}

// I JUST REALIZED WHY CHROME DEBUGGER WILL PERPUTUALLY HAVE A HARDER
//   AND HARDER TIME WITH WEB APPS LIKE THIS. I SCREWED UP IT'S ABILITY TO OPTIMIZE GRAPH.

async function readPreFS() {
	// TODO: offline download so it saves binary to IndexedDB
	if(typeof window.preFS == 'undefined') {
		throw new Error('No preFS, must load in correct order!')
	}
	let preloadedPaths = Object.keys(window.preFS)
	for(let i = 0; i < preloadedPaths.length; i++) {
		if(preloadedPaths[i].endsWith('_timestamp')) {
			continue
		}
		let newFiletime = window.preFS[
				preloadedPaths[i] + '_timestamp'] 
						|| new Date()
		FS.virtual[preloadedPaths[i]] = {
			timestamp: newFiletime,
			mode: FS_FILE,
			contents: _base64ToArrayBuffer(window.preFS[preloadedPaths[i]])
		}
	}

  /*
  thinking about how deep I want to go on this:
  I had a plan to repackage assets as they are requested from inside the server code
  so that the same process could be used for UDP downloads.

  But now I'm wondering if there's any reason not to do the same thing from the proxy server.

  Where do I get this list from normally? I has a plan to transfer pk3_cache databases from the server. That would store a list of pk3s per mod.

  How do I make the engine automatically pick a mod? #define BASEGAME in q_shared.h
  but FS_GetCurrentGameDir() is not available before the engine loads.
  */
  let listOfFiles = [
    'gfx/2d/bigchars.png',
    'maps/repacked/pak0',
  ]
  for(let i = 0; i < listOfFiles.length; i++) {
    //let result = await readStore(listOfFiles[i])
    //if(!result || (result.mode >> 12) == ST_DIR) {
      responseData = await Com_DL_Begin(listOfFiles[i], 
          listOfFiles[i].replace(/^[^\/]+\//, ''))
      Com_DL_Perform(i == listOfFiles.length - 1 
          ? 'pak0' : listOfFiles[i], listOfFiles[i], responseData)
    //}
  }

}
