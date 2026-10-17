// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;

namespace OUT.Motor

{

    public enum OUTMotorRTSRole : byte { Worker, Melee, Archer, Slinger }

    public enum OUTMotorRTSOrder : byte { Idle, Move, Gather, ReturnResources, Attack }

    public enum OUTMotorResourceKind : byte { Wood, Stone }



    /// <summary>Flat RTS economy/combat decisions over the existing particle world. No unit GameObjects.

    /// Decisions average 10 Hz per unit and are spread across calls with an explicit work budget.

    /// Goals use a 1/8 m grid; actual command byte packs order and one of 16 headings.

    /// Contact physics remains float precision in the world's existing solver.</summary>

    public sealed class OUTMotorRTSSimulation : IDisposable

    {

        public const float DecisionHz=10f,GoalGrid=.125f;

        private static readonly Vector3[] Headings={

            new Vector3(0,0,1),new Vector3(.38268343f,0,.92387953f),new Vector3(.70710678f,0,.70710678f),new Vector3(.92387953f,0,.38268343f),

            new Vector3(1,0,0),new Vector3(.92387953f,0,-.38268343f),new Vector3(.70710678f,0,-.70710678f),new Vector3(.38268343f,0,-.92387953f),

            new Vector3(0,0,-1),new Vector3(-.38268343f,0,-.92387953f),new Vector3(-.70710678f,0,-.70710678f),new Vector3(-.92387953f,0,-.38268343f),

            new Vector3(-1,0,0),new Vector3(-.92387953f,0,.38268343f),new Vector3(-.70710678f,0,.70710678f),new Vector3(-.38268343f,0,.92387953f)};

        private static readonly float[] Speeds={0,.55f,.85f,1.35f};

        private readonly OUTMotorWorld world;

        private readonly OUTMotorProjectiles projectiles;

        public readonly OUTMotorRTSRole[] UnitRoles;

        public readonly OUTMotorRTSOrder[] Orders;

        public readonly int[] TargetActor,TargetResource,CarriedResource;

        public readonly OUTMotorResourceKind[] CarriedKind;

        public readonly Vector3[] MoveGoals;

        public readonly byte[] DecisionCommand,SpeedCode;

        public readonly Vector3[] TeamBasePositions={new Vector3(-22,0,0),new Vector3(22,0,0)};

        public readonly Vector3[] ResourcePositions;

        public readonly int[] ResourceRemaining;

        public readonly OUTMotorResourceKind[] ResourceKinds;

        public readonly int[] Wood={60,60},Stone={30,30},Kills=new int[2],Score=new int[2];

        public readonly int[] DeliveredValue=new int[2];

        private readonly float[] gatherTimer,cooldown,lastDecision;

        private readonly int[] probeCursor,lastDamager;

        private readonly byte[] wasAlive;

        private float now,decisionCredit,recruitClock;

        private int cursor;

        private bool disposed;

        public bool EnemyAIEnabled { get; set; }=true;

        public bool PlayerAutoDefendEnabled { get; set; }

        public int MaximumDecisionsPerTick { get; set; }

        public int LastDecisionCount { get; private set; }

        public int LastTargetProbes { get; private set; }

        public long TotalDecisions { get; private set; }

        public int Count { get { return world.Count; } }

        public int ResourceNodeCount { get { return ResourcePositions.Length; } }

        public OUTMotorRTSSimulation(OUTMotorWorld world,OUTMotorProjectiles projectiles,int resourceNodes=12)

