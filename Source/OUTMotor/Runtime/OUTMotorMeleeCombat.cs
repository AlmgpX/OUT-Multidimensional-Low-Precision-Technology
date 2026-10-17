// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;

namespace OUT.Motor

{

    public enum OUTMotorMeleeWeapon : byte { Auto, Unarmed, Baton, Sword }

    public enum OUTMotorMeleePhase : byte { Idle, Windup, Contact, Recovery }



    /// <summary>Shared visual/collision weapon transform. Distances are actual mesh-local metres.</summary>

    public static class OUTMotorWeaponGeometry

    {

        public static OUTMotorMeleeWeapon ResolveWeapon(OUTMotorWorld world,int actor)

        {return world.VisualArchetype[actor]==5?OUTMotorMeleeWeapon.Baton:world.VisualArchetype[actor]==10?OUTMotorMeleeWeapon.Sword:OUTMotorMeleeWeapon.Unarmed;}

        public static Vector3 TipLocal(OUTMotorMeleeWeapon weapon)

        {return weapon==OUTMotorMeleeWeapon.Baton?new Vector3(0,.545f,0):weapon==OUTMotorMeleeWeapon.Sword?new Vector3(0,.94f,0):Vector3.zero;}

        public static void GetPose(OUTMotorWorld world,int actor,OUTMotorMeleeWeapon weapon,

            out Vector3 origin,out Quaternion rotation,out Vector3 tip,out float radius)

        {

            if(weapon==OUTMotorMeleeWeapon.Auto)weapon=ResolveWeapon(world,actor);

            origin=world.GetJoint(actor,9);

            Vector3 forearm=origin-world.GetJoint(actor,8);

            rotation=forearm.sqrMagnitude>1e-8f?Quaternion.FromToRotation(Vector3.up,forearm.normalized):world.GetHeading(actor);

            tip=origin+rotation*TipLocal(weapon);

            radius=weapon==OUTMotorMeleeWeapon.Unarmed?.075f:.035f;

        }

    }



    /// <summary>One preselected opponent per attacker, one physical swept-tip hit per swing.

    /// Only post-physics particle positions are sampled. Pose goals and actor proximity never award damage.</summary>

    public sealed class OUTMotorMeleeCombat

    {

        public const float WindupSeconds=.18f,ContactEndSeconds=.60f;

        private readonly OUTMotorWorld world;

        private readonly int[] targets;

        private readonly float[] elapsed,damage,impulse;

        private readonly Vector3[] previousTip;

        private readonly OUTMotorMeleeWeapon[] weapons;

        private readonly byte[] active,hit;

        public event Action<int,int> OnHit;

        public int ActiveAttacks { get; private set; }

        public Vector3 LastHitPoint { get; private set; }

        public int LastHitJoint { get; private set; }

        public float LastHitDamage { get; private set; }

        public int LastContactTests { get; private set; }

        public long StartedAttacks { get; private set; }

        public long HitCount { get; private set; }

        public long MissCount { get; private set; }

        public long CanceledAttacks { get; private set; }

        public OUTMotorMeleeCombat(OUTMotorWorld world)

        {

            this.world=world??throw new ArgumentNullException(nameof(world));

            int count=world.Count;targets=new int[count];elapsed=new float[count];damage=new float[count];impulse=new float[count];

            previousTip=new Vector3[count];weapons=new OUTMotorMeleeWeapon[count];active=new byte[count];hit=new byte[count];

        }

        public int GetTarget(int actor){return active[actor]!=0?targets[actor]:-1;}

        public OUTMotorMeleePhase GetPhase(int actor)

        {

            if(active[actor]==0)return OUTMotorMeleePhase.Idle;

            return elapsed[actor]<WindupSeconds?OUTMotorMeleePhase.Windup:

                elapsed[actor]<ContactEndSeconds?OUTMotorMeleePhase.Contact:OUTMotorMeleePhase.Recovery;

        }

        private bool CanSwing(int actor)

        {

            return (uint)actor<(uint)world.Count&&world.Health[actor]>0&&world.GetState(actor)==OUTMotorState.Active

                &&!world.IsStumbling(actor)&&!world.IsDetached(actor,7)&&!world.IsDetached(actor,8)&&!world.IsDetached(actor,9);

        }

        private bool ValidTarget(int actor,int target)

        {return (uint)target<(uint)world.Count&&target!=actor&&world.Health[target]>0&&world.GetState(target)!=OUTMotorState.Dead

            &&world.VisualArchetype[target]!=7&&world.Faction[actor]!=world.Faction[target];}

        public bool TryBegin(int attacker,int target,float hitDamage,float physicalImpulse,OUTMotorMeleeWeapon weapon=OUTMotorMeleeWeapon.Auto)

