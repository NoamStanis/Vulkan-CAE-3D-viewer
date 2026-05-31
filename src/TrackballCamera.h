#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

// ─────────────────────────────────────────────────────────────────────────────
// TrackballCamera
//
// An orbit ("trackball") camera: the eye orbits a fixed target on a sphere of
// radius `distance`. Orientation is stored as a quaternion to avoid gimbal lock
// and accumulate rotations smoothly.
//
// Pure CPU state with no Qt threading affinity. ViewerItem owns one on the main
// thread, mutates it from input handlers, and reads viewProjection() on the
// render thread at the existing main-thread-blocked sync point.
// ─────────────────────────────────────────────────────────────────────────────
class TrackballCamera
{
public:
    // Frame the camera around a target and bounding radius so the object fits.
    void frame(const QVector3D &target, float boundingRadius);

    // Drag-rotate. dx/dy are pixel deltas normalised by the viewport size, so a
    // full-width drag maps to a fixed angular sweep regardless of resolution.
    void rotate(float dxNorm, float dyNorm);

    // Wheel/pinch zoom. Positive `delta` zooms in (decreases distance).
    void zoom(float delta);

    // Combined model-view-projection for the given viewport aspect ratio.
    // Includes the OpenGL→Vulkan clip-space correction (Y flip, [0,1] depth).
    QMatrix4x4 viewProjection(float aspect) const;

private:
    QVector3D   m_target{0.0f, 0.0f, 0.0f};
    QQuaternion m_orientation;                 // identity = looking down -Z
    float       m_distance     = 3.0f;
    float       m_minDistance  = 0.05f;
    float       m_maxDistance  = 1000.0f;

    // Vertical field of view in degrees.
    static constexpr float k_fovY = 45.0f;
    // How many radians a full-viewport drag sweeps.
    static constexpr float k_rotateSpeed = 3.14159265f; // ~180° per full drag
};