        {

            this.world=world??throw new ArgumentNullException(nameof(world));

            this.projectiles=projectiles??throw new ArgumentNullException(nameof(projectiles));

            int count=world.Count;

            UnitRoles=new OUTMotorRTSRole[count];Orders=new OUTMotorRTSOrder[count];

            TargetActor=new int[count];TargetResource=new int[count];CarriedResource=new int[count];

            CarriedKind=new OUTMotorResourceKind[count];

            MoveGoals=new Vector3[count];DecisionCommand=new byte[count];SpeedCode=new byte[count];

            gatherTimer=new float[count];cooldown=new float[count];lastDecision=new float[count];

            probeCursor=new int[count];lastDamager=new int[count];wasAlive=new byte[count];

            projectiles.OnDamage+=OnProjectileDamage;

            world.Melee.OnHit+=OnMeleeHit;

            MaximumDecisionsPerTick=Mathf.Max(1,(count+1)/2);

            resourceNodes=Mathf.Clamp(resourceNodes,1,16);

            ResourcePositions=new Vector3[resourceNodes];ResourceRemaining=new int[resourceNodes];ResourceKinds=new OUTMotorResourceKind[resourceNodes];

            for(int node=0;node<resourceNodes;node++)

            {

                float angle=node*(Mathf.PI*2f/resourceNodes);

                ResourcePositions[node]=QuantizeGoal(new Vector3(Mathf.Cos(angle)*12f,0,Mathf.Sin(angle)*12f));

                ResourceRemaining[node]=1200;ResourceKinds[node]=(OUTMotorResourceKind)(node&1);

            }

            for(int actor=0;actor<count;actor++)

            {

                int mix=(actor>>1)%8;

                SetRole(actor,mix<3?OUTMotorRTSRole.Worker:mix<5?OUTMotorRTSRole.Melee:mix<7?OUTMotorRTSRole.Archer:OUTMotorRTSRole.Slinger);

                TargetActor[actor]=TargetResource[actor]=lastDamager[actor]=-1;

                probeCursor[actor]=(actor*17)%count;

                wasAlive[actor]=(byte)(Alive(actor)?1:0);

            }

        }

        public void Dispose(){if(disposed)return;projectiles.OnDamage-=OnProjectileDamage;world.Melee.OnHit-=OnMeleeHit;disposed=true;}

        private void OnMeleeHit(int attacker,int victim)

        {if((uint)victim<(uint)Count&&(uint)attacker<(uint)Count)lastDamager[victim]=attacker;}

        private void OnProjectileDamage(int victim,int shooter,float damage)

        {if((uint)victim<(uint)Count&&(uint)shooter<(uint)Count&&damage>0)lastDamager[victim]=shooter;}

        /// <summary>Make a preallocated slot an invisible recruitable reserve, without awarding a kill.</summary>

        public bool ReserveActor(int actor)

        {

            if(disposed||(uint)actor>=(uint)Count)return false;

            world.SetLocomotion(actor,Vector3.zero,0);

            world.VisualArchetype[actor]=7;

            world.Health[actor]=0;world.Strength[actor]=0;world.State[actor]=(byte)OUTMotorState.Dead;

            wasAlive[actor]=0;CarriedResource[actor]=0;TargetActor[actor]=TargetResource[actor]=lastDamager[actor]=-1;

            Order(actor,OUTMotorRTSOrder.Idle);return true;

        }

        /// <summary>Spend resources and respawn one dead/reserved slot owned by the team.</summary>

        public bool TryRecruit(int team,OUTMotorRTSRole role)

        {

            if(disposed||(uint)team>1||(uint)role>3)return false;

            int wood=role==OUTMotorRTSRole.Worker?15:20,stone=role==OUTMotorRTSRole.Worker?0:10;

            if(Wood[team]<wood||Stone[team]<stone)return false;

            int actor=-1;

            for(int i=0;i<Count;i++)if((world.Faction[i]&1)==team&&!Alive(i)){actor=i;break;}

            if(actor<0)return false;

            AccountDeath(actor);

            Wood[team]-=wood;Stone[team]-=stone;

            Vector3 spawn=TeamBasePositions[team]+new Vector3((actor%4-1.5f)*.8f,0,((actor/4)%2)*.8f);

            float mass=role==OUTMotorRTSRole.Worker?65:role==OUTMotorRTSRole.Melee?85:role==OUTMotorRTSRole.Archer?70:68;

            world.Spawn(actor,spawn,team==0?90:-90,mass,team,

                role==OUTMotorRTSRole.Archer||role==OUTMotorRTSRole.Slinger?OUTMotorRole.Thrower:OUTMotorRole.Zombie);

            SetRole(actor,role);wasAlive[actor]=1;CarriedResource[actor]=0;

            TargetActor[actor]=TargetResource[actor]=lastDamager[actor]=-1;

            gatherTimer[actor]=0;cooldown[actor]=.5f;Order(actor,OUTMotorRTSOrder.Idle);

            return true;

        }

        private void AccountDeath(int actor)

