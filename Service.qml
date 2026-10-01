import QtQuick
import Quickshell
import Quickshell.Io
import "Model.js" as Model

// Keeps the native plugin in step with ~/.config/hypr/hyprripple.lua:
//
//   - creates the file with defaults the first time, so hyprland.lua's
//     require has something to load
//   - pushes the whole file to the plugin at login and on every edit, so a
//     hand edit applies live (no `hyprctl reload`), and a plugin that loaded
//     after the config was parsed still gets the right values
//
// IPC: omarchy-shell ipc call hyprripple trigger | apply
Item {
  id: root

  property var shell: null
  property var manifest: null

  readonly property string luaPath: Quickshell.env("HOME") + "/.config/hypr/hyprripple.lua"

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
    onLoaded: root.apply(text())
    onLoadFailed: setText(Model.renderLua(Model.defaults()))
    onFileChanged: reload()
  }

  // The plugin may load (hyprpm reload -n) after the shell started; re-apply
  // once it's had a moment, then stay event driven.
  Timer {
    interval: 4000
    running: true
    onTriggered: luaFile.reload()
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
