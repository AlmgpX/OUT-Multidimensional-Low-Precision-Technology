// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using UnityEngine;

namespace OUT.Motor
{
    public enum OUTMotorMotionFamily : byte
    {
        Idle, Walk, Run, Crouch, Guard, Strike, Kick, Throw,
        Hit, Fall, Recovery, Drunk, Sport, Zombie, Police, Protest
    }

    public readonly struct OUTMotorMotionClip
    {
        public readonly string Name;
        public readonly OUTMotorMotionFamily Family;
        public readonly float Duration, NominalSpeed, MuscleScale;
        public readonly bool Looping;
        internal OUTMotorMotionClip(string name, OUTMotorMotionFamily family, float duration,
            float speed, float muscleScale, bool looping)
        {
            Name = name; Family = family; Duration = duration;
            NominalSpeed = speed; MuscleScale = muscleScale; Looping = looping;
        }
    }

    /// <summary>
    /// Shared procedural target clips derived from the 17 source poses, not captured animation.
    /// Baked once on first use. Sampling is two contiguous pose reads and a lerp; no trig,
    /// decoding, object creation or per-actor clip copies in the simulation hot path.
    /// These are muscle targets; the particle solver remains responsible for contacts and balance.
    /// </summary>
    public static class OUTMotorMotionCache
    {
        public const int Count = 128;
        public const int VariantsPerFamily = 8;
        public const int FramesPerClip = 17;
        public const float StanceFraction = .6f;
        public const int CachedPoseBytes = Count * FramesPerClip * OUTMotorData.Joints * 12;
        private const int Intervals = FramesPerClip - 1;
        private static readonly OUTMotorMotionClip[] Clips = new OUTMotorMotionClip[Count];
        private static readonly Vector3[] Frames = new Vector3[Count * FramesPerClip * OUTMotorData.Joints];
        private static readonly int[] RecoveryPoses = {10,8,12,13,14,15,16,0};
        private static readonly string[] Names =
        {
            "Idle relaxed", "Idle alert", "Idle tired", "Idle nervous", "Idle breathing", "Idle look left", "Idle look right", "Idle wounded",
            "Walk relaxed", "Walk careful", "Walk heavy", "Walk brisk", "Walk limp left", "Walk limp right", "Walk tired", "Walk patrol",
            "Run jog", "Run sprint", "Run heavy", "Run chase", "Run wounded left", "Run wounded right", "Run escape", "Run athletic",
            "Crouch settle", "Crouch sneak", "Crouch cover", "Crouch tired", "Crouch brace left", "Crouch brace right", "Crouch inspect", "Crouch recover",
            "Guard boxing", "Guard close", "Guard wide", "Guard low", "Guard left", "Guard right", "Guard retreat", "Guard flinch",
            "Strike jab left", "Strike jab right", "Strike hook left", "Strike hook right", "Strike shove", "Strike overhead", "Strike body left", "Strike body right",
            "Kick front left", "Kick front right", "Kick low left", "Kick low right", "Kick knee left", "Kick knee right", "Kick push left", "Kick push right",
            "Throw overhand left", "Throw overhand right", "Throw underhand left", "Throw underhand right", "Throw heavy left", "Throw heavy right", "Throw quick left", "Throw quick right",
            "Hit chest", "Hit back", "Hit shoulder left", "Hit shoulder right", "Hit stomach", "Hit head", "Hit leg left", "Hit leg right",
            "Fall forward", "Fall backward", "Fall left", "Fall right", "Fall knees", "Fall curl", "Fall stumble left", "Fall stumble right",
            "Recovery prone left", "Recovery prone right", "Recovery supine left", "Recovery supine right", "Recovery tired left", "Recovery tired right", "Recovery quick left", "Recovery quick right",
            "Drunk sway", "Drunk stumble", "Drunk swagger", "Drunk stagger", "Drunk lean left", "Drunk lean right", "Drunk argue", "Drunk exhausted",
            "Sport warmup", "Sport bounce", "Sport spar left", "Sport spar right", "Sport dodge left", "Sport dodge right", "Sport victory", "Sport fatigued",
            "Zombie shamble", "Zombie reach", "Zombie heavy", "Zombie frantic", "Zombie limp left", "Zombie limp right", "Zombie lurch", "Zombie exhausted",
            "Police patrol", "Police shield brace", "Police advance", "Police push", "Police guard left", "Police guard right", "Police command", "Police restraint",
            "Protest march", "Protest chant", "Protest raised arms", "Protest push", "Protest evade left", "Protest evade right", "Protest rally", "Protest cower"
        };

