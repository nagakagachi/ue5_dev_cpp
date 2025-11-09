// @author: @nagakagachi
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Math/Vector.h"
#include "Math/Vector4.h"

namespace naga
{
	class TestVoxelNoise
	{
	private:
		static FVector Floor(const FVector& v)
		{
			return FVector(floorf(v.X), floorf(v.Y), floorf(v.Z));
		}
		static FVector Frac(const FVector& v)
		{
			return v - Floor(v);
		}
		static float Frac(float v)
		{
			return v - floorf(v);
		}
	public:

		// hash from 3d position
		static float Hash(const FVector& v)
		{
			const auto p = Frac(v*0.3183099f + FVector(0.71f, 0.113f, 0.419f)) * 50.0f;
			return Frac(p.X*p.Y*p.Z*(p.X + p.Y + p.Z)) * 2.0f - 1.0f;
		}

		// return value noise (in v) and its derivatives (in yzw)
		// [-1, +1]
		template<bool WITH_GRADIENT = true>
		static FVector4 Noise(const FVector& v)
		{
			FVector i = Floor(v);
			FVector w = Frac(v);

#if 1
			// quintic
			FVector u = w * w*w*(w*(w*6.0f - 15.0f) + 10.0f);
			FVector du = 30.0f*w*w*(w*(w - 2.0f) + 1.0f);
#else
			// cubic
			FVector u = w * w*(3.0f - 2.0f*w);
			FVector du = 6.0f*w*(1.0f - w);
#endif    

			float a = Hash(i + FVector(0.0f, 0.0f, 0.0f));
			float b = Hash(i + FVector(1.0f, 0.0f, 0.0f));
			float c = Hash(i + FVector(0.0f, 1.0f, 0.0f));
			float d = Hash(i + FVector(1.0f, 1.0f, 0.0f));
			float e = Hash(i + FVector(0.0f, 0.0f, 1.0f));
			float f = Hash(i + FVector(1.0f, 0.0f, 1.0f));
			float g = Hash(i + FVector(0.0f, 1.0f, 1.0f));
			float h = Hash(i + FVector(1.0f, 1.0f, 1.0f));

			float k0 = a;
			float k1 = b - a;
			float k2 = c - a;
			float k3 = e - a;
			float k4 = a - b - c + d;
			float k5 = a - c - e + g;
			float k6 = a - b - e + f;
			float k7 = -a + b + c - d + e - f - g + h;
			// analytic derivative
			const auto n = (WITH_GRADIENT)? du * FVector(
				k1 + k4 * u.Y + k6 * u.Z + k7 * u.Y*u.Z,
				k2 + k5 * u.Z + k4 * u.X + k7 * u.Z*u.X,
				k3 + k6 * u.X + k5 * u.Y + k7 * u.X*u.Y)
				:
				FVector::ZeroVector;
			return FVector4(k0 + k1 * u.X + k2 * u.Y + k3 * u.Z + k4 * u.X*u.Y + k5 * u.Y*u.Z + k6 * u.Z*u.X + k7 * u.X*u.Y*u.Z, n.X, n.Y, n.Z);
		}

		// returns 3D fbm and its 3 derivatives
		// FVector4( noise, noise_grad_x, noise_grad_y, noise_grad_z)
		// [-1, +1]
		template<bool WITH_GRADIENT = true>
		static FVector4 Fbm(const FVector& x, int octaves)
		{
			float amp = 0.5;
			float a = 0.0;
			FVector  d = FVector(0.0);
			auto tmp_v = x;
			for (int i = 0; i < octaves; i++)
			{
				FVector4 n = Noise<WITH_GRADIENT>(tmp_v);
				a += amp * n.X;						// accumulate values
				if(WITH_GRADIENT)
					d += amp * FVector(n.Y, n.Z, n.W);	// accumulate derivatives
				amp *= 0.5f;
				tmp_v = 2.0f * tmp_v;
			}
			return FVector4(a, d.X, d.Y, d.Z);
		}

		// returns 3D fbm and its 3 derivatives
		// FVector4( noise, noise_grad_x, noise_grad_y, noise_grad_z)
		// [-1, +1]
		template<int OCTAVE, bool WITH_GRADIENT = true>
		static FVector4 Fbm(const FVector& x)
		{
			float amp = 0.5;
			float a = 0.0;
			FVector  d = FVector(0.0);
			auto tmp_v = x;
			for (int i = 0; i < OCTAVE; i++)
			{
				FVector4 n = Noise<WITH_GRADIENT>(tmp_v);
				a += amp * n.X;						// accumulate values
				if (WITH_GRADIENT)
					d += amp * FVector(n.Y, n.Z, n.W);	// accumulate derivatives
				amp *= 0.5f;
				tmp_v = 2.0f * tmp_v;
			}
			return FVector4(a, d.X, d.Y, d.Z);
		}
	};
}