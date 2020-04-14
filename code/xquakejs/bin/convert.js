var {ufs} = require('unionfs')
var path = require('path')
var {mkdirpSync} = require('../bin/compress.js')
var {execSync} = require('child_process');
var {
  findTypes, fileTypes, sourceTypes,
  audioTypes, imageTypes, findTypes,
} = require('../bin/repack-whitelist.js')

function chext(file, ext) {
  return file.replace(new RegExp('\\' + path.extname(file) + '$'), ext)
}

function chroot(file, root, output) {
  // TODO: example of defensive programming
  if(file.substr(0, root.length).localeCompare(root) !== 0)
    throw new Error(`File not in root ${file}`)
  return path.join(output, file.substr(root.length))
}

async function convertNonAlpha(inFile, project, output, noOverwrite) {
  var outFile
  var alphaCmd = '';
  outFile = chroot(chext(inFile, '.png'), project, output)
  if(noOverwrite && ufs.existsSync(outFile)) return outFile
  outFile = chroot(chext(inFile, '.jpg'), project, output)
  if(noOverwrite && ufs.existsSync(outFile)) return outFile
  try {
    alphaCmd = execSync(`identify -format '%[opaque]' "${inFile}"`, {stdio : 'pipe'}).toString('utf-8')
  } catch (e) {
    console.error(e.message, (e.output || '').toString('utf-8').substr(0, 1000))
  }
  // if it is alpha
  if(alphaCmd.localeCompare('False', 'en', { sensitivity: 'base' }) === 0) {
    // convert everything else to png to support transparency
    outFile = chroot(chext(inFile, '.png'), project, output)
  } else {
    // if a jpg already exists, use that file for convert
    if(ufs.existsSync(chext(inFile, '.jpg'))) {
        inFile = chext(inFile, '.jpg')
    }
    // transfer low quality jpeg instead
    outFile = chroot(chext(inFile, '.jpg'), project, output)
  }
  mkdirpSync(path.dirname(outFile))
  if(noOverwrite && ufs.existsSync(outFile)) return outFile
  // convert, baseq3 already includes jpg
  try {
    execSync(`convert -strip -interlace Plane -sampling-factor 4:2:0 -quality 50% "${inFile}" "${outFile}"`, {stdio : 'pipe'})
  } catch (e) {
    console.error(e.message, (e.output || '').toString('utf-8').substr(0, 1000))
  }
  return outFile
}

async function convertAudio(inFile, project, output, noOverwrite) {
  var outFile = chroot(chext(inFile, '.ogg'), project, output)
  mkdirpSync(path.dirname(outFile))
  if(noOverwrite && ufs.existsSync(outFile)) return outFile
  try {
    execSync(`oggenc --quiet "${inFile}" -n "${outFile}"`, {stdio : 'pipe'})
  } catch (e) {
    console.error(e.message, (e.output || '').toString('utf-8').substr(0, 1000))
  }
  return outFile
}

async function convertGameFiles(gs, project, outConverted, noOverwrite, progress) {  
  await progress([
    [2, false],
    [1, 0, 3, `Converting images`]
  ])  
  var images = findTypes(imageTypes, gs.everything)
  for(var j = 0; j < images.length; j++) {
    await progress([[2, j, images.length, chroot(images[j], project, '')]])
    if(!ufs.existsSync(images[j])) continue
    var newFile = await convertNonAlpha(images[j], project, outConverted, noOverwrite)
    Object.keys(gs.ordered).forEach(k => {
      var index = gs.ordered[k].indexOf(images[j])
      if(index > -1) gs.ordered[k][index] = newFile
    })
  }
  
  await progress([
    [2, false],
    [1, 1, 3, `Converting audio`]
  ])
  var audio = findTypes(audioTypes, gs.everything)
  for(var j = 0; j < audio.length; j++) {
    await progress([[2, j, audio.length, chroot(audio[j], project, '')]])
    if(!ufs.existsSync(audio[j])) continue
    var newFile = await convertAudio(audio[j], project, outConverted, noOverwrite)
    Object.keys(gs.ordered).forEach(k => {
      var index = gs.ordered[k].indexOf(audio[j])
      if(index > -1) gs.ordered[k][index] = newFile
    })
  }
  
  // TODO: convert videos to different qualities like YouTubes
  
  await progress([
    [2, false],
    [1, 2, 3, `Copying known files`]
  ])
  var known = gs.everything
    .filter(f => !images.includes(f) && !audio.includes(f))
  for(var j = 0; j < known.length; j++) {
    await progress([[2, j, known.length, chroot(known[j], project, '')]])
    var newFile = chroot(known[j], project, outConverted)
    if(!ufs.existsSync(known[j])) continue
    mkdirpSync(path.dirname(newFile))
    if(!noOverwrite || !ufs.existsSync(newFile)) {
      ufs.copyFileSync(known[j], newFile)
    }
    Object.keys(gs.ordered).forEach(k => {
      var index = gs.ordered[k].indexOf(known[j])
      if(index > -1) gs.ordered[k][index] = newFile
    })
  }
  
  await progress([[2, false]])
}

module.exports = {
  convertNonAlpha,
  convertAudio,
  convertGameFiles
}
