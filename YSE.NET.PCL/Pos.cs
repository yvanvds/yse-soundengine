using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public class Pos
  {
    public Pos() { }
    public Pos(float r) { Set(r); }
    public Pos(float x, float y, float z) { Set(x, y, z); }
    public Pos(Pos p) { Set(p.X, p.Y, p.Z); }

    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }

    public void Zero() { X = Y = Z = 0; }
    public void Set(float r) { X = Y = Z = r; }
    public void Set(float x, float y, float z) { X = x; Y = y; Z = z; }
    public float Length() { return (float)Math.Sqrt(Math.Pow(X, 2) + Math.Pow(Y, 2) + Math.Pow(Z, 2)); }

    String AsString() { return "X: " + X + " Y: " + Y + " Z: " + Z; }

    public static Pos operator+(Pos p, float r) { return new Pos() { X = p.X + r, Y = p.Y + r, Z = p.Z + r }; }
    public static Pos operator-(Pos p, float r) { return new Pos() { X = p.X - r, Y = p.Y - r, Z = p.Z - r }; }
    public static Pos operator*(Pos p, float r) { return new Pos() { X = p.X * r, Y = p.Y * r, Z = p.Z * r }; }
    public static Pos operator/(Pos p, float r) { return new Pos() { X = p.X / r, Y = p.Y / r, Z = p.Z / r }; }

    public static Pos operator +(Pos p, Pos o) { return new Pos() { X = p.X + o.X, Y = p.Y + o.Y, Z = p.Z + o.Z }; }
    public static Pos operator -(Pos p, Pos o) { return new Pos() { X = p.X - o.X, Y = p.Y - o.Y, Z = p.Z - o.Z }; }
    public static Pos operator *(Pos p, Pos o) { return new Pos() { X = p.X * o.X, Y = p.Y * o.Y, Z = p.Z * o.Z }; }
    public static Pos operator /(Pos p, Pos o) { return new Pos() { X = p.X / o.X, Y = p.Y / o.Y, Z = p.Z / o.Z }; }

    public static bool operator==(Pos p, Pos o) { return (p.X == o.X && p.Y == o.Y && p.Z == o.Z); }
    public static bool operator!=(Pos p, Pos o) { return (p.X != o.X || p.Y != o.Y || p.Z != o.Z); }

    public static Pos Min(Pos a, Pos b) { return new Pos(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Min(a.Z, b.Z)); }
    public static Pos Max(Pos a, Pos b) { return new Pos(Math.Max(a.X, b.X), Math.Max(a.Y, b.Y), Math.Max(a.Z, b.Z)); }
    public static Pos Avg(Pos a, Pos b) { return (a + b) * 0.5f; }
    public static float Dist(Pos a, Pos b) { return (float)Math.Sqrt(Math.Pow((b.X - a.X), 2) + Math.Pow((b.Y - a.Y), 2) + Math.Pow((b.Z - a.Z), 2)); }
    public static float Dot(Pos a, Pos b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
  }
}
