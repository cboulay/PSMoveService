//-- includes -----
#include "MathUtility.h"

//-- float methods -----
float clampf(float x, float lo, float hi)
{
	return fminf(fmaxf(x, lo), hi);
}

float clampf01(float x)
{
	return clampf(x, 0.f, 1.f);
}

float lerpf(float a, float b, float u)
{
	return a*(1.f - u) + b*u;
}

float lerp_clampf(float a, float b, float u)
{
	return clampf(lerpf(a, b, u), a, b);
}

float degrees_to_radians(float x)
{
	return ((x * k_real_pi) / 180.f);
}

float radians_to_degrees(float x)
{
	return ((x * 180.f) / k_real_pi);
}

float wrap_radians(float angle)
{
    return fmodf(angle + k_real_two_pi, k_real_two_pi);
}

float wrap_degrees(float angle)
{
    return fmodf(angle + 360.f, 360.f);
}

//-- glm vector methods -----
float glm_vec3_normalize_with_default(glm::vec3 &v, const glm::vec3 &default)
{
    const float length= glm::length(v);

    // Use the default value if v is too tiny
    v= (length > k_normal_epsilon) ? (v / length) : default;

    return length;
}

glm::vec3 glm_vec3_lerp(const glm::vec3 &a, const glm::vec3 &b, const float u)
{
    return a*(1.f-u) + b*u;
}