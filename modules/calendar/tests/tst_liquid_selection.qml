import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "LiquidSelection"
    when: windowShown
    width: 900
    height: 900
    visible: true
    property var liquid
    readonly property bool oldCellCovered: liquid ? liquid.covers(Qt.point(120, 140)) : false
    readonly property bool newCellCovered: liquid ? liquid.covers(Qt.point(600, 620)) : false

    SignalSpy { id: settledSpy; signalName: "settled" }

    function init() {
        const component = Qt.createComponent("../LiquidSelection.qml")
        compare(component.status, Component.Ready, component.errorString())
        liquid = component.createObject(testCase, { width: 900, height: 900, duration: 180 })
        verify(liquid !== null)
        settledSpy.target = liquid
        settledSpy.clear()
    }

    function cleanup() {
        if (liquid)
            liquid.destroy()
        liquid = null
    }

    function verifyCircle(x, y) {
        fuzzyCompare(liquid.centerX, x, 0.01)
        fuzzyCompare(liquid.centerY, y, 0.01)
        verify(liquid.deformation === 0, "A settled shape must have no remaining deformation")
        verify(liquid.covers(Qt.point(x, y)))
        verify(liquid.covers(Qt.point(x + 24, y)))
        verify(liquid.covers(Qt.point(x, y - 24)))
        verify(!liquid.covers(Qt.point(x + 26, y)))
        verify(!liquid.covers(Qt.point(x, y - 26)))
    }

    function test_coverage_uses_current_geometry_without_render_wait() {
        liquid.snapTo(Qt.point(120, 140))
        verifyCircle(120, 140)
        verify(oldCellCovered)
        liquid.snapTo(Qt.point(600, 620))
        verifyCircle(600, 620)
        verify(newCellCovered, "Bound contrast must update before the next render")
        verify(!oldCellCovered, "Bound contrast at the old cell must immediately clear")
        verify(!liquid.covers(Qt.point(120, 140)), "The old cell must lose contrast immediately")
        liquid.gatherAt(Qt.point(300, 350))
        verify(liquid.covers(Qt.point(300, 350)))
        verify(!liquid.covers(Qt.point(300, 340)), "Coverage must immediately use the gathered scale")
        liquid.snapTo(Qt.point(500, 550))
        verifyCircle(500, 550)
    }

    function test_short_tail_has_a_round_tip() {
        liquid.duration = 600
        liquid.snapTo(Qt.point(150, 450))
        let sampled = false
        let rounded = false
        let bounded = false
        const samplePeak = function() {
            if (!sampled && liquid.deformation > 0.9) {
                // The early peak can fall entirely between tryVerify's polls.
                // Sample the real geometry when that animation frame arrives.
                rounded = liquid.covers(Qt.point(liquid.centerX - 40, liquid.centerY + 8))
                bounded = !liquid.covers(Qt.point(liquid.centerX - 51, liquid.centerY))
                sampled = true
            }
        }
        liquid.deformationChanged.connect(samplePeak)
        liquid.flowTo(Qt.point(850, 450))
        tryVerify(function() { return sampled })
        liquid.deformationChanged.disconnect(samplePeak)
        verify(rounded,
               "The short tail must have rounded shoulders instead of a needle tip")
        verify(bounded,
               "A 50 px body must not extend more than 25 px behind its circle")
    }

    function test_major_reversal_collapses_before_turning() {
        liquid.duration = 240
        liquid.snapTo(Qt.point(150, 450))
        liquid.flowTo(Qt.point(850, 450))
        wait(55)
        verify(liquid.deformation > 0.4)
        const oldShape = liquid.deformation
        liquid.flowTo(Qt.point(100, 450))
        fuzzyCompare(liquid.deformation, oldShape, 0.001)
        fuzzyCompare(liquid.angle, 0, 0.001)
        let sawRound = false
        for (let i = 0; i < 9; ++i) {
            wait(8)
            sawRound = sawRound || liquid.deformation < 0.08
            const heading = ((liquid.angle % 360) + 360) % 360
            verify(liquid.deformation < 0.08 || heading < 1 || Math.abs(heading - 180) < 1,
                   "A substantial tail must hold its old heading until it is round")
            verify(!liquid.covers(Qt.point(liquid.centerX, liquid.centerY + 30)),
                   "Reversal must not swing a tail off the movement row")
        }
        verify(sawRound, "Reversal must finish its collapse in about 40 ms")
        tryCompare(liquid, "running", false)
        verifyCircle(100, 450)
        compare(settledSpy.count, 1)
    }

    function test_rapid_reversals_do_not_pump_or_restart_collapse_data() {
        return [{ tag: "20ms", interval: 20 }, { tag: "50ms", interval: 50 },
                { tag: "100ms", interval: 100 }]
    }

    function test_rapid_reversals_do_not_pump_or_restart_collapse(data) {
        liquid.duration = 240
        liquid.snapTo(Qt.point(450, 450))
        liquid.flowTo(Qt.point(850, 450))
        wait(55)
        let elapsed = 0
        let sawRound = false
        let destination = Qt.point(0, 0)
        for (let i = 0; i < 8; ++i) {
            destination = Qt.point(liquid.centerX + (i % 2 === 0 ? -300 : 300), 450)
            const x = liquid.centerX
            const shape = liquid.deformation
            liquid.flowTo(destination)
            fuzzyCompare(liquid.centerX, x, 0.001)
            fuzzyCompare(liquid.deformation, shape, 0.001)
            for (let tick = 0; tick < data.interval; tick += 5) {
                wait(5)
                elapsed += 5
                if (elapsed <= 70 && liquid.deformation < 0.08)
                    sawRound = true
                verify(liquid.deformation <= 1.001,
                       "Repeated clicks must replace the tail envelope rather than add to it")
                verify(!liquid.covers(Qt.point(liquid.centerX, liquid.centerY + 30)),
                       "A rapid horizontal reversal must not make a diagonal needle")
                verify(!liquid.covers(Qt.point(liquid.centerX - 51, liquid.centerY)))
                verify(!liquid.covers(Qt.point(liquid.centerX + 51, liquid.centerY)))
            }
        }
        verify(sawRound, "New clicks must not restart or delay an active collapse")
        tryCompare(liquid, "running", false)
        verifyCircle(destination.x, destination.y)
        compare(settledSpy.count, 1)
    }

    function test_retarget_at_current_position_only_settles_the_tail() {
        liquid.snapTo(Qt.point(100, 100))
        liquid.flowTo(Qt.point(700, 100))
        wait(55)
        const position = Qt.point(liquid.centerX, liquid.centerY)
        let previousShape = liquid.deformation
        liquid.flowTo(position)
        while (liquid.running) {
            wait(10)
            verify(liquid.deformation <= previousShape + 0.001,
                   "A stationary head must not inflate a fresh tail")
            previousShape = liquid.deformation
        }
        verifyCircle(position.x, position.y)
        compare(settledSpy.count, 1)
    }

    function test_retarget_preserves_gathered_scale() {
        liquid.gatherAt(Qt.point(150, 450))
        wait(35)
        const scale = liquid.bodyScale
        const shape = liquid.deformation
        liquid.flowTo(Qt.point(850, 450))
        fuzzyCompare(liquid.bodyScale, scale, 0.001)
        fuzzyCompare(liquid.deformation, shape, 0.001)
        tryCompare(liquid, "running", false)
        verifyCircle(850, 450)
    }

    function test_snap_has_circle_coverage_without_completion() {
        liquid.snapTo(Qt.point(120, 140))
        wait(1)
        verify(liquid.visible)
        verify(!liquid.running)
        verifyCircle(120, 140)
        compare(settledSpy.count, 0)
    }

    function test_flow_in_every_direction_data() {
        return [
            { tag: "right", x: 750, y: 450 },
            { tag: "left", x: 150, y: 450 },
            { tag: "down", x: 450, y: 750 },
            { tag: "up", x: 450, y: 150 },
            { tag: "down-right", x: 750, y: 750 },
            { tag: "down-left", x: 150, y: 750 },
            { tag: "up-right", x: 750, y: 150 },
            { tag: "up-left", x: 150, y: 150 }
        ]
    }

    function test_flow_in_every_direction(data) {
        liquid.snapTo(Qt.point(450, 450))
        liquid.flowTo(Qt.point(data.x, data.y))
        compare(liquid.targetX, data.x)
        compare(liquid.targetY, data.y)
        verify(liquid.running)
        tryVerify(function() { return liquid.deformation > 0.65 })
        verify(liquid.centerX >= Math.min(450, data.x) && liquid.centerX <= Math.max(450, data.x))
        verify(liquid.centerY >= Math.min(450, data.y) && liquid.centerY <= Math.max(450, data.y))
        const length = Math.sqrt(Math.pow(data.x - 450, 2) + Math.pow(data.y - 450, 2))
        const ux = (data.x - 450) / length
        const uy = (data.y - 450) / length
        verify(liquid.covers(Qt.point(liquid.centerX - ux * 35, liquid.centerY - uy * 35)),
               "The tail must extend behind the head")
        verify(!liquid.covers(Qt.point(liquid.centerX + ux * 30, liquid.centerY + uy * 30)),
               "The front must stay rounded instead of stretching symmetrically")
        tryCompare(liquid, "running", false)
        verifyCircle(data.x, data.y)
        compare(settledSpy.count, 1)
    }

    function test_retarget_preserves_current_visual_state() {
        liquid.duration = 320
        liquid.snapTo(Qt.point(80, 100))
        liquid.flowTo(Qt.point(500, 100))
        tryVerify(function() { return liquid.deformation > 0.2 })
        const x = liquid.centerX
        const y = liquid.centerY
        const deformation = liquid.deformation
        liquid.flowTo(Qt.point(140, 500))
        fuzzyCompare(liquid.centerX, x, 0.001)
        fuzzyCompare(liquid.centerY, y, 0.001)
        fuzzyCompare(liquid.deformation, deformation, 0.001)
        compare(settledSpy.count, 0)
        tryCompare(liquid, "running", false)
        verifyCircle(140, 500)
        compare(settledSpy.count, 1)
    }

    function test_snap_and_clear_cancel_without_completion() {
        liquid.snapTo(Qt.point(100, 100))
        liquid.flowTo(Qt.point(700, 100))
        wait(35)
        liquid.snapTo(Qt.point(200, 200))
        wait(220)
        verifyCircle(200, 200)
        compare(settledSpy.count, 0)
        liquid.flowTo(Qt.point(700, 700))
        wait(35)
        liquid.clear()
        wait(220)
        verify(!liquid.visible)
        verify(!liquid.running)
        verify(!liquid.covers(Qt.point(liquid.centerX, liquid.centerY)))
        compare(settledSpy.count, 0)
    }

    function test_hidden_flow_gathers_at_target() {
        liquid.flowTo(Qt.point(300, 350))
        compare(liquid.centerX, 300)
        compare(liquid.centerY, 350)
        verify(liquid.visible)
        verify(liquid.running)
        tryCompare(liquid, "running", false)
        verifyCircle(300, 350)
        compare(settledSpy.count, 1)
    }

    function test_explicit_gather_replaces_flight() {
        liquid.snapTo(Qt.point(50, 50))
        liquid.flowTo(Qt.point(700, 50))
        wait(30)
        liquid.gatherAt(Qt.point(300, 350))
        compare(liquid.centerX, 300)
        compare(liquid.centerY, 350)
        tryCompare(liquid, "running", false)
        verifyCircle(300, 350)
        compare(settledSpy.count, 1)
    }

    function test_zero_distance_stays_finite() {
        liquid.snapTo(Qt.point(100, 100))
        liquid.flowTo(Qt.point(100, 100))
        wait(250)
        verifyCircle(100, 100)
        verify(isFinite(liquid.progress))
        verify(!liquid.running)
    }

    function test_turn_crosses_angle_boundary_by_shortest_arc() {
        liquid.duration = 600
        liquid.snapTo(Qt.point(450, 450))
        liquid.flowTo(Qt.point(150, 400))
        tryVerify(function() { return liquid.deformation > 0.2 })
        const previousAngle = liquid.angle
        liquid.flowTo(Qt.point(liquid.centerX - 300, liquid.centerY + 50))
        fuzzyCompare(liquid.angle, previousAngle, 0.001)
        tryVerify(function() { return liquid.progress > 0.1 })
        verify(Math.abs(liquid.angle - previousAngle) < 25,
               "Crossing -180/180 degrees must take the short turn")
        verify(liquid.angle < previousAngle)
    }

    function test_long_flight_has_bounded_tail() {
        liquid.duration = 600
        liquid.snapTo(Qt.point(150, 450))
        liquid.flowTo(Qt.point(850, 450))
        tryVerify(function() { return liquid.deformation > 0.8 })
        verify(!liquid.covers(Qt.point(liquid.centerX - 51, liquid.centerY)),
               "Long moves must not leave a trail across the whole grid")
    }

    function test_repeated_turns_keep_using_shortest_arc() {
        liquid.duration = 200
        liquid.snapTo(Qt.point(450, 450))
        const headings = [170, -100, -10, 80, 170, -100, -10]
        for (let i = 0; i < headings.length; ++i) {
            const previousAngle = liquid.angle
            const radians = headings[i] * Math.PI / 180
            liquid.flowTo(Qt.point(liquid.centerX + 200 * Math.cos(radians),
                                   liquid.centerY + 200 * Math.sin(radians)))
            tryVerify(function() { return liquid.progress > 0.4 })
            if (i > 0) {
                const turn = liquid.angle - previousAngle
                verify(turn > 0 && turn < 100,
                       "Repeated 90-degree retargets must not accumulate a full reverse turn: " + turn)
            }
        }
    }

    function test_motion_revision_tracks_coverage() {
        liquid.snapTo(Qt.point(100, 100))
        const revision = liquid.motionRevision
        liquid.flowTo(Qt.point(500, 100))
        tryVerify(function() { return liquid.centerX > 150 })
        verify(liquid.motionRevision !== revision)
        verify(liquid.covers(Qt.point(liquid.centerX, liquid.centerY)))
    }
}
