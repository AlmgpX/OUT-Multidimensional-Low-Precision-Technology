// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;



namespace OUT.Motor

{

    public enum OUTMotorProjectileKind : byte { Stone, Arrow, Bullet }

    /// <summary>Bounded SoA projectile pool with swept point/sphere tests against actual joints.</summary>

    public sealed class OUTMotorProjectiles

    {

        public const int Capacity = 256;

        private readonly Vector3[] position=new Vector3[Capacity], velocity=new Vector3[Capacity];

        private readonly float[] life=new float[Capacity], radius=new float[Capacity], impulse=new float[Capacity];

        private readonly int[] owner=new int[Capacity];

        private readonly byte[] alive=new byte[Capacity];

        private readonly OUTMotorProjectileKind[] kind=new OUTMotorProjectileKind[Capacity];

        private int slot;

        private const int BucketCount=16384;

        private const float CellSize=1.5f;

        private readonly int[] heads=new int[BucketCount];

        private int[] nextNode,cellX,cellZ;

        public int LastNarrowPhaseTests { get; private set; }

        public int Active { get; private set; }

        public event Action<int,int,Vector3,float> OnImpact; // actor, joint, contact point, damage

        public event Action<int,int,float> OnDamage; // victim, shooter, applied damage

        public Vector3 GetPosition(int i) {return position[i];}

        public float GetRadius(int i) {return radius[i];}

        public bool IsActive(int i) {return alive[i]!=0;}

        public OUTMotorProjectileKind GetKind(int i) {return kind[i];}

        public Vector3 GetVelocity(int i) {return velocity[i];}

        /// <summary>Clear the demonstration projectile pool without allocating.</summary>

        public void Clear()

        {

            System.Array.Clear(alive, 0, alive.Length);

            Active = 0;

            slot = 0;

        }



        public void Fire(Vector3 spawn,Vector3 direction,float speed,float physicalImpulse,float ballRadius,int shooter=-1,

                         OUTMotorProjectileKind projectileKind=OUTMotorProjectileKind.Stone)

        {

            int idx=-1;

            for(int i=0;i<Capacity;i++){int k=(slot+i)%Capacity;if(alive[k]==0){idx=k;break;}}

            if(idx<0)idx=slot;

            if(alive[idx]==0)Active++;

            alive[idx]=1;slot=(idx+1)%Capacity;

            position[idx]=spawn;

            velocity[idx]=direction.normalized*speed;

            life[idx]=5f;radius[idx]=Mathf.Max(.02f,ballRadius);

            impulse[idx]=physicalImpulse;owner[idx]=shooter;

            kind[idx]=projectileKind;

        }

        public void FireArrow(Vector3 spawn,Vector3 direction,float speed,float physicalImpulse,int shooter=-1)

        { Fire(spawn,direction,speed,physicalImpulse,.045f,shooter,OUTMotorProjectileKind.Arrow); }

        public void FireBullet(Vector3 spawn,Vector3 direction,int shooter=-1,float physicalImpulse=12)

        { Fire(spawn,direction,85,physicalImpulse,.025f,shooter,OUTMotorProjectileKind.Bullet); }



        private static int Hash(int x,int z) { unchecked { return ((x*73856093)^(z*19349663))&(BucketCount-1); } }

        private void RebuildNodes(OUTMotorWorld world)

        {

            int count=world.Count*OUTMotorWorld.Joints;

            if(nextNode==null || nextNode.Length<count)

            { nextNode=new int[count];cellX=new int[count];cellZ=new int[count]; }

            for(int i=0;i<heads.Length;i++)heads[i]=-1;

            for(int i=0;i<count;i++)

            {

                if(world.VisualArchetype[i/OUTMotorWorld.Joints]==7)continue;

                Vector3 p=world.Position[i];

                int x=Mathf.FloorToInt(p.x/CellSize),z=Mathf.FloorToInt(p.z/CellSize),h=Hash(x,z);

                cellX[i]=x;cellZ[i]=z;nextNode[i]=heads[h];heads[h]=i;

            }

        }