        static OUTMotorMotionCache()
        {
            for (int id = 0; id < Count; id++)
            {
                var family = (OUTMotorMotionFamily)(id / VariantsPerFamily);
                int variant = id % VariantsPerFamily;
                bool loop = family != OUTMotorMotionFamily.Strike && family != OUTMotorMotionFamily.Kick
                    && family != OUTMotorMotionFamily.Throw && family != OUTMotorMotionFamily.Hit
                    && family != OUTMotorMotionFamily.Fall && family != OUTMotorMotionFamily.Recovery;
                float duration = family == OUTMotorMotionFamily.Run ? .61f : family == OUTMotorMotionFamily.Walk ? 1.25f
                    : family == OUTMotorMotionFamily.Recovery ? 5.6f : family == OUTMotorMotionFamily.Fall ? .9f
                    : family == OUTMotorMotionFamily.Strike || family == OUTMotorMotionFamily.Kick ? .48f
                    : family == OUTMotorMotionFamily.Throw ? .65f : family == OUTMotorMotionFamily.Hit ? .42f : 1.8f;
                duration *= 1f + (variant - 3.5f) * .035f;
                float speed = family == OUTMotorMotionFamily.Run ? 2.6f : family == OUTMotorMotionFamily.Walk ? .8f
                    : family == OUTMotorMotionFamily.Zombie ? .55f : family == OUTMotorMotionFamily.Drunk ? .45f
                    : family == OUTMotorMotionFamily.Police || family == OUTMotorMotionFamily.Protest ? .7f : 0f;
                float muscle = family == OUTMotorMotionFamily.Drunk ? .68f : family == OUTMotorMotionFamily.Zombie ? .78f
                    : family == OUTMotorMotionFamily.Fall ? .18f : family == OUTMotorMotionFamily.Hit ? .72f : 1f;
                Clips[id] = new OUTMotorMotionClip(Names[id], family, duration, speed, muscle, loop);
                for (int frame = 0; frame < FramesPerClip; frame++)
                for (int joint = 0; joint < OUTMotorData.Joints; joint++)
                {
                    // Copy the first frame for a bit-exact loop seam.
                    int index = (id * FramesPerClip + frame) * OUTMotorData.Joints + joint;
                    Frames[index] = loop && frame == Intervals
                        ? Frames[id * FramesPerClip * OUTMotorData.Joints + joint]
                        : Bake(family, variant, frame / (float)Intervals, joint);
                }
            }
        }

        public static OUTMotorMotionClip GetClip(int clipId) { return Clips[ValidateClip(clipId)]; }

        public static int Select(OUTMotorMotionFamily family, uint seed)
        {
            if ((uint)family >= Count / VariantsPerFamily) throw new ArgumentOutOfRangeException(nameof(family));
            seed ^= seed >> 16; seed *= 0x7feb352du; seed ^= seed >> 15;
            return (int)family * VariantsPerFamily + (int)(seed & (VariantsPerFamily - 1));
        }

        public static Vector3 Sample(int clipId, float timeSeconds, int joint)
        {
            ValidateClip(clipId);
            return SampleNormalized(clipId, timeSeconds / Clips[clipId].Duration, joint);
        }

