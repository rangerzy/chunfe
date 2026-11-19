import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property color color: "#1b55f5"
    property real diameter: 50
    property int duration: 240
    property real stretch: 1
    readonly property bool running: flight.running
    readonly property real progress: motion.phase
    readonly property real centerX: motion.fromX + (motion.toX - motion.fromX) * easedProgress
    readonly property real centerY: motion.fromY + (motion.toY - motion.fromY) * easedProgress
    readonly property real targetX: motion.toX
    readonly property real targetY: motion.toY
    readonly property real deformation: tailDeformation()
    readonly property int motionRevision: motion.revision
    readonly property real easedProgress: 1 - Math.pow(1 - progress, 3)
    readonly property real bodyScale: motion.initialScale + (1 - motion.initialScale) * easedProgress
    readonly property real turnProgress: motion.collapsing ? 0 : Math.min(1, motion.tailPhase / 0.22)
    readonly property real angle: motion.fromAngle + (motion.toAngle - motion.fromAngle)
                                 * turnProgress * turnProgress * (3 - 2 * turnProgress)
    signal settled()

    visible: false

    // The head accepts every target immediately. A separate tail clock lets an
    // active reversal finish collapsing without subsequent clicks restarting it.
    QtObject {
        id: motion
        property real phase: 1
        property real tailPhase: 1
        property bool collapsing: false
        property real fromX: 0
        property real fromY: 0
        property real toX: 0
        property real toY: 0
        property real fromAngle: 0
        property real toAngle: 0
        property real pendingAngle: 0
        property real initialDeformation: 0
        property real peak: 0
        property real initialScale: 1
        property int time: 240
        property int tailTime: 240
        property int revision: 0
    }

    onCenterXChanged: motion.revision++
    onCenterYChanged: motion.revision++
    onDeformationChanged: motion.revision++
    onAngleChanged: motion.revision++
    onBodyScaleChanged: motion.revision++
    onDiameterChanged: motion.revision++
    onVisibleChanged: motion.revision++

    function tailDeformation(): real {
        const p = motion.tailPhase
        if (p >= 1)
            return 0
        if (motion.collapsing || motion.peak === 0)
            return motion.initialDeformation * Math.pow(1 - p, 2)
        // Interpolate to one bounded peak; inherited deformation is never added
        // to a new pulse. The early peak leaves time to round off on arrival.
        if (p < 0.22) {
            const t = p / 0.22
            return motion.initialDeformation + (motion.peak - motion.initialDeformation)
                    * t * t * (3 - 2 * t)
        }
        return motion.peak * Math.pow((1 - p) / 0.78, 2)
    }

    function startTail(shape: real, direction: real, heading: real, time: real): void {
        tailFlight.stop()
        motion.collapsing = false
        motion.initialDeformation = shape
        motion.fromAngle = shape > 0.001 ? direction : heading
        motion.toAngle = heading
        motion.tailTime = Math.max(1, Math.round(time * 0.82))
        motion.tailPhase = 0
        tailFlight.start()
    }

    function snapTo(position: point): void {
        flight.stop()
        tailFlight.stop()
        motion.fromX = motion.toX = position.x
        motion.fromY = motion.toY = position.y
        motion.initialDeformation = motion.peak = 0
        motion.initialScale = 1
        motion.collapsing = false
        motion.tailPhase = motion.phase = 1
        visible = true
    }

    function flowTo(position: point): void {
        if (!visible) {
            gatherAt(position)
            return
        }
        const x = centerX
        const y = centerY
        const shape = deformation
        const scale = bodyScale
        const direction = angle
        const dx = position.x - x
        const dy = position.y - y
        const distance = Math.sqrt(dx * dx + dy * dy)
        if (distance < 0.01 && shape < 0.001 && scale > 0.999) {
            snapTo(position)
            return
        }
        flight.stop()
        const heading = distance > 0.01 ? Math.atan2(dy, dx) * 180 / Math.PI : direction
        const startAngle = shape > 0.001 ? direction : heading
        const delta = ((heading - startAngle + 180) % 360 + 360) % 360 - 180
        motion.fromX = x
        motion.fromY = y
        motion.toX = position.x
        motion.toY = position.y
        motion.pendingAngle = startAngle + delta
        motion.initialScale = scale
        motion.peak = Math.min(1, distance / Math.max(1, diameter * 2) * Math.max(0, stretch))
        motion.time = Math.max(1, duration)
        motion.phase = 0
        if (!motion.collapsing) {
            if (shape > 0.001 && Math.abs(delta) > 100) {
                tailFlight.stop()
                motion.fromAngle = motion.toAngle = direction
                motion.initialDeformation = shape
                motion.collapsing = true
                motion.tailTime = Math.min(40, motion.time)
                motion.tailPhase = 0
                tailFlight.start()
            } else {
                startTail(shape, direction, motion.pendingAngle, motion.time)
            }
        }
        flight.start()
    }

    function gatherAt(position: point): void {
        flight.stop()
        tailFlight.stop()
        motion.fromX = motion.toX = position.x
        motion.fromY = motion.toY = position.y
        motion.fromAngle = motion.toAngle = -135
        motion.initialDeformation = 0.65
        motion.initialScale = 0.18
        motion.peak = 0
        motion.collapsing = false
        motion.time = motion.tailTime = 210
        motion.tailPhase = motion.phase = 0
        visible = true
        tailFlight.start()
        flight.start()
    }

    function clear(): void {
        flight.stop()
        tailFlight.stop()
        visible = false
    }

    function covers(position: point): bool {
        // Read the current model directly: Shape.contains()/mapFromItem() can
        // expose previous-polish geometry to a label's binding in this frame.
        if (!visible || bodyScale <= 0)
            return false
        const radians = angle * Math.PI / 180
        const dx = (position.x - centerX) / bodyScale
        const dy = (position.y - centerY) / bodyScale
        const x = dx * Math.cos(radians) + dy * Math.sin(radians)
        const y = -dx * Math.sin(radians) + dy * Math.cos(radians)
        if (x < -outline.rx - outline.tail || x > outline.rx || Math.abs(y) > outline.ry)
            return false
        const points = outline.points
        let inside = false
        for (let i = 0, j = points.length - 1; i < points.length; j = i++) {
            const a = points[i]
            const b = points[j]
            if ((a.y > y) !== (b.y > y)
                    && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
                inside = !inside
        }
        return inside
    }

    function flattenCurves(curves: var): var {
        let start = Qt.point(curves[3][4], curves[3][5])
        const points = [start]
        for (let curve of curves) {
            for (let step = 1; step <= 16; ++step) {
                const t = step / 16
                const u = 1 - t
                points.push(Qt.point(u * u * u * start.x + 3 * u * u * t * curve[0]
                                     + 3 * u * t * t * curve[2] + t * t * t * curve[4],
                                     u * u * u * start.y + 3 * u * u * t * curve[1]
                                     + 3 * u * t * t * curve[3] + t * t * t * curve[5]))
            }
            start = Qt.point(curve[4], curve[5])
        }
        return points
    }

    QtObject {
        id: outline
        readonly property real r: root.diameter / 2
        readonly property real tail: root.diameter * 0.5 * Math.min(1, root.deformation)
        readonly property real rx: r * (1 - 0.04 * Math.min(1, root.deformation))
        readonly property real ry: r * (1 - 0.12 * Math.min(1, root.deformation))
        readonly property real k: 0.5522847498
        // Shared cubic controls, relative to the head center. The vertical
        // tangent at the back keeps the capped tail rounded even at its peak.
        readonly property var curves: [
            [rx, ry * k, rx * k, ry, 0, ry],
            [-rx * k, ry, -rx - tail, ry * k, -rx - tail, 0],
            [-rx - tail, -ry * k, -rx * k, -ry, 0, -ry],
            [rx * k, -ry, rx, -ry * k, rx, 0]
        ]
        // Flatten once per shape change, shared by every calendar cell query.
        readonly property var points: root.flattenCurves(curves)
    }

    NumberAnimation {
        id: tailFlight
        target: motion
        property: "tailPhase"
        from: 0
        to: 1
        duration: motion.tailTime
        onFinished: {
            if (motion.collapsing)
                root.startTail(0, motion.pendingAngle, motion.pendingAngle,
                               motion.time * (1 - motion.phase))
        }
    }

    NumberAnimation {
        id: flight
        target: motion
        property: "phase"
        from: 0
        to: 1
        duration: motion.time
        onFinished: {
            tailFlight.stop()
            motion.collapsing = false
            motion.tailPhase = 1
            root.settled()
        }
    }

    Shape {
        id: droplet
        readonly property real bodyX: outline.tail + outline.r
        x: root.centerX - bodyX
        y: root.centerY - outline.r
        width: outline.tail + root.diameter
        height: root.diameter
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        transform: [
            Scale { origin.x: droplet.bodyX; origin.y: outline.r; xScale: root.bodyScale; yScale: root.bodyScale },
            Rotation { origin.x: droplet.bodyX; origin.y: outline.r; angle: root.angle }
        ]

        ShapePath {
            fillColor: root.color
            strokeColor: "transparent"
            strokeWidth: 0
            startX: droplet.bodyX + outline.rx
            startY: outline.r
            PathCubic {
                x: droplet.bodyX + outline.curves[0][4]; y: outline.r + outline.curves[0][5]
                control1X: droplet.bodyX + outline.curves[0][0]; control1Y: outline.r + outline.curves[0][1]
                control2X: droplet.bodyX + outline.curves[0][2]; control2Y: outline.r + outline.curves[0][3]
            }
            PathCubic {
                x: droplet.bodyX + outline.curves[1][4]; y: outline.r + outline.curves[1][5]
                control1X: droplet.bodyX + outline.curves[1][0]; control1Y: outline.r + outline.curves[1][1]
                control2X: droplet.bodyX + outline.curves[1][2]; control2Y: outline.r + outline.curves[1][3]
            }
            PathCubic {
                x: droplet.bodyX + outline.curves[2][4]; y: outline.r + outline.curves[2][5]
                control1X: droplet.bodyX + outline.curves[2][0]; control1Y: outline.r + outline.curves[2][1]
                control2X: droplet.bodyX + outline.curves[2][2]; control2Y: outline.r + outline.curves[2][3]
            }
            PathCubic {
                x: droplet.bodyX + outline.curves[3][4]; y: outline.r + outline.curves[3][5]
                control1X: droplet.bodyX + outline.curves[3][0]; control1Y: outline.r + outline.curves[3][1]
                control2X: droplet.bodyX + outline.curves[3][2]; control2Y: outline.r + outline.curves[3][3]
            }
        }
    }
}
