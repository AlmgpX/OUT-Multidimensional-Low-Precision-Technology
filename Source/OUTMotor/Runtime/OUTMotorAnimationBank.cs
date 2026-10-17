// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace OUT.Motor
{
    [Serializable]
    public struct OUTMotorBakedClip
    {
        public string name;
        public OUTMotorMotionFamily family;
        public float duration;
        public float nominalSpeed;
        public bool looping;
        public int frameOffset;
        public int frameCount;
    }

    /// <summary>Shared authored motion targets, sampled in the editor. Int16 millimetre
    /// vectors and two foot contact bits per frame; no Animator or clip decode at runtime.</summary>
    [CreateAssetMenu(menuName = "OUT/Motor/Authored Animation Bank")]
    public sealed class OUTMotorAnimationBank : ScriptableObject
    {
        public const int ChannelsPerFrame = OUTMotorData.Joints * 3;
        public int sampleRate = 30;
        public float quantization = .001f;
        public OUTMotorBakedClip[] clips = Array.Empty<OUTMotorBakedClip>();
        [HideInInspector] public short[] samples = Array.Empty<short>();
        [HideInInspector] public byte[] footContacts = Array.Empty<byte>();
        [SerializeField] private string sourceAvatar;
        private int[] lookup;
        private Dictionary<string,int> names;
        public int ClipCount => clips == null ? 0 : clips.Length;
        public int PackedBytes => (samples == null ? 0 : samples.Length * sizeof(short)) +
            (footContacts == null ? 0 : footContacts.Length);
        public string SourceAvatar { get => sourceAvatar; set => sourceAvatar = value; }

        public void Warmup()
        {
            if (lookup != null) return;
            lookup = new int[16];
            names = new Dictionary<string,int>(StringComparer.Ordinal);
            for (int i = 0; i < lookup.Length; i++) lookup[i] = -1;
            if (clips == null || samples == null || quantization <= 0 || float.IsNaN(quantization) || float.IsInfinity(quantization)) return;
            for (int i = 0; i < clips.Length; i++)
            {
                var clip = clips[i]; int family = (int)clip.family;
                if (family < 0 || family >= lookup.Length || !ValidClip(clip)) continue;
                if (lookup[family] < 0) lookup[family] = i;
                if (!string.IsNullOrEmpty(clip.name) && !names.ContainsKey(clip.name)) names.Add(clip.name,i);
            }
        }

        public bool TryGetClip(string name, out OUTMotorBakedClip clip)
        {
            Warmup();
            if (!string.IsNullOrEmpty(name) && names.TryGetValue(name,out int index))
            { clip=clips[index];return true; }
            clip=default;return false;
        }

        private bool ValidClip(OUTMotorBakedClip clip)
        {
            return samples!=null && quantization>0 && !float.IsNaN(quantization) && !float.IsInfinity(quantization)
                && clip.duration>0 && !float.IsNaN(clip.duration) && !float.IsInfinity(clip.duration)
                && clip.frameCount>=2 && clip.frameOffset>=0
                && ((long)clip.frameOffset+clip.frameCount)*ChannelsPerFrame<=samples.Length;
        }

        public bool TryGetClip(OUTMotorMotionFamily family, out OUTMotorBakedClip clip)
        {
            Warmup(); int f = (int)family;
            if (f >= 0 && f < lookup.Length && lookup[f] >= 0)
            { clip = clips[lookup[f]]; return true; }
            clip = default; return false;
        }

        private static float Frame(OUTMotorBakedClip clip, float seconds)
        {
            float normalized = clip.looping ? Mathf.Repeat(seconds, clip.duration) / clip.duration : Mathf.Clamp01(seconds / clip.duration);
            return normalized * (clip.frameCount - 1);
        }

        public bool TrySample(OUTMotorMotionFamily family, float seconds, int joint, out Vector3 target)
        {
            target = default;
            if (!TryGetClip(family,out var clip)) return false;
            return TrySample(clip,seconds,joint,out target);
        }

        /// <summary>Sample already-selected metadata without repeating family/name lookup per joint.</summary>
        public bool TrySample(OUTMotorBakedClip clip, float seconds, int joint, out Vector3 target)
        {
            target=default;
            if (joint<0 || joint>=OUTMotorData.Joints || float.IsNaN(seconds) || float.IsInfinity(seconds) || !ValidClip(clip)) return false;
            float f = Frame(clip, seconds); int first = (int)f;
            int a = (clip.frameOffset + first) * ChannelsPerFrame + joint * 3;
            int b = (clip.frameOffset + Mathf.Min(first + 1, clip.frameCount - 1)) * ChannelsPerFrame + joint * 3;
            float t = f - first;
            target = new Vector3(Mathf.Lerp(samples[a], samples[b], t),
                Mathf.Lerp(samples[a + 1], samples[b + 1], t), Mathf.Lerp(samples[a + 2], samples[b + 2], t)) * quantization;
            return true;
        }

        /// <returns>Bit 0: left foot planted. Bit 1: right foot planted. Zero if unavailable.</returns>
        public byte GetFootContactMask(OUTMotorMotionFamily family, float seconds)
        {
            if (float.IsNaN(seconds) || float.IsInfinity(seconds) || !TryGetClip(family, out var clip) || footContacts == null) return 0;
            int frame = clip.frameOffset + Mathf.Clamp(Mathf.RoundToInt(Frame(clip, seconds)), 0, clip.frameCount - 1);
            return frame < footContacts.Length ? (byte)(footContacts[frame] & 3) : (byte)0;
        }

        private void OnEnable() { lookup = null;names=null; }
        private void OnValidate() { lookup = null;names=null; }
    }
}