        public static Vector3 SampleNormalized(int clipId, float normalizedTime, int joint)
        {
            ValidateClip(clipId);
            if ((uint)joint >= OUTMotorData.Joints) throw new ArgumentOutOfRangeException(nameof(joint));
            if (float.IsNaN(normalizedTime) || float.IsInfinity(normalizedTime)) normalizedTime = 0f;
            float t = Clips[clipId].Looping ? normalizedTime - Mathf.Floor(normalizedTime) : Mathf.Clamp01(normalizedTime);
            float f = t * Intervals;
            int frame = Mathf.Min((int)f, Intervals - 1);
            int offset = (clipId * FramesPerClip + frame) * OUTMotorData.Joints + joint;
            return Vector3.LerpUnclamped(Frames[offset], Frames[offset + OUTMotorData.Joints], f - frame);
        }

        /// <summary>Bit 0: left ankle (12) stance; bit 1: right ankle (16) stance.</summary>
        public static byte GetFootContactMask(int clipId, float normalizedTime)
        {
            ValidateClip(clipId);
            var family = Clips[clipId].Family;
            if (family != OUTMotorMotionFamily.Walk && family != OUTMotorMotionFamily.Run
                && family != OUTMotorMotionFamily.Drunk && family != OUTMotorMotionFamily.Zombie
                && family != OUTMotorMotionFamily.Police && family != OUTMotorMotionFamily.Protest
                && family != OUTMotorMotionFamily.Sport) return 3;
            int mask = GetFootPhase(clipId, normalizedTime, true) < StanceFraction ? 1 : 0;
            if (GetFootPhase(clipId, normalizedTime, false) < StanceFraction) mask |= 2;
            return (byte)mask;
        }

        /// <summary>Shared planner phase, including the clip's left/right mirror variant.</summary>
        public static float GetFootPhase(int clipId, float normalizedTime, bool leftFoot)
        {
            ValidateClip(clipId);
            if (float.IsNaN(normalizedTime) || float.IsInfinity(normalizedTime)) normalizedTime = 0;
            float phase = normalizedTime + (leftFoot ? 0f : .5f) + ((clipId & 1) != 0 ? .5f : 0f);
            return phase - Mathf.Floor(phase);
        }

        private static int ValidateClip(int id)
        {
            if ((uint)id >= Count) throw new ArgumentOutOfRangeException(nameof(id));
            return id;
        }

