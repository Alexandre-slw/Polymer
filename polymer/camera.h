#ifndef POLYMER_CAMERA_H_
#define POLYMER_CAMERA_H_

#include <polymer/math.h>
#include <cmath>

namespace polymer {

struct Camera {
  Vector3f position;
  float yaw;
  float pitch;
  float roll;
  float fov;

  float target_fov;
  float aspect_ratio;
  float near;
  float far;

  inline Vector3f GetForward() const {
    return Vector3f(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));
  }

  inline Vector3f GetForwardXZ() const {
    return Vector3f(cosf(yaw), 0, sinf(yaw));
  }

  inline mat4 GetViewMatrix() const {
    static const Vector3f kWorldUp(0, 1, 0);

    Vector3f front(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));

    Vector3f side = Normalize(front.Cross(kWorldUp));
    Vector3f up = Normalize(side.Cross(front));

    Vector3f rolledUp = up * cosf(roll) + side * sinf(roll);

    return LookAt(Vector3f(0, 0, 0), front, rolledUp);
  }

  inline mat4 GetProjectionMatrix() const {
    return Perspective(fov, aspect_ratio, near, far);
  }

  inline void SetFov(float f) {
    target_fov = Radians(f);

    if (!fov) {
      fov = target_fov;
    }
  }

  inline Frustum GetViewFrustum() const {
    static const Vector3f kWorldUp(0, 1, 0);

    Vector3f front(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));

    Vector3f side = Normalize(front.Cross(kWorldUp));
    Vector3f up = Normalize(side.Cross(front));

    float c = cosf(roll);
    float s = sinf(roll);

    Vector3f rolledUp = up * c + side * s;
    Vector3f rolledSide = side * c - up * s;

    return Frustum(position, front, near, far, fov, aspect_ratio, rolledUp, rolledSide);
  }
};

} // namespace polymer

#endif
