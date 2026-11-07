import QtQuick

// A menu's, sheet's or dialog's background: `fill`, rounded as asked, with
// square corners along the bottom for a sheet that sits on the bottom edge.
Rectangle {
    id: panel
    property color fill: Theme.surface
    // A sheet along the bottom edge: square corners there.
    property bool squareBottom: false

    color: fill
    bottomLeftRadius: squareBottom ? 0 : radius
    bottomRightRadius: squareBottom ? 0 : radius
}