        {

            if(wasAlive[actor]==0)return;

            int killer=lastDamager[actor];

            if((uint)killer<(uint)Count&&world.Faction[killer]!=world.Faction[actor])

            {

                int team=world.Faction[killer]&1;

                Kills[team]++;Score[team]=DeliveredValue[team]+Kills[team]*100;

            }

            wasAlive[actor]=0;CarriedResource[actor]=0;Order(actor,OUTMotorRTSOrder.Idle);

        }

        public OUTMotorRTSRole GetRole(int actor){return UnitRoles[actor];}

        public OUTMotorRTSOrder GetOrder(int actor){return Orders[actor];}

        public void SetRole(int actor,OUTMotorRTSRole role)

        {

            if((uint)actor>=(uint)Count||(uint)role>3)throw new ArgumentOutOfRangeException(nameof(actor));

            UnitRoles[actor]=role;

            world.VisualArchetype[actor]=role==OUTMotorRTSRole.Melee?(byte)10:(byte)((byte)role+1);

        }

        public static Vector3 QuantizeGoal(Vector3 position)

        {return new Vector3(Mathf.Round(position.x/GoalGrid)*GoalGrid,0,Mathf.Round(position.z/GoalGrid)*GoalGrid);}

        private static bool Finite(Vector3 p)

        {return !float.IsNaN(p.x)&&!float.IsInfinity(p.x)&&!float.IsNaN(p.z)&&!float.IsInfinity(p.z);}

        private bool Alive(int actor)

        {return world.Health[actor]>0&&world.GetState(actor)!=OUTMotorState.Dead;}

        private bool ValidActor(int actor){return !disposed&&(uint)actor<(uint)Count&&Alive(actor);}

        private void Order(int actor,OUTMotorRTSOrder order)

        {Orders[actor]=order;DecisionCommand[actor]=(byte)((DecisionCommand[actor]&240)|(byte)order);}

        public bool SetMoveOrder(int actor,Vector3 target)

        {

            if(!ValidActor(actor)||!Finite(target))return false;

            MoveGoals[actor]=QuantizeGoal(target);TargetActor[actor]=-1;Order(actor,OUTMotorRTSOrder.Move);return true;

        }

        public bool SetGatherOrder(int actor,int node)

        {

            if(!ValidActor(actor)||UnitRoles[actor]!=OUTMotorRTSRole.Worker||(uint)node>=(uint)ResourceNodeCount||ResourceRemaining[node]<=0)return false;

            TargetResource[actor]=node;TargetActor[actor]=-1;gatherTimer[actor]=0;

            Order(actor,CarriedResource[actor]>0?OUTMotorRTSOrder.ReturnResources:OUTMotorRTSOrder.Gather);return true;

        }

        public bool SetAttackOrder(int actor,int target)

        {

            if(!ValidActor(actor)||(uint)target>=(uint)Count||target==actor||!Alive(target)||world.Faction[actor]==world.Faction[target])return false;

            TargetActor[actor]=target;Order(actor,OUTMotorRTSOrder.Attack);return true;

        }

        public void Tick(float deltaTime)

