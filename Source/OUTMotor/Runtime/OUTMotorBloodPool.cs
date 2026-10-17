// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using UnityEngine;

namespace OUT.Motor
{
    /// <summary>Bounded cosmetic particles. One pool for the world, no GameObjects,
    /// physics components, Emit allocations or coupling to the render pipeline.</summary>
    public sealed class OUTMotorBloodPool
    {
        public const int Capacity = 1024;
        private readonly Vector3[] position = new Vector3[Capacity];
        private readonly Vector3[] velocity = new Vector3[Capacity];
        private readonly float[] life = new float[Capacity];
        private readonly float[] radius = new float[Capacity];
        private readonly byte[] grounded = new byte[Capacity];
        private int next;
        private uint seed = 0x91e10da5;
        public int Active { get; private set; }
        public bool IsActive(int index) => life[index] > 0;
        public Vector3 GetPosition(int index) => position[index];
        public float GetRadius(int index) => radius[index];
        public bool IsGrounded(int index) => grounded[index] != 0;
        public float GetAlpha(int index) => Mathf.Clamp01(life[index]);

        private float Random01()
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return (seed & 0x00ffffff) * (1f / 16777216f);
        }

        /// <summary>Recycles oldest slots when full. Count is bounded even for burst input.</summary>
        public void Emit(Vector3 origin, Vector3 direction, int count = 12)
        {
            count = Mathf.Clamp(count, 0, 128);
            Vector3 aim = direction.sqrMagnitude > .0001f ? direction.normalized : Vector3.up;
            for (int n = 0; n < count; n++)
            {
                int i = next; next = (next + 1) % Capacity;
                if (life[i] <= 0) Active++;
                Vector3 spread = new Vector3(Random01() * 2 - 1, Random01() * 1.4f, Random01() * 2 - 1);
                position[i] = origin;
                velocity[i] = aim * (1.1f + Random01() * 2.3f) + spread * 1.5f;
                life[i] = 2.2f + Random01() * 2.6f;
                radius[i] = .013f + Random01() * .026f;
                grounded[i] = 0;
            }
        }

        public void Tick(float dt)
        {
            if (dt <= 0 || Active == 0) return;
            // One bounded cosmetic integration; expiry still follows the supplied clock.
            float h = Mathf.Min(dt, .05f);
            float drag = 1f / (1f + 1.8f * h);
            for (int i = 0; i < Capacity; i++)
            {
                if (life[i] <= 0) continue;
                life[i] -= dt;
                if (life[i] <= 0) { life[i] = 0; Active--; continue; }
                if (grounded[i] != 0) continue;
                velocity[i] = (velocity[i] + Vector3.down * (9.81f * h)) * drag;
                position[i] += velocity[i] * h;
                if (position[i].y > .012f) continue;
                position[i].y = .012f;
                velocity[i] = Vector3.zero;
                radius[i] *= 1.6f;
                grounded[i] = 1;
            }
        }

        public void Clear()
        {
            Array.Clear(life, 0, life.Length);
            Active = 0; next = 0; seed = 0x91e10da5;
        }
    }
}
