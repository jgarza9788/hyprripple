import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import "Model.js" as Model

// Keeps the native plugin in step with ~/.config/hypr/hyprripple.lua:
//
//   - creates the file with defaults the first time, so hyprland.lua's
//     require has something to load
//   - pushes the whole file to the plugin at login and on every edit, so a
//     hand edit applies live (no `hyprctl reload`), and a plugin that loaded
//     after the config was parsed still gets the right values
//   - owns per-preset memory: whenever the style changes - from the panel,
//     `hyprctl hyprripple set style`, or Lua - the plugin announces
//     hyprripplestyle>>old,new and this swaps in the new preset's remembered
//     opacity/radius/softness, saving the old one's
//   - says so once if the plugin is enabled in hyprpm but not loading
//     (usually: Hyprland updated, run `hyprpm update`)
//
// IPC: omarchy-shell ipc call hyprripple trigger | apply
Item {
  id: root

  property var shell: null
  property var manifest: null

  readonly property string home: Quickshell.env("HOME")
  readonly property string luaPath: home + "/.config/hypr/hyprripple.lua"
  readonly property string stateDir: home + "/.config/omarchy/jgarza.hyprripple"
  readonly property string memoryPath: stateDir + "/presets.json"

  property bool selfWrite: false
  property var switchQueue: []       // [{ from, to }], handled one at a time

  function apply(text) {
    applyProc.command = Model.batchCommand(Model.parseLua(text))
    if (!applyProc.running) applyProc.running = true
  }

  Process { id: applyProc }

  FileView {
    id: luaFile
    path: root.luaPath
    printErrors: false
    watchChanges: true
    atomicWrites: true
    onLoaded: root.apply(text())
    onLoadFailed: setText(Model.renderLua(Model.defaults()))
    onSaved: root.selfWrite = false
    onFileChanged: {
      // our own write already went to the plugin as a batch
      if (root.selfWrite) return
      reload()
    }
  }

  // ------------------------------------------------------- preset memory

  Process {
    running: true
    command: ["mkdir", "-p", root.stateDir]
  }

  FileView {
    id: memoryFile
    path: root.memoryPath
    printErrors: false
    watchChanges: true
    atomicWrites: true
    onFileChanged: reload()
  }

  Connections {
    target: Hyprland
    function onRawEvent(event) {
      if (event.name !== "hyprripplestyle") return
      var change = Model.parseStyleEvent(event.data)
      if (!change) return
      root.switchQueue = root.switchQueue.concat([change])
      root.nextSwitch()
    }
  }

  function nextSwitch() {
    if (liveProc.running || root.switchQueue.length === 0) return
    liveProc.running = true
  }

  // The plugin has already flipped `style` but still holds the old preset's
  // values: read them, remember them, then load the new preset's.
  Process {
    id: liveProc
    command: ["hyprctl", "hyprripple", "get"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var change = root.switchQueue[0]
        root.switchQueue = root.switchQueue.slice(1)
        var live = Model.parseStatus(text)
        if (change && live) {
          var result = Model.switchFromLive(live, change.from, change.to, Model.parseMemory(memoryFile.text()))
          switchApply.command = Model.batchCommand(result.settings)
          switchApply.running = true
          memoryFile.setText(Model.renderMemory(result.memory))
          root.selfWrite = true
          luaFile.setText(Model.renderLua(result.settings))
        }
        Qt.callLater(root.nextSwitch)
      }
    }
  }

  Process { id: switchApply }

  // ------------------------------------------------------- health check

  // The plugin may load (hyprpm reload -n) after the shell started; re-apply
  // once it's had a moment, then check it actually loaded.
  Timer {
    interval: 4000
    running: true
    onTriggered: { luaFile.reload(); healthProc.running = true }
  }

  property bool warned: false

  Process {
    id: healthProc
    command: ["hyprctl", "hyprripple", "get"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: if (!Model.parseStatus(text)) hyprpmProc.running = true
    }
  }

  // Not loaded: only worth a word if hyprpm has it enabled (a stale build
  // after a Hyprland update) - not if the user simply never installed it.
  Process {
    id: hyprpmProc
    command: ["hyprpm", "list"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var out = String(text || "").replace(/\x1b\[[0-9;]*m/g, "")
        if (root.warned || !/Plugin hyprripple[\s\S]*?enabled:\s*true/.test(out)) return
        root.warned = true
        Quickshell.execDetached(["notify-send", "-a", "HyprRipple", "HyprRipple isn't loading",
                                 "Hyprland was probably updated. Rebuild the plugin with: hyprpm update"])
      }
    }
  }

  IpcHandler {
    target: "hyprripple"

    function trigger(): string {
      Quickshell.execDetached(["hyprctl", "hyprripple", "trigger"])
      return "ok"
    }

    function apply(): string {
      luaFile.reload()
      return "ok"
    }
  }
}