        {

            if(disposed)throw new ObjectDisposedException(nameof(OUTMotorRTSSimulation));

            LastDecisionCount=LastTargetProbes=0;

            if(float.IsNaN(deltaTime)||float.IsInfinity(deltaTime)||deltaTime<=0)return;

            deltaTime=Mathf.Min(.1f,deltaTime);now+=deltaTime;

            decisionCredit=Mathf.Min(Count,decisionCredit+Count*DecisionHz*deltaTime);

            int budget=Mathf.Min(Mathf.Clamp(MaximumDecisionsPerTick,1,Count),Mathf.FloorToInt(decisionCredit));

            decisionCredit-=budget;

            for(int item=0;item<budget;item++)

            {

                int actor=cursor;if(++cursor==Count)cursor=0;

                float elapsed=Mathf.Clamp(now-lastDecision[actor],0,.5f);lastDecision[actor]=now;

                LastDecisionCount++;TotalDecisions++;

                if(!Alive(actor))

                {

                    AccountDeath(actor);

                    continue;

                }

                wasAlive[actor]=1;

                if(world.GetState(actor)!=OUTMotorState.Active&&world.GetState(actor)!=OUTMotorState.Crawling)continue;

                cooldown[actor]=Mathf.Max(0,cooldown[actor]-elapsed);

                if(EnemyAIEnabled&&world.Faction[actor]==1)EnemyOrder(actor);

                else if(PlayerAutoDefendEnabled&&world.Faction[actor]==0&&Orders[actor]==OUTMotorRTSOrder.Idle&&UnitRoles[actor]!=OUTMotorRTSRole.Worker)

                {

                    int threat=FindEnemy(actor);

                    if(threat>=0&&FlatDistanceSquared(world.GetJoint(actor,0),world.GetJoint(threat,0))<=64f)

                        SetAttackOrder(actor,threat);

                }

                switch(Orders[actor])

                {

                    case OUTMotorRTSOrder.Move: Move(actor,MoveGoals[actor],.3f);break;

                    case OUTMotorRTSOrder.Gather: Gather(actor,elapsed);break;

                    case OUTMotorRTSOrder.ReturnResources: ReturnResources(actor);break;

                    case OUTMotorRTSOrder.Attack: Attack(actor);break;

                    default: Stop(actor);break;

                }

            }

            recruitClock+=deltaTime;

            if(EnemyAIEnabled&&recruitClock>=5f)

            {

                recruitClock%=5f;int workers=0;

                for(int i=0;i<Count;i++)if(world.Faction[i]==1&&Alive(i)&&UnitRoles[i]==OUTMotorRTSRole.Worker)workers++;

                TryRecruit(1,workers<2?OUTMotorRTSRole.Worker:(OUTMotorRTSRole)(1+((int)(now*.2f)%3)));

            }

            for(int team=0;team<2;team++)Score[team]=DeliveredValue[team]+Kills[team]*100;

        }

        private void EnemyOrder(int actor)

        {

            if(UnitRoles[actor]==OUTMotorRTSRole.Worker)

            {

                if(Orders[actor]==OUTMotorRTSOrder.Idle)

                {int resource=NearestResource(world.GetJoint(actor,0));if(resource>=0)SetGatherOrder(actor,resource);}

                return;

            }

            if(Orders[actor]==OUTMotorRTSOrder.Attack&&ValidEnemy(actor,TargetActor[actor]))return;

            int target=FindEnemy(actor);

            if(target>=0)SetAttackOrder(actor,target);

        }

        private int NearestResource(Vector3 position)

        {

            int result=-1;float best=float.PositiveInfinity;

            for(int node=0;node<ResourceNodeCount;node++)if(ResourceRemaining[node]>0)

            {float distance=FlatDistanceSquared(position,ResourcePositions[node]);if(distance<best){best=distance;result=node;}}

            return result;

        }

        private bool ValidEnemy(int actor,int target)

        {return (uint)target<(uint)Count&&Alive(target)&&world.Faction[actor]!=world.Faction[target];}

        private int FindEnemy(int actor)

        {

            int result=-1;float best=float.PositiveInfinity;Vector3 me=world.GetJoint(actor,0);

            int samples=Mathf.Min(32,Count),start=probeCursor[actor];

            for(int probe=0;probe<samples;probe++)

            {

                int target=(start+probe)%Count;LastTargetProbes++;

                if(!ValidEnemy(actor,target))continue;

                float distance=FlatDistanceSquared(me,world.GetJoint(target,0));

                if(distance<best){best=distance;result=target;}

            }

            probeCursor[actor]=(start+samples)%Count;return result;

        }

        private static float FlatDistanceSquared(Vector3 a,Vector3 b)

        {float x=a.x-b.x,z=a.z-b.z;return x*x+z*z;}

        private void Stop(int actor)

        {SpeedCode[actor]=0;world.SetLocomotion(actor,Vector3.zero,0);}

        private bool Move(int actor,Vector3 target,float stoppingDistance)

        {

            Vector3 delta=target-world.GetJoint(actor,0);delta.y=0;

            if(delta.sqrMagnitude<=stoppingDistance*stoppingDistance)

            {

                Stop(actor);if(Orders[actor]==OUTMotorRTSOrder.Move)Order(actor,OUTMotorRTSOrder.Idle);return true;

            }

            int heading=0;float best=float.NegativeInfinity;

            for(int bin=0;bin<Headings.Length;bin++)

            {float dot=Vector3.Dot(delta,Headings[bin]);if(dot>best){best=dot;heading=bin;}}

            int speed=UnitRoles[actor]==OUTMotorRTSRole.Melee?3:UnitRoles[actor]==OUTMotorRTSRole.Slinger?1:2;

            DecisionCommand[actor]=(byte)((heading<<4)|(byte)Orders[actor]);SpeedCode[actor]=(byte)speed;

            world.SetLocomotion(actor,Headings[heading],Speeds[speed]);return false;

        }