        private static Vector3 Gait(float t, int joint, bool running = false, float variation = 1f)
        {
            Vector3 upper = OUTMotorData.Pose(0, joint);
            float pelvisDrop = running ? .08f : .04f;
            float cycle=t*Mathf.PI*2f;
            Vector3 bodyOffset=new Vector3(.015f*Mathf.Cos(cycle),-pelvisDrop+.008f*(1f-Mathf.Cos(cycle*2f)),0);
            if (joint < 10)
            {
                if(joint==5||joint==6||joint==8||joint==9)
                {
                    int shoulderJoint=joint<7?4:7;
                    float side=shoulderJoint==4?-1f:1f;
                    float angle=Mathf.Cos(cycle)*(side<0?-1f:1f)*(running?.55f:.30f);
                    float forearmAngle=angle+(running?1.05f:.30f);
                    Vector3 shoulder=OUTMotorData.Pose(0,shoulderJoint)+bodyOffset;
                    float armLength=Vector3.Distance(OUTMotorData.Pose(0,shoulderJoint),OUTMotorData.Pose(0,shoulderJoint+1));
                    float forearmLength=Vector3.Distance(OUTMotorData.Pose(0,shoulderJoint+1),OUTMotorData.Pose(0,shoulderJoint+2));
                    Vector3 elbow=shoulder+new Vector3(side*.10f,-Mathf.Cos(angle),Mathf.Sin(angle)).normalized*armLength;
                    return joint==shoulderJoint+1?elbow:elbow+new Vector3(side*.07f,-Mathf.Cos(forearmAngle),Mathf.Sin(forearmAngle)).normalized*forearmLength;
                }
                return upper+bodyOffset;
            }
            int hipJoint = joint < 14 ? 10 : 14, kneeJoint = hipJoint + 1, ankleJoint = hipJoint + 2;
            Vector3 hip = OUTMotorData.Pose(0, hipJoint) + bodyOffset;
            if (joint == hipJoint) return hip;
            Vector3 baseKnee = OUTMotorData.Pose(0, kneeJoint), baseAnkle = OUTMotorData.Pose(0, ankleJoint);
            float footPhase = t + (hipJoint == 10 ? 0f : .5f);
            footPhase -= Mathf.Floor(footPhase);
            float stride = (running ? .3f : .22f) * Mathf.Clamp(variation, .85f, 1.1f);
            float clearance = running ? .19f : .13f;
            float z, lift = 0;
            if (footPhase < StanceFraction)
                z = stride * (1f - 2f * footPhase / StanceFraction);
            else
            {
                float swing = (footPhase - StanceFraction) / (1f - StanceFraction);
                float ease = swing * swing * (3f - 2f * swing);
                z = stride * (2f * ease - 1f);
                lift = clearance * Mathf.Sin(swing * Mathf.PI);
            }
            Vector3 ankle = new Vector3(baseAnkle.x, Mathf.Max(baseAnkle.y, OUTMotorData.Radii[ankleJoint]) + lift, baseAnkle.z + z);
            if (joint == ankleJoint) return ankle;
            if (joint == hipJoint + 3)
            {
                Vector3 toeOffset = OUTMotorData.Pose(0, joint) - baseAnkle;
                return ankle + toeOffset;
            }
            // Bake an anatomically forward-bending knee, preserving the source segment lengths.
            float thigh = Vector3.Distance(OUTMotorData.Pose(0, hipJoint), baseKnee);
            float shin = Vector3.Distance(baseKnee, baseAnkle);
            Vector3 delta = ankle - hip;
            float distance = Mathf.Clamp(delta.magnitude, .001f, thigh + shin - .001f);
            Vector3 direction = delta.normalized;
            float along = (thigh * thigh - shin * shin + distance * distance) / (2f * distance);
            float bend = Mathf.Sqrt(Mathf.Max(0f, thigh * thigh - along * along));
            Vector3 forwardBend = Vector3.ProjectOnPlane(Vector3.forward, direction).normalized;
            return hip + direction * along + forwardBend * bend;
        }

