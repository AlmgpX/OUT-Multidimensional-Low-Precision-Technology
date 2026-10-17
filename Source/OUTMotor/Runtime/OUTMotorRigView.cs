// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;



namespace OUT.Motor

{

    /// <summary>

    /// Drives imported Unity skinned rigs from the shared 18-point motor.

    /// Keeps existing mesh, weights and bindposes. Rig root must face Unity +Z in its bind pose.

    /// Imported optimized hierarchies must expose the animated bones.

    /// </summary>

    [DisallowMultipleComponent]

    public sealed class OUTMotorRigView : MonoBehaviour

    {

        [SerializeField] private Animator humanoidAnimator;

        [SerializeField] private Transform[] joints = new Transform[OUTMotorData.Joints];

        [SerializeField, Range(0f,30f)] private float smoothing = 12f;

        [SerializeField] private bool disableAnimatorWhileMotorControlsRig = true;



        private static readonly int[] DriverChild = {1,2,3,2,5,6,5,8,9,8,11,12,13,12,15,16,17,16};

        private readonly Quaternion[] restRotation = new Quaternion[OUTMotorData.Joints];

        private readonly Vector3[] restDirection = new Vector3[OUTMotorData.Joints];

        private Vector3 hipRootOffset;

        private OUTMotorWorld world;

        private int actor = -1;

        private bool ready;



        public Transform[] Joints { get { return joints; } }

        public int Actor { get { return actor; } }

        public bool IsBound { get { return ready && world!=null; } }



        private Transform Human(HumanBodyBones id) {return humanoidAnimator && humanoidAnimator.isHuman?humanoidAnimator.GetBoneTransform(id):null;}

        public bool AutoMapHumanoid()

        {

            if(!humanoidAnimator)humanoidAnimator=GetComponentInChildren<Animator>();

            if(!humanoidAnimator||!humanoidAnimator.isHuman)return false;

            joints=new Transform[OUTMotorData.Joints];

            joints[0]=Human(HumanBodyBones.Hips);

            joints[1]=Human(HumanBodyBones.Spine) ? Human(HumanBodyBones.Spine) : Human(HumanBodyBones.Chest);

            joints[2]=Human(HumanBodyBones.Chest) ? Human(HumanBodyBones.Chest) : Human(HumanBodyBones.UpperChest);

            joints[3]=Human(HumanBodyBones.Head);

            joints[4]=Human(HumanBodyBones.LeftUpperArm);

            joints[5]=Human(HumanBodyBones.LeftLowerArm);

            joints[6]=Human(HumanBodyBones.LeftHand);

            joints[7]=Human(HumanBodyBones.RightUpperArm);

            joints[8]=Human(HumanBodyBones.RightLowerArm);

            joints[9]=Human(HumanBodyBones.RightHand);

            joints[10]=Human(HumanBodyBones.LeftUpperLeg);

            joints[11]=Human(HumanBodyBones.LeftLowerLeg);

            joints[12]=Human(HumanBodyBones.LeftFoot);

            joints[13]=Human(HumanBodyBones.LeftToes) ? Human(HumanBodyBones.LeftToes) : joints[12];

            joints[14]=Human(HumanBodyBones.RightUpperLeg);

            joints[15]=Human(HumanBodyBones.RightLowerLeg);

            joints[16]=Human(HumanBodyBones.RightFoot);

            joints[17]=Human(HumanBodyBones.RightToes) ? Human(HumanBodyBones.RightToes) : joints[16];

            if(!joints[1])joints[1]=joints[2];

            if(!joints[2])joints[2]=joints[1];

            return HasRequiredBones();

        }

        public void AssignBones(Transform[] mapping)

        {

            if(mapping==null||mapping.Length!=OUTMotorData.Joints)throw new ArgumentException("Expected 18 joint transforms.");

            Array.Copy(mapping,joints,OUTMotorData.Joints);

            ready=false;

        }

        public bool HasRequiredBones()

        {

            if(joints==null||joints.Length!=OUTMotorData.Joints)return false;

            // Toes are optional; rest must exist.

            for(int i=0;i<OUTMotorData.Joints;i++)if(!joints[i])return false;

            return true;

        }

        public bool Bind(OUTMotorWorld motorWorld,int actorIndex)

        {

            if(!HasRequiredBones()&&!AutoMapHumanoid())return false;

            world=motorWorld;actor=actorIndex;CaptureBindPose();

            if(disableAnimatorWhileMotorControlsRig && humanoidAnimator)humanoidAnimator.enabled=false;

            return true;

        }

        public void CaptureBindPose()

        {

            if(!HasRequiredBones()) {ready=false;return;}

            Quaternion inverse=Quaternion.Inverse(transform.rotation);

            hipRootOffset=transform.InverseTransformPoint(joints[0].position);

            for(int i=0;i<OUTMotorData.Joints;i++)

            {

                restRotation[i]=inverse*joints[i].rotation;

                int child=DriverChild[i];

                Vector3 direction=OUTMotorData.Parents[child]==i

                    ? joints[child].position-joints[i].position

                    : joints[i].position-joints[OUTMotorData.Parents[i]].position;

                // Duplicate optional bones can produce zero length, in which case use parent orientation.

                restDirection[i]=inverse*(direction.sqrMagnitude>.00001f?direction.normalized:Vector3.forward);

            }

            ready=true;

        }

        public void SetSmoothing(float value) { smoothing=Mathf.Max(0,value); }

        private void LateUpdate() { if(IsBound)Apply(); }

        public void Apply()

        {

            if(!IsBound||actor>=world.Count)return;

            Vector3 pelvis=world.GetJoint(actor,0);

            Quaternion heading=world.GetHeading(actor);

            // Root transform follows the simulated pelvis. All bone targets are solved in WORLD space.

            transform.rotation=heading;

            Vector3 offset=transform.TransformVector(hipRootOffset);

            transform.position=pelvis-offset;

            // Parents first; world-space assignments avoid accumulating parent correction drift.

            for(int i=0;i<OUTMotorData.Joints;i++)

            {

                Transform bone=joints[i];if(!bone)continue;

                int c=DriverChild[i];

                Vector3 desired = OUTMotorData.Parents[c]==i

                    ? world.GetJoint(actor,c)-world.GetJoint(actor,i)

                    : world.GetJoint(actor,i)-world.GetJoint(actor,OUTMotorData.Parents[i]);

                if(desired.sqrMagnitude<.00001f)continue;

                Quaternion baseRotation=heading*restRotation[i];

                Quaternion delta=Quaternion.FromToRotation(heading*restDirection[i],desired.normalized);

                Quaternion target=delta*baseRotation;

                float t=smoothing<=0?1f:1f-Mathf.Exp(-smoothing*Time.deltaTime);

                bone.rotation=Quaternion.Slerp(bone.rotation,target,t);

            }

        }

    }

}
