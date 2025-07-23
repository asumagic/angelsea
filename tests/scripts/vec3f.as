// SPDX-License-Identifier: BSD-2-Clause

final class Vec3f
{
	float x;
	float y;
	float z;

	Vec3f()
	{
		x = 0;
		y = 0;
		z = 0;
	}

	Vec3f(float _x, float _y, float _z)
	{
		x = _x;
		y = _y;
		z = _z;
	}

	Vec3f opAdd(const Vec3f&in oof) const { return Vec3f(x + oof.x, y + oof.y, z + oof.z); }

	Vec3f opAdd(float oof) const { return Vec3f(x + oof, y + oof, z + oof); }

	void opAddAssign(const Vec3f&in oof) { x += oof.x; y += oof.y; z += oof.z; }

	void opAddAssign(float oof) { x += oof; y += oof; z += oof; }

	Vec3f opSub(const Vec3f&in oof) const { return Vec3f(x - oof.x, y - oof.y, z - oof.z); }

	Vec3f opSub(float oof) const { return Vec3f(x - oof, y - oof, z - oof); }

	void opSubAssign(const Vec3f&in oof) { x -= oof.x; y -= oof.y; z -= oof.z; }

	Vec3f opMul(const Vec3f&in oof) { return Vec3f(x * oof.x, y * oof.y, z * oof.z); }

	Vec3f opMul(float oof) const { return Vec3f(x * oof, y * oof, z * oof); }

	void opMulAssign(float oof) { x *= oof; y *= oof; z *= oof; }

	Vec3f opDiv(const Vec3f&in oof) const { return Vec3f(x / oof.x, y / oof.y, z / oof.z); }

	Vec3f opDiv(float oof) { return Vec3f(x / oof, y / oof, z / oof); }

	void opDivAssign(float oof) { x /= oof; y /= oof; z /= oof; }

	void opAssign(const Vec3f &in oof){ x=oof.x;y=oof.y;z=oof.z; }

	Vec3f Lerp(const Vec3f&in desired, float t)
	{
		return Vec3f((((1 - t) * this.x) + (t * desired.x)), (((1 - t) * this.y) + (t * desired.y)), (((1 - t) * this.z) + (t * desired.z)));
	}

	void Print()
	{
		print("x: "+x+"; y: "+y+"; z: "+z);
	}

	string IntString()
	{
		return int(x)+", "+int(y)+", "+int(z);
	}

	string FloatString()
	{
		return x+", "+y+", "+z;
	}
}

float DotProduct(const Vec3f&in v1, const Vec3f&in v2)
{
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

Vec3f CrossProduct(const Vec3f&in v1, const Vec3f&in v2)
{
	return Vec3f(v1.y * v2.z - v1.z * v2.y, v1.z * v2.x - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x);
}

void main()
{
	Vec3f v1(10.0f, 10.0f, 10.0f);
	Vec3f v2(10.0f, 5.0f, 0.0f);
	print('' + DotProduct(v1, v2));
	CrossProduct(v1, v2).Print();
	v1.Lerp(v2, 0.5).Print();
}