        private static Vector3 Bake(OUTMotorMotionFamily family, int v, float t, int joint)
        {
            bool mirror = (v & 1) != 0;
            int j = mirror ? OUTMotorData.Mirrored[joint] : joint;
            float wave = Mathf.Sin(t * Mathf.PI * 2f), pulse = Mathf.Sin(t * Mathf.PI);
            float variation = .78f + v * .065f;
            Vector3 p = OUTMotorData.Pose(0, j);
            switch (family)
            {
                case OUTMotorMotionFamily.Walk:
                case OUTMotorMotionFamily.Run:
                    p = Gait(t, j, family == OUTMotorMotionFamily.Run, variation);
                    if (family == OUTMotorMotionFamily.Run && (j == 0 || j == 1 || j == 2 || j == 3)) p.z += .05f * variation;
                    if ((v == 4 || v == 5) && j < 10) p.x += .025f * wave;
                    break;
                case OUTMotorMotionFamily.Crouch: p = OUTMotorData.Blend(0, 2, .65f + .2f * variation, j); break;
                case OUTMotorMotionFamily.Guard: p = OUTMotorData.Blend(0, 3, .72f + .12f * variation, j); break;
                case OUTMotorMotionFamily.Strike:
                case OUTMotorMotionFamily.Throw:
                    p = OUTMotorData.Blend(0, 3, .3f + .4f * pulse, j);
                    if (j == 6 || (v == 4 && j == 9)) p += new Vector3(-.06f, family == OUTMotorMotionFamily.Throw ? .24f * pulse : .08f * pulse, .46f * pulse * variation);
                    if (j == 5) p.z += .16f * pulse;
                    break;
                case OUTMotorMotionFamily.Kick:
                    p = OUTMotorData.Blend(0, 3, .35f, j);
                    if (j == 11) p += new Vector3(0, .16f * pulse, .19f * pulse);
                    if (j == 12 || j == 13) p += new Vector3(0, .24f * pulse * variation, .42f * pulse * variation);
                    if (j <= 3) p.z -= .045f * pulse;
                    break;
                case OUTMotorMotionFamily.Hit:
                    p = OUTMotorData.Blend(0, v == 4 ? 2 : 3, .35f * pulse, j);
                    if (j <= 3) p += new Vector3((v == 2 || v == 3 ? .1f : .025f) * pulse, -.06f * pulse, (v == 1 ? .16f : -.12f) * pulse);
                    break;
                case OUTMotorMotionFamily.Fall:
                    p = OUTMotorData.Blend(0, v == 1 ? 11 : v == 4 ? 12 : 10, t * t * (3f - 2f * t), j);
                    break;
                case OUTMotorMotionFamily.Recovery:
                    float path = t * (RecoveryPoses.Length - 1);
                    int key = Mathf.Min((int)path, RecoveryPoses.Length - 2);
                    int first = key == 0 && (v == 2 || v == 3) ? 11 : RecoveryPoses[key];
                    p = OUTMotorData.Blend(first, RecoveryPoses[key + 1], path - key, j);
                    break;
                case OUTMotorMotionFamily.Drunk:
                    p = Gait(t, j);
                    if (j < 10) p.x += (.025f + .004f * v) * wave * Mathf.Clamp01(p.y);
                    if (j == 6 || j == 9) p.y += .08f * Mathf.Sin(t * Mathf.PI * 4f + j);
                    break;
                case OUTMotorMotionFamily.Sport:
                    p = OUTMotorData.Blend(0, 3, .65f, j); p.y += .045f * (1f - Mathf.Cos(t * Mathf.PI * 4f));
                    if (j >= 10) p = Gait(t, j, false, variation);
                    if (j == 6 || j == 9) p.z += .1f * wave;
                    break;
                case OUTMotorMotionFamily.Zombie:
                    p = Gait(t, j);
                    if (j == 5 || j == 8) p.z += .14f * variation;
                    if (j == 6 || j == 9) p += new Vector3(0, .14f + wave * .025f, .38f * variation);
                    if (j == 2 || j == 3) p.z += .08f;
                    break;
                case OUTMotorMotionFamily.Police:
                    p = OUTMotorData.Blend(0, 3, .7f, j);
                    if (j >= 10) p = Gait(t, j, false, variation);
                    else if (v == 0 || v == 2) p = Vector3.LerpUnclamped(Gait(t, j), p, j >= 4 && j <= 9 ? .8f : .15f);
                    if (j == 6) p.z += .15f * variation;
                    break;
                case OUTMotorMotionFamily.Protest:
                    p = v == 0 ? Gait(t, j) : OUTMotorData.Blend(0, v == 7 ? 2 : 3, .25f, j);
                    if (j >= 10) p = Gait(t, j, false, variation);
                    if (j == 6 || j == 9) p.y += (v == 2 || v == 6 ? .65f : .16f) + .06f * wave;
                    break;
                default:
                    if (j == 2 || j == 3) p.y += .008f * wave;
                    if (j == 3) p.x += (v - 3.5f) * .005f;
                    break;
            }
            // Small deterministic posture variants, with feet kept at source height.
            // Motion remains synthetic; these offsets do not replace authored mocap clips.
            if (j == 2 || j == 3) p.x += (v - 3.5f) * .003f;
            if (j == 6 || j == 9) p.z += (v - 3.5f) * .008f;
            if (mirror) p.x = -p.x;
            return p;
        }
    }
}
