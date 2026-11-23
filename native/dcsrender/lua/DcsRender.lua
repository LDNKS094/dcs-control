-- Install beside DcsRender.dll in Saved Games/DCS/Scripts/Hooks.
local dll = lfs.writedir() .. [[Scripts\Hooks\DcsRender.dll]]
local function native(name)
  return assert(package.loadlib(dll, name))
end
native('DcsRender_Initialize')()
local loadBegin = native('DcsRender_LoadBegin')
local loadEnd = native('DcsRender_LoadEnd')
local simStart = native('DcsRender_SimulationStart')
local firstFrame = native('DcsRender_FirstFrame')
local simStop = native('DcsRender_SimulationStop')
local pendingFrame = false
local generation = 0
local function note(event)
  log.write('DcsRender', log.INFO, 'scene=' .. generation .. ' event=' .. event)
end
local callbacks = {}
function callbacks.onMissionLoadBegin()
  pendingFrame = false
  generation = generation + 1
  loadBegin()
  note('load-begin')
end
function callbacks.onMissionLoadEnd()
  loadEnd()
  note('load-end')
end
function callbacks.onSimulationStart()
  simStart()
  pendingFrame = true
  note('simulation-start')
end
function callbacks.onSimulationFrame()
  if pendingFrame then
    pendingFrame = false
    firstFrame()
    note('first-simulation-frame')
  end
end
function callbacks.onSimulationStop()
  pendingFrame = false
  simStop()
  note('simulation-stop')
end
DCS.setUserCallbacks(callbacks)
note('callbacks-installed')
