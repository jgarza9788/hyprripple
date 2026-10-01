import QtQuick
import qs.Commons
import qs.Ui

// One setting: label + description on the left, its control on the right.
// `edited` fires continuously while a slider drags (live preview only);
// `committed` is a value worth writing to hyprripple.lua.
Item {
  id: root

  required property var item
  property var value
  property color foreground: Color.foreground
  property color accent: Color.accent
  property color swatch: "transparent"   // resolved ink color, color rows only
  property string fontFamily: Style.font.family
  property bool hasCursor: false            // keyboard cursor is on this row

  signal edited(var value)
  signal committed(var value)
  signal reset()
  signal focusRequested()
  signal doneEditing()     // a text field gave up focus (Enter / Esc)

  // Keyboard: Enter on a text or color row edits its field.
  function editField() {
    if (isText) textField.forceActiveFocus()
    else if (isColor) colorField.forceActiveFocus()
  }

  // Differs from the schema default -> show the reset button and the pip.
  // What reset restores; the panel passes the current preset's default for
  // per-preset values.
  property var defaultValue: item.fallback
  readonly property bool modified: String(value) !== String(defaultValue)

  readonly property bool isBool: item.type === "bool"
  readonly property bool isEnum: item.type === "enum"
  // Too many choices to sit beside the label: chips get their own line.
  readonly property bool wideEnum: isEnum && (item.options || []).length > 4
  readonly property bool isSlider: item.type === "int" || item.type === "float"
  readonly property bool isColor: item.type === "color"
  readonly property bool isText: item.type === "string" || item.type === "text"
  readonly property bool isPresetColor: {
    if (!isColor) return false
    for (var i = 0; i < item.options.length; i++)
      if (item.options[i].value === String(value)) return true
    return false
  }

  function formatted() {
    var n = Number(value)
    if (!isFinite(n)) return "-"
    var t = item.type === "int" ? String(Math.round(n)) : n.toFixed(item.decimals === undefined ? 2 : item.decimals)
    return t + (item.unit || "")
  }

  implicitHeight: Math.max(labels.implicitHeight, control.implicitHeight) + Style.spacing.xl

  // Keyboard cursor highlight; hovering a row moves the cursor to it.
  Rectangle {
    anchors.fill: parent
    anchors.leftMargin: -Style.spacing.md
    anchors.rightMargin: -Style.spacing.md
    radius: Style.cornerRadius
    color: root.hasCursor ? Style.hoverFillFor(root.foreground, root.accent) : "transparent"
    Behavior on color { ColorAnimation { duration: 100 } }
  }

  MouseArea {
    anchors.fill: parent
    acceptedButtons: Qt.NoButton
    hoverEnabled: true
    onEntered: root.focusRequested()
  }

  Column {
    id: labels
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter
    width: root.wideEnum ? parent.width - resetSlot.width - Style.spacing.lg : Math.round(parent.width * 0.42)
    spacing: Style.spacing.xxs

    Row {
      spacing: Style.spacing.sm

      Text {
        text: root.item.label
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.subtitle
        font.bold: true
      }

      // Filled pip = changed from the default.
      Rectangle {
        width: Style.space(5)
        height: width
        radius: width / 2
        color: root.accent
        opacity: root.modified ? 1 : 0
        anchors.verticalCenter: parent.verticalCenter
        Behavior on opacity { NumberAnimation { duration: 120 } }
      }
    }

    Text {
      text: root.item.description || ""
      visible: text !== ""
      width: parent.width
      wrapMode: Text.WordWrap
      color: Qt.darker(root.foreground, 1.55)
      font.family: root.fontFamily
      font.pixelSize: Style.font.caption
    }

    // Wide pickers (Preset) sit under their label, full width.
    Item {
      visible: root.wideEnum
      width: 1
      height: Style.spacing.sm
    }

    ButtonGroup {
      visible: root.wideEnum
      options: root.wideEnum ? root.item.options : []
      value: String(root.value)
      foreground: root.foreground
      accent: root.accent
      focusable: false
      onChanged: function(v) { root.committed(v) }
    }
  }

  Row {
    id: control
    anchors.left: labels.right
    anchors.leftMargin: Style.spacing.xxl
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.spacing.lg
    layoutDirection: Qt.RightToLeft

    // Reserved even when hidden so controls stay aligned down the list.
    Item {
      id: resetSlot
      width: resetButton.width
      height: resetButton.height
      anchors.verticalCenter: parent.verticalCenter

      PanelActionButton {
        id: resetButton
        iconText: "󰕌"
        tooltipText: "Reset to default (" + String(root.defaultValue) + ")"
        foreground: root.foreground
        opacity: root.modified ? 1 : 0
        enabled: root.modified
        onClicked: root.reset()
        Behavior on opacity { NumberAnimation { duration: 120 } }
      }
    }

    ToggleSwitch {
      visible: root.isBool
      checked: root.value === true
      foreground: root.foreground
      accent: root.accent
      anchors.verticalCenter: parent.verticalCenter
      onToggled: root.committed(root.value !== true)
    }

    Text {
      visible: root.isSlider
      text: root.formatted()
      width: Style.space(56)
      horizontalAlignment: Text.AlignRight
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
      anchors.verticalCenter: parent.verticalCenter
    }

    PanelSlider {
      visible: root.isSlider
      width: Math.max(Style.space(90), control.width - resetSlot.width - Style.space(56) - Style.spacing.lg * 2)
      minimum: root.isSlider ? root.item.min : 0
      maximum: root.isSlider ? root.item.max : 1
      step: root.item.step === undefined ? 1 : root.item.step
      integer: root.item.type === "int"
      value: root.isSlider ? Number(root.value) : 0
      fillColor: root.accent
      knobColor: root.accent
      anchors.verticalCenter: parent.verticalCenter
      onMoved: function(v) { root.edited(v) }
      onReleased: function(v) { root.committed(v) }
    }

    ButtonGroup {
      visible: root.isEnum && !root.wideEnum
      options: root.isEnum ? root.item.options : []
      value: String(root.value)
      foreground: root.foreground
      accent: root.accent
      focusable: false
      anchors.verticalCenter: parent.verticalCenter
      onChanged: function(v) { root.committed(v) }
    }

    // Color: live swatch, a free-form field, and theme presets.
    Column {
      visible: root.isColor
      spacing: Style.spacing.sm
      anchors.verticalCenter: parent.verticalCenter

      Row {
        spacing: Style.spacing.md
        layoutDirection: Qt.RightToLeft

        Rectangle {
          width: Style.space(26)
          height: width
          radius: width / 2
          color: root.swatch
          border.width: 1
          border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.35)
          anchors.verticalCenter: parent.verticalCenter
        }

        TextField {
          id: colorField
          width: Style.space(130)
          text: root.isPresetColor ? "" : String(root.value)
          placeholderText: "#rrggbb"
          foreground: root.foreground
          accent: root.accent
          onAccepted: { root.committed(text); root.doneEditing() }
          onEditingFinished: if (text !== "" && text !== String(root.value)) root.committed(text)
          Keys.onEscapePressed: root.doneEditing()
        }
      }

      ButtonGroup {
        options: root.isColor ? root.item.options : []
        value: root.isPresetColor ? String(root.value) : ""
        foreground: root.foreground
        accent: root.accent
        focusable: false
        onChanged: function(v) { root.committed(v) }
      }
    }

    TextField {
      id: textField
      visible: root.isText
      width: root.item.type === "text" ? Style.space(220) : Style.space(130)
      text: root.isText ? String(root.value) : ""
      placeholderText: root.item.placeholder || ""
      foreground: root.foreground
      accent: root.accent
      anchors.verticalCenter: parent.verticalCenter
      onAccepted: { root.committed(text); root.doneEditing() }
      onEditingFinished: if (text !== String(root.value)) root.committed(text)
      Keys.onEscapePressed: root.doneEditing()
    }
  }
}