        {

            if(!CanSwing(attacker)||!ValidTarget(attacker,target)||active[attacker]!=0||world.IsAttacking(attacker)

                ||float.IsNaN(hitDamage)||float.IsInfinity(hitDamage)||float.IsNaN(physicalImpulse)||float.IsInfinity(physicalImpulse)

                ||(uint)weapon>(uint)OUTMotorMeleeWeapon.Sword)return false;

            if(weapon==OUTMotorMeleeWeapon.Auto)weapon=OUTMotorWeaponGeometry.ResolveWeapon(world,attacker);

            targets[attacker]=target;weapons[attacker]=weapon;damage[attacker]=Mathf.Max(0,hitDamage);impulse[attacker]=Mathf.Max(0,physicalImpulse);

            elapsed[attacker]=0;hit[attacker]=0;active[attacker]=1;

            OUTMotorWeaponGeometry.GetPose(world,attacker,weapon,out _,out _,out previousTip[attacker],out _);

            world.BeginAttack(attacker,OUTMotorAction.Strike);ActiveAttacks++;StartedAttacks++;return true;

        }

        public void Cancel(int actor)

        {

            if((uint)actor>=(uint)world.Count||active[actor]==0)return;

            active[actor]=0;ActiveAttacks--;CanceledAttacks++;

        }

        public void CancelTarget(int target)

        {for(int actor=0;actor<world.Count;actor++)if(active[actor]!=0&&targets[actor]==target)Cancel(actor);}

        public void Tick(float deltaTime=OUTMotorWorld.StepSeconds)

        {

            LastContactTests=0;

            if(ActiveAttacks==0||float.IsNaN(deltaTime)||float.IsInfinity(deltaTime)||deltaTime<=0)return;

            deltaTime=Mathf.Min(deltaTime,.1f);

            for(int actor=0;actor<world.Count;actor++)

            {

                if(active[actor]==0)continue;

                int target=targets[actor];

                if(!CanSwing(actor)||!ValidTarget(actor,target)){Cancel(actor);continue;}

                float before=elapsed[actor];elapsed[actor]+=deltaTime;

                OUTMotorWeaponGeometry.GetPose(world,actor,weapons[actor],out Vector3 origin,out _,out Vector3 tip,out float radius);

                Vector3 from=previousTip[actor];previousTip[actor]=tip;

                if(hit[actor]==0&&elapsed[actor]>=WindupSeconds&&before<ContactEndSeconds)

                {

                    float closest=2;int joint=-1;Vector3 contactPoint=tip;

                    Vector3 fullPath=tip-from;

                    // A resting overlap is not an impact, even during the contact window.

                    if(fullPath.sqrMagnitude<(.35f*deltaTime)*(.35f*deltaTime))continue;

                    float start=Mathf.Clamp01((WindupSeconds-before)/deltaTime);

                    float end=Mathf.Clamp01((ContactEndSeconds-before)/deltaTime);

                    from+=fullPath*start;

                    Vector3 path=fullPath*(end-start);

                    float reach=OUTMotorWeaponGeometry.TipLocal(weapons[actor]).magnitude+2.4f;

                    bool nearby=(world.GetJoint(actor,0)-world.GetJoint(target,0)).sqrMagnitude<=reach*reach;

                    for(int bone=0;nearby&&bone<OUTMotorWorld.Joints;bone++)

                    {

                        if(world.IsDetached(target,bone))continue;

                        LastContactTests++;

                        if(Sweep(world.GetJoint(target,bone),from,path,radius+OUTMotorData.Radii[bone],out float t)&&t<closest)

                        {closest=t;joint=bone;contactPoint=from+path*t;}

                        // The current shaft is also a capsule: a long blade can overlap a torso while its tip passes beyond it.

                        if(weapons[actor]!=OUTMotorMeleeWeapon.Unarmed&&Sweep(world.GetJoint(target,bone),origin,tip-origin,radius+OUTMotorData.Radii[bone],out float bladeT)&&closest>0)

                        {closest=0;joint=bone;contactPoint=origin+(tip-origin)*bladeT;}

                    }

                    if(joint>=0)

                    {

                        Vector3 direction=path.sqrMagnitude>1e-8f?path.normalized:world.GetHeading(actor)*Vector3.forward;

                        if(world.QueueDamage(target,joint,direction*impulse[actor],damage[actor]))

                        {

                            hit[actor]=1;HitCount++;LastHitPoint=contactPoint;

                            LastHitJoint=joint;LastHitDamage=damage[actor];OnHit?.Invoke(actor,target);

                        }

                    }

                }

                if(elapsed[actor]>=OUTMotorWorld.AttackDuration)

                {active[actor]=0;ActiveAttacks--;if(hit[actor]==0)MissCount++;}

            }

        }

        private static bool Sweep(Vector3 center,Vector3 from,Vector3 path,float radius,out float t)

        {

            Vector3 offset=from-center;float c=offset.sqrMagnitude-radius*radius;

            t=0;if(c<=0)return true;

            float a=path.sqrMagnitude;if(a<1e-12f)return false;

            float b=Vector3.Dot(offset,path),discriminant=b*b-a*c;

            if(b>0||discriminant<0)return false;

            t=(-b-Mathf.Sqrt(discriminant))/a;return t>=0&&t<=1;

        }

    }

}
