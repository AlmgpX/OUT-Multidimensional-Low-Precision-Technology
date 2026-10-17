// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;



namespace OUT.Motor

{

    internal enum OUTMotorEventKind : byte { Impulse, Damage, Sever }



    internal readonly struct OUTMotorEvent

    {

        public readonly OUTMotorEventKind Kind;

        public readonly int Actor, Joint;

        public readonly Vector3 Impulse;

        public readonly float Damage;

        public readonly uint Generation;

        public OUTMotorEvent(OUTMotorEventKind kind, int actor, int joint, Vector3 impulse, float damage, uint generation)

        { Kind=kind; Actor=actor; Joint=joint; Impulse=impulse; Damage=damage; Generation=generation; }

    }



    /// <summary>Single simulation-thread ring buffer. No locks, resizing, or steady-state allocations.</summary>

    internal sealed class OUTMotorEventQueue

    {

        private readonly OUTMotorEvent[] items;

        private int head, tail;

        public int Count { get; private set; }

        public int Capacity { get { return items.Length; } }

        public OUTMotorEventQueue(int capacity)

        {

            if (capacity < 1 || capacity > 1048576) throw new ArgumentOutOfRangeException(nameof(capacity));

            items = new OUTMotorEvent[capacity];

        }

        public bool TryEnqueue(OUTMotorEvent item)

        {

            if (Count == items.Length) return false;

            items[tail] = item;

            if (++tail == items.Length) tail = 0;

            Count++;

            return true;

        }

        public bool TryDequeue(out OUTMotorEvent item)

        {

            if (Count == 0) { item = default; return false; }

            item = items[head];

            if (++head == items.Length) head = 0;

            Count--;

            return true;

        }

    }

}
