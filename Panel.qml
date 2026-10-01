import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "Model.js" as Model

// HyprRipple settings. Every change is pushed live to the native plugin with
// `hyprctl hyprripple set`, then written to ~/.config/hypr/hyprripple.lua so it
// survives a restart. The Lua file is the only store: hand edits show up here.
//
// Open with: omarchy-shell shell toggle jgarza.hyprripple
Item {
  id: root

  property var shell: null
  property var manifest: null

  readonly property string luaPath: Quickshell.env("HOME") + "/.config/hypr/hyprripple.lua"
  // Per-preset opacity/radius/softness memory, owned by Service.qml; the
  // panel only clears it on "reset everything".
  readonly property string memoryPath: Quickshell.env("HOME") + "/.config/omarchy/jgarza.hyprripple/presets.json"

  property bool opened: false
  property var settings: Model.defaults()
  property var status: null          // `hyprctl hyprripple get`, null = plugin not loaded
  property int sectionIndex: 0
  property int cursorIndex: 0
  property string errorText: ""
  property string statusText: ""
  property bool selfWrite: false

  readonly property string section: Model.SECTIONS[sectionIndex]
  readonly property var rows: Model.rowsFor(section, settings)
  onRowsChanged: if (cursorIndex >= rows.length) cursorIndex = Math.max(0, rows.length - 1)
  readonly property bool pluginLoaded: status !== null

  property color background: Color.menu.background
  property color foreground: Color.menu.text
  property color accent: Color.accent
  property color scrim: Color.menu.scrim
  property string fontFamily: Style.font.menuFamily

  // -------------------------------------------------------------- lifecycle

  function open(payloadJson) {
    root.opened = true
    root.errorText = ""
    luaFile.reload()
    statusProc.running = true
    Qt.callLater(function() { keyCatcher.forceActiveFocus() })
  }

  function close() {
    if (persistTimer.running) persistNow()
    root.opened = false
  }

  function dismiss() {
    if (root.shell && typeof root.shell.hide === "function")
      root.shell.hide((root.manifest && root.manifest.id) || "jgarza.hyprripple")
    else close()
  }

  function toggle() {
    if (root.opened) dismiss()
    else open("{}")
  }

  // ----------------------------------------------------------------- values

  // Reset one value to its default - for opacity/radius/softness that's the
  // current preset's default. A preset reset only restores `style`; the
  // other values keep their own reset buttons.
  function resetValue(key) {
    var item = Model.itemFor(key)
    if (item) setValue(key, Model.defaultFor(key, root.settings), true, true)
  }

  function setValue(key, raw, commit, noPreset) {
    var c = Model.coerce(key, raw)
    if (!c.ok) { root.errorText = c.error; return }
    root.errorText = ""

    // A preset brings its own opacity/softness/duration; apply them as one
    // batch, then fire a ripple so the pick is visible straight away.
    // A preset switch is just `set style`: the plugin announces it and the
    // service swaps in the preset's remembered values and rewrites
    // hyprripple.lua, which this panel then reloads. One writer, no race.
    // Pending tweaks are flushed first so they're what gets remembered.
    if (key === "style" && !noPreset) {
      if (persistTimer.running) persistNow()
      setProc.command = Model.setCommand("style", c.value)
      if (!setProc.running) setProc.running = true
      else root.pending = Object.assign({}, root.pending, { style: c.value })
      previewTimer.restart()
      return
    }

    var next = {}
    for (var k in root.settings) next[k] = root.settings[k]
    next[key] = c.value
    root.settings = next

    push(key, c.value)
    if (commit) persistTimer.restart()
  }

  // One set in flight, newest value queued behind it, so a slider drag
  // can't outrun hyprctl.
  property var pending: ({})
  function push(key, value) {
    if (setProc.running) {
      var p = {}
      for (var k in root.pending) p[k] = root.pending[k]
      p[key] = value
      root.pending = p
      return
    }
    setProc.command = Model.setCommand(key, value)
    setProc.running = true
  }

  function flushPending() {
    var keys = Object.keys(root.pending)
    if (keys.length === 0) return
    var key = keys[0]
    var value = root.pending[key]
    var p = {}
    for (var k in root.pending) if (k !== key) p[k] = root.pending[k]
    root.pending = p
    push(key, value)
  }

  function persistNow() {
    persistTimer.stop()
    var next = Model.renderLua(root.settings)
    if (next === luaFile.text()) return
    root.selfWrite = true
    root.statusText = "Saving…"
    luaFile.setText(next)
  }

  function resetAll() {
    root.settings = Model.defaults()
    memoryFile.setText(Model.renderMemory({}))
    applyAllProc.command = Model.batchCommand(root.settings)
    applyAllProc.running = true
    persistNow()
  }

  // ------------------------------------------------------------- keyboard

  function moveSection(delta) {
    var n = Model.SECTIONS.length
    root.sectionIndex = (root.sectionIndex + delta + n) % n
    root.cursorIndex = 0
  }

  function moveCursor(delta) {
    var n = root.rows.length
    if (n === 0) return
    root.cursorIndex = (root.cursorIndex + delta + n) % n
    rowList.positionViewAtIndex(root.cursorIndex, ListView.Contain)
  }

  function cursorItem() {
    return root.cursorIndex >= 0 && root.cursorIndex < root.rows.length ? root.rows[root.cursorIndex] : null
  }

  function nudgeCursor(direction) {
    var item = cursorItem()
    if (!item) return
    var next = Model.nudge(item, root.settings[item.key], direction)
    if (String(next) !== String(root.settings[item.key])) setValue(item.key, next, true)
  }

  function activateCursor() {
    var item = cursorItem()
    if (!item) return
    if (item.type === "bool") setValue(item.key, root.settings[item.key] !== true, true)
    else if (item.type === "enum" || item.type === "color") nudgeCursor(1)
    else if (item.type === "text" || item.type === "string") {
      var row = rowList.itemAtIndex(root.cursorIndex)
      if (row) row.editField()
    }
  }

  function test() {
    Quickshell.execDetached(["hyprctl", "hyprripple", "trigger"])
  }

  // -------------------------------------------------------------- processes

  Process {
    id: setProc
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var out = String(text || "").trim()
        if (out.indexOf("error") === 0) root.errorText = out
      }
    }
    onExited: Qt.callLater(root.flushPending)
  }

  Process { id: applyAllProc }

  Process {
    id: statusProc
    command: ["hyprctl", "hyprripple", "get"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.status = Model.parseStatus(text)
    }
  }

  // Keep the swatch honest while open (theme switches, color edits).
  Timer {
    interval: 1500
    repeat: true
    running: root.opened
    onTriggered: if (!statusProc.running) statusProc.running = true
  }

  Timer {
    id: previewTimer
    interval: 120
    onTriggered: root.test()
  }

  Timer {
    id: persistTimer
    interval: 450
    onTriggered: root.persistNow()
  }

  Timer {
    interval: 1600
    running: root.statusText === "Saved"
    onTriggered: root.statusText = ""
  }

  FileView {
    id: memoryFile
    path: root.memoryPath
    atomicWrites: true
    printErrors: false
    onSaveFailed: root.errorText = "Could not write " + root.memoryPath
  }

  FileView {
    id: luaFile
    path: root.luaPath
    atomicWrites: true
    printErrors: false
    watchChanges: true
    onLoaded: root.settings = Model.parseLua(text())
    onLoadFailed: root.settings = Model.defaults()
    onSaved: { root.selfWrite = false; root.statusText = "Saved" }
    onSaveFailed: { root.selfWrite = false; root.statusText = ""; root.errorText = "Could not write ~/.config/hypr/hyprripple.lua" }
    // Adopt a hand edit, but never mid-drag.
    onFileChanged: {
      if (root.selfWrite || persistTimer.running) return
      reload()
    }
  }

  // --------------------------------------------------------------------- UI

  PanelWindow {
    id: window
    visible: root.opened
    anchors { top: true; bottom: true; left: true; right: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "hyprripple"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

    Rectangle {
      anchors.fill: parent
      color: root.scrim
      MouseArea { anchors.fill: parent; onClicked: root.dismiss() }
    }

    BorderSurface {
      id: card
      anchors.centerIn: parent
      width: Math.min(Style.space(720), window.width - Style.gapsOut * 4)
      height: Math.min(Style.space(600), window.height - Style.gapsOut * 4)
      radius: Style.cornerRadius
      color: root.background
      borderSpec: Border.surfaceSpec("menu", "border", Color.menu.border, Math.max(1, Style.space(2)))
      padding: Style.spacing.panelPadding

      MouseArea { anchors.fill: parent; onClicked: {} }

      Item {
        id: keyCatcher
        anchors.fill: parent
        focus: true
        // Arrows or hjkl (unmodified, so Ctrl+L etc. stay with the
        // compositor). Text fields take their own keys while focused; Esc or
        // Enter hands focus back here.
        Keys.onPressed: function(event) {
          var vim = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
          if (event.key === Qt.Key_Escape) root.dismiss()
          else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
            root.moveSection(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1)
          else if (event.key === Qt.Key_Down || (vim && event.key === Qt.Key_J)) root.moveCursor(1)
          else if (event.key === Qt.Key_Up || (vim && event.key === Qt.Key_K)) root.moveCursor(-1)
          else if (event.key === Qt.Key_Right || (vim && event.key === Qt.Key_L)) root.nudgeCursor(1)
          else if (event.key === Qt.Key_Left || (vim && event.key === Qt.Key_H)) root.nudgeCursor(-1)
          else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) root.activateCursor()
          else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            var item = root.cursorItem()
            if (item) root.resetValue(item.key)
          }
          else if (vim && event.key === Qt.Key_T) root.test()
          else return
          event.accepted = true
        }
      }

      ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: card.contentTopInset
        anchors.rightMargin: card.contentRightInset
        anchors.bottomMargin: card.contentBottomInset
        anchors.leftMargin: card.contentLeftInset
        spacing: Style.spacing.panelGap

        // Header
        Item {
          Layout.fillWidth: true
          Layout.preferredHeight: Math.max(titleBlock.implicitHeight, actions.implicitHeight)

          Column {
            id: titleBlock
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing.xxs

            Text {
              text: "HyprRipple"
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.heading
              font.bold: true
            }

            Text {
              text: root.pluginLoaded
                ? "~/.config/hypr/hyprripple.lua · " + root.status.resolved_color
                : "Native plugin not loaded. See README: hyprpm add, then hyprpm enable hyprripple"
              color: root.pluginLoaded ? Qt.darker(root.foreground, 1.6) : Color.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
            }
          }

          Row {
            id: actions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing.lg

            Text {
              text: root.statusText
              visible: text !== ""
              color: Qt.darker(root.foreground, 1.4)
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              anchors.verticalCenter: parent.verticalCenter
            }

            Button {
              text: "Test"
              iconText: "󰐊"
              tooltipText: "Ripple at the cursor (T)"
              foreground: root.foreground
              accent: root.accent
              bordered: true
              enabled: root.pluginLoaded
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.test()
            }

            PanelActionButton {
              iconText: "󰕌"
              tooltipText: "Reset everything to defaults"
              foreground: root.foreground
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.resetAll()
            }

            PanelActionButton {
              iconText: "󰅖"
              tooltipText: "Close (Esc)"
              foreground: root.foreground
              anchors.verticalCenter: parent.verticalCenter
              onClicked: root.dismiss()
            }
          }
        }

        ButtonGroup {
          options: Model.SECTIONS
          value: root.section
          foreground: root.foreground
          accent: root.accent
          focusable: false
          onChanged: function(v) { root.sectionIndex = Math.max(0, Model.SECTIONS.indexOf(v)); root.cursorIndex = 0 }
        }

        PanelSeparator { Layout.fillWidth: true }

        ListView {
          id: rowList
          Layout.fillWidth: true
          Layout.fillHeight: true
          clip: true
          spacing: Style.spacing.xs
          model: root.rows
          boundsBehavior: Flickable.StopAtBounds

          delegate: SettingRow {
            required property var modelData
            required property int index
            width: ListView.view.width
            item: modelData
            value: root.settings[modelData.key]
            foreground: root.foreground
            accent: root.accent
            fontFamily: root.fontFamily
            swatch: root.pluginLoaded ? root.status.resolved_color : root.accent
            defaultValue: Model.defaultFor(modelData.key, root.settings)
            opacity: (modelData.key === "enabled" || root.settings.enabled) ? 1 : 0.45
            onEdited: function(v) { root.setValue(modelData.key, v, false) }
            onCommitted: function(v) { root.setValue(modelData.key, v, true) }
            onReset: root.resetValue(modelData.key)
            hasCursor: index === root.cursorIndex
            onFocusRequested: root.cursorIndex = index
            onDoneEditing: keyCatcher.forceActiveFocus()
          }
        }

        Text {
          Layout.fillWidth: true
          text: root.errorText !== "" ? root.errorText : "Tab: section · ↑↓/jk: row · ←→/hl: change · Enter: toggle · ⌫: reset row · T: test · Esc: close"
          color: root.errorText !== "" ? Color.urgent : Qt.darker(root.foreground, 1.6)
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          wrapMode: Text.WordWrap
        }
      }
    }
  }
}