        private void Gather(int actor,float elapsed)

        {

            int node=TargetResource[actor];

            if((uint)node>=(uint)ResourceNodeCount||ResourceRemaining[node]<=0)

            {

                if(CarriedResource[actor]>0){Order(actor,OUTMotorRTSOrder.ReturnResources);return;}

                node=NearestResource(world.GetJoint(actor,0));

                if(node<0){Order(actor,OUTMotorRTSOrder.Idle);Stop(actor);return;}

                TargetResource[actor]=node;

            }

            if(!Move(actor,ResourcePositions[node],1.15f))return;

            gatherTimer[actor]+=elapsed;

            int take=Mathf.Min(Mathf.FloorToInt(gatherTimer[actor]/.35f),Mathf.Min(ResourceRemaining[node],8-CarriedResource[actor]));

            if(take>0)

            {

                if(CarriedResource[actor]==0)CarriedKind[actor]=ResourceKinds[node];

                gatherTimer[actor]-=take*.35f;CarriedResource[actor]+=take;ResourceRemaining[node]-=take;

            }

            if(CarriedResource[actor]>=8||ResourceRemaining[node]<=0)Order(actor,OUTMotorRTSOrder.ReturnResources);

        }

        private void ReturnResources(int actor)

        {

            int team=world.Faction[actor]&1,node=TargetResource[actor];

            if(!Move(actor,TeamBasePositions[team],1.6f))return;

            if(CarriedResource[actor]>0&&(uint)node<(uint)ResourceNodeCount)

            {

                if(CarriedKind[actor]==OUTMotorResourceKind.Wood)Wood[team]+=CarriedResource[actor];else Stone[team]+=CarriedResource[actor];

                DeliveredValue[team]+=CarriedResource[actor]*(CarriedKind[actor]==OUTMotorResourceKind.Wood?1:2);

                CarriedResource[actor]=0;

            }

            if((uint)node>=(uint)ResourceNodeCount||ResourceRemaining[node]<=0)node=NearestResource(world.GetJoint(actor,0));

            TargetResource[actor]=node;gatherTimer[actor]=0;

            Order(actor,node>=0?OUTMotorRTSOrder.Gather:OUTMotorRTSOrder.Idle);

        }

        private void Attack(int actor)

        {

            int target=TargetActor[actor];

            if(!ValidEnemy(actor,target)){Order(actor,OUTMotorRTSOrder.Idle);Stop(actor);return;}

            var role=UnitRoles[actor];bool ranged=role==OUTMotorRTSRole.Archer||role==OUTMotorRTSRole.Slinger;

            float range=ranged?(role==OUTMotorRTSRole.Archer?13f:10f):role==OUTMotorRTSRole.Melee?1.8f:1.15f;

            Vector3 me=world.GetJoint(actor,2),victim=world.GetJoint(target,2);

            if(!Move(actor,victim,range*.9f))return;

            if(cooldown[actor]>0)return;

            if(ranged)

            {

                float speed=role==OUTMotorRTSRole.Archer?24f:16f;

                Vector3 origin=world.GetJoint(actor,9),delta=victim-origin;

                float travel=new Vector2(delta.x,delta.z).magnitude/speed;

                Vector3 aim=delta+Vector3.up*(.5f*9.81f*travel*travel);

                if(role==OUTMotorRTSRole.Archer)projectiles.FireArrow(origin,aim,speed,26f,actor);

                else projectiles.Fire(origin,aim,speed,46f,.16f,actor);

                world.BeginAttack(actor,OUTMotorAction.Throw);

                cooldown[actor]=role==OUTMotorRTSRole.Archer?1.15f:1.8f;

            }

            else

            {

                if(world.Melee.TryBegin(actor,target,role==OUTMotorRTSRole.Worker?3f:10f,7f))

                    cooldown[actor]=role==OUTMotorRTSRole.Worker?1.4f:OUTMotorWorld.AttackDuration;

            }

        }

    }

}
