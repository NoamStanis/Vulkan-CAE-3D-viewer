#include "TrackballCamera.h"

#include <algorithm>
#include <cmath>

void TrackballCamera::frame(const QVector3D &target, float boundingRadius)
{
    m_target = target;

    // Distance so the bounding sphere fits within the vertical FOV, with margin.
    const float fovRad = qDegreesToRadians(k_fovY);
    const float fit    = boundingRadius / std::sin(fovRad * 0.5f);
    m_distance    = fit * 1.3f;
    m_minDistance = std::max(0.01f, boundingRadius * 0.05f);
    m_maxDistance = fit * 20.0f;
    m_orientation = QQuaternion();
}

void TrackballCamera::rotate(float dxNorm, float dyNorm)
{
    // Yaw about world up, pitch about the camera's right axis. Composing the new
    // rotation on the left of the accumulated orientation keeps axes in view
    // space, which feels natural for a trackball.
    const float yaw   = -dxNorm * k_rotateSpeed;
    const float pitch = -dyNorm * k_rotateSpeed;

    const QQuaternion qYaw =
        QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, qRadiansToDegrees(yaw));
    const QQuaternion qPitch =
        QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, qRadiansToDegrees(pitch));

    m_orientation = qPitch * m_orientation * qYaw;
    m_orientation.normalize();
}

void TrackballCamera::zoom(float delta)
{
    // Exponential zoom so each notch feels proportional at any distance.
    m_distance *= std::pow(0.9f, delta);
    m_distance  = std::clamp(m_distance, m_minDistance, m_maxDistance);
}

QMatrix4x4 TrackballCamera::viewProjection(float aspect) const
{
    // ── View ──────────────────────────────────────────────────────────────────
    // Start at the target, back off along the rotated +Z axis to place the eye.
    const QVector3D forward = m_orientation.rotatedVector(QVector3D(0, 0, -1));
    const QVector3D up      = m_orientation.rotatedVector(QVector3D(0, 1, 0));
    const QVector3D eye     = m_target - forward * m_distance;

    QMatrix4x4 view;
    view.lookAt(eye, m_target, up);

    // ── Projection ─────────────────────────────────────────────────────────────
    QMatrix4x4 proj;
    proj.perspective(k_fovY, aspect > 0.0f ? aspect : 1.0f, 0.01f, 1000.0f);

    // QMatrix4x4::perspective produces OpenGL clip space: Y up, depth in [-1, 1].
    // Vulkan wants Y down and depth in [0, 1]. This correction matrix converts
    // it, and because it includes a Y flip it preserves triangle winding so our
    // CCW front-face culling stays correct.
    QMatrix4x4 clip(1.0f,  0.0f, 0.0f, 0.0f,
                    0.0f, -1.0f, 0.0f, 0.0f,
                    0.0f,  0.0f, 0.5f, 0.5f,
                    0.0f,  0.0f, 0.0f, 1.0f);

    return clip * proj * view;
}