        private static bool SegmentSphere(Vector3 center,Vector3 start,Vector3 delta,float radius,out float t)

        {

            Vector3 offset=start-center;

            float c=offset.sqrMagnitude-radius*radius;

            t=0;if(c<=0)return true;

            float a=delta.sqrMagnitude;

            if(a<1e-12f)return false;

            float b=Vector3.Dot(offset,delta),disc=b*b-a*c;

            if(b>0||disc<0)return false;

            t=(-b-Mathf.Sqrt(disc))/a;

            return t>=0 && t<=1;

        }

        private void TestNode(OUTMotorWorld world,int node,int shooter,Vector3 from,Vector3 delta,float r,

                              ref float closest,ref int victim,ref int joint)

        {

            int actor=node/OUTMotorWorld.Joints,bone=node%OUTMotorWorld.Joints;

            if(world.VisualArchetype[actor]==7)return;

            if(actor==shooter)return;

            if(shooter>=0 && shooter<world.Count && world.Faction[actor]==world.Faction[shooter])return;

            LastNarrowPhaseTests++;

            if(SegmentSphere(world.Position[node],from,delta,r+OUTMotorData.Radii[bone],out float t) && t<closest)

            { closest=t;victim=actor;joint=bone; }

        }

        public void Tick(OUTMotorWorld world,float deltaTime)

        {

            LastNarrowPhaseTests=0;

            if(Active==0)return;

            RebuildNodes(world);

            for(int projectile=0;projectile<Capacity;projectile++)

            {

                if(alive[projectile]==0)continue;

                Vector3 from=position[projectile],next=from+velocity[projectile]*deltaTime;

                velocity[projectile]+=Vector3.down*9.81f*deltaTime;

                float closestT=2f;int victim=-1,joint=-1;

                // Index actual nodes, including detached limbs. A pelvis proxy can miss those entirely.

                Vector3 delta=next-from;

                float extent=radius[projectile]+.16f;

                int x0=Mathf.FloorToInt((Mathf.Min(from.x,next.x)-extent)/CellSize);

                int x1=Mathf.FloorToInt((Mathf.Max(from.x,next.x)+extent)/CellSize);

                int z0=Mathf.FloorToInt((Mathf.Min(from.z,next.z)-extent)/CellSize);

                int z1=Mathf.FloorToInt((Mathf.Max(from.z,next.z)+extent)/CellSize);

                long cells=((long)x1-x0+1)*((long)z1-z0+1);

                if(cells>4096)

                {

                    // Bound traversal work for unusually long sweeps; retain exact collision semantics.

                    for(int node=0;node<world.Count*OUTMotorWorld.Joints;node++)

                        TestNode(world,node,owner[projectile],from,delta,radius[projectile],ref closestT,ref victim,ref joint);

                }

                else for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++)

                {

                    for(int node=heads[Hash(x,z)];node!=-1;node=nextNode[node])

                    {

                        if(cellX[node]!=x||cellZ[node]!=z)continue;

                        TestNode(world,node,owner[projectile],from,delta,radius[projectile],ref closestT,ref victim,ref joint);

                    }

                }

                if(victim>=0)

                {

                    Vector3 point=Vector3.Lerp(from,next,closestT);

                    Vector3 hitImpulse=velocity[projectile].normalized*impulse[projectile];

                    float damage=impulse[projectile]*.38f;

                    world.ApplyDamage(victim,joint,hitImpulse,damage);

                    OnDamage?.Invoke(victim,owner[projectile],damage);

                    OnImpact?.Invoke(victim,joint,point,damage);

                    alive[projectile]=0;Active--;continue;

                }

                position[projectile]=next;life[projectile]-=deltaTime;

                if(next.y<radius[projectile]||life[projectile]<=0f){alive[projectile]=0;Active--;}

            }

        }

    }

}
