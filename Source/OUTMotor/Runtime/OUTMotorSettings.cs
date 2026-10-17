// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using UnityEngine;



namespace OUT.Motor

{

    // Engine-side tuning and authoring. Shared by the entire crowd.

    [CreateAssetMenu(fileName="OUTMotorSettings", menuName="OUT Motor/Simulation settings")]

    public sealed class OUTMotorSettings : ScriptableObject

    {

        [Range(1,4096)] public int capacity = 512;

        [Range(1,8)] public int constraintIterations = 3;

        [Range(5,120)] public int aiHz = 10;

        [Range(5,120)] public int motorHz = 20;

        [Tooltip("Optional calibrated int4 policy. Empty keeps the supplied reference bank.")]

        public OUTMotorResponseProfile responseProfile;

        [Tooltip("Optional authored vector motion bank baked in the editor; no runtime Animator needed.")]

        public OUTMotorAnimationBank animationBank;

        [Range(.05f,3f)] public float muscleStrength = 1f;

        [Range(0f,5f)] public float damping = 1f;

        [Range(0f,1f)] public float supportAssistance = .9f;

        [Range(0f,2f)] public float airResistance = .18f;

        [Range(20f,180f)] public float averageMassKg = 70f;

        public bool enableJointLimits = true;

        public bool enableSelfCollisions = true;

        public bool enableBodyCollisions = true;

        public bool enableAutoRecovery = true;

        public bool enableNavigation = true;

        [Tooltip("Optional limb rupture after three 10 Hz audits of excessive stretch or extreme joint folding.")]

        public bool tearOverstressedLimbs = false;

        [Range(1.5f,3f)] public float tearLengthRatio = 1.8f;

        [Range(1,32)] public int maximumRenderedRigInstances = 24;

        public LayerMask obstacleMask = 0;

        [Range(.5f,3f)] public float navCellSize = 1.1f;

        [Range(16,128)] public int navGridWidth = 64;

        public float arenaHalfSize = 32f;

        public float projectileVelocity = 26f;

        public float projectileImpulse = 65f;

        public float projectileRadius = .16f;

    }



    public enum OUTMotorState : byte { Active, Falling, Recovering, Passive, Dead, Crawling }

    public enum OUTMotorAnatomy : byte { Human, Zombie }

    public enum OUTMotorRole : byte { Zombie, Thrower }

    public enum OUTMotorAction : byte { Idle, Walk, Run, Crouch, Guard, Reach, Strike, Throw }

}
