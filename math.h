typedef struct {
    float x;
    float y;
} Vector;

inline Vector
operator-(Vector a, Vector b)
{
  Vector result;
  result.x = a.x - b.x;
  result.y = a.y - b.y;
  return result;
}

inline Vector
operator+(Vector a, Vector b)
{
  Vector result;
  result.x = a.x + b.x;
  result.y = a.y + b.y;
  return result;
}

inline Vector
operator+=(Vector &a, Vector b)
{
  a = a + b;
  return a;
}

inline Vector
operator-=(Vector &a, Vector b)
{
  a = a - b;
  return a;
}

inline Vector
operator-(Vector a)
{
  Vector result;
  result.x = -a.x;
  result.y = -a.y;
  return result;
}


inline Vector
operator*(float a, Vector b)
{
  Vector result = { a * b.x, a * b.y };
  return result;
}

inline Vector
operator*(Vector b, float a)
{
  Vector result = a * b;
  return result;
}

inline Vector
operator*=(Vector &a, float b)
{
  a = b * a;
  return a;
}


inline Vector
operator/(float a, Vector b)
{
  Vector result = { a / b.x, a / b.y };
  return result;
}

inline Vector
operator/(Vector b, float a)
{
  Vector result = { b.x / a, b.y / a  };
  return result;
}

inline Vector
operator/=(Vector &a, float b)
{
  a = b / a;
  return a;
}

Vector RotateVector(Vector vector, float alpha) {
    double cosAlpha = cos(alpha);
    double sinAlpha = sin(alpha);
    // signs are specific for our coordinate system
    float newX = vector.x * cosAlpha + vector.y * sinAlpha;
    float newY = -vector.x * sinAlpha + vector.y * cosAlpha;
    Vector result = { newX, newY };
    return result;
}

inline float VectorLength(Vector a) {
    return sqrt(a.x * a.x + a.y * a.y);
}

inline float IsZeroVector(Vector a) {
    return a.x == 0 && a.y == 0;
}


inline float Dot(Vector a, Vector b) {
    return a.x * b.x + a.y * b.y;
}
