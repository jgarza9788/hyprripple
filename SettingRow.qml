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

  signal edited(var value)
  signal committed(var value)

  readonly property bool isBool: item.type === "bool"
  readonly property bool isEnum: item.type === "enum"
  readonly property bool isSlider: item.type === "int" || item.type === "float"
  readonly property bool isColor: item.type === "color"
  readonly property bool isText: item.type === "string"
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

  Column {
    id: labels
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter
    width: Math.round(parent.width * 0.42)
    spacing: Style.spacing.xxs

    Text {
      text: root.item.label
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.subtitle
      font.bold: true
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
  }

  Row {
    id: control
    anchors.left: labels.right
    anchors.leftMargin: Style.spacing.xxl
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.spacing.lg
    layoutDirection: Qt.RightToLeft

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
      width: Math.max(Style.space(90), control.width - Style.space(56) - Style.spacing.lg)
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
      visible: root.isEnum
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
          onAccepted: root.committed(text)
          onEditingFinished: if (text !== "" && text !== String(root.value)) root.committed(text)
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
      visible: root.isText
      width: Style.space(130)
      text: root.isText ? String(root.value) : ""
      foreground: root.foreground
      accent: root.accent
      anchors.verticalCenter: parent.verticalCenter
      onAccepted: root.committed(text)
      onEditingFinished: if (text !== String(root.value)) root.committed(text)
    }
  }
}
