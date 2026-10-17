// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;



namespace OUT.Motor

{

    /// <summary>

    /// Particle skeleton simulation (18 nodes per agent, SoA-style arrays).

    /// Not Unity Rigidbody/PhysX, not GPU simulation. Identical fixed-step solver for all agents.

    /// Shared 1087-byte pose/motor codebook. No per-agent MonoBehaviours.

    /// </summary>

    public sealed class OUTMotorWorld : IDisposable

    {

        public const int Joints = OUTMotorData.Joints;

        public const float StepSeconds = 1f / 120f;

        public const float AttackDuration = .85f;

        public const float GoalCorrectionResolution = .004f;

        private const int Links = 30;

        private const float Gravity = 9.81f;

        private static readonly int[] Controlled = { 0, 2, 6, 9, 12, 16, 3 };

        private static readonly int[] MotorProbe = { 0, 2, 6, 9 };

        private static readonly int[] CollisionA = {6,9,12,16,5,8,13,6,9,11,12,16};

        private static readonly int[] CollisionB = {2,2,2,2,7,4,17,9,14,15,16,12};

        private static readonly int[] RecoveryPose = {10,8,12,13,14,15,16,0};

        private static readonly float[] RecoveryTime = {.8f,.55f,.7f,.65f,.7f,.62f,.88f,.7f};



        public readonly int Capacity;

        public int Count { get; private set; }

        public int Frame { get; private set; }

        public float SimulationSeconds { get; private set; }

        public int Recoveries { get; private set; }

        public int SeveredCount { get; private set; }

        /// <summary>Scheduled pose refreshes, separate from the 120 Hz physical solver.</summary>

        public long TargetPoseUpdateCount { get; private set; }

        public readonly Vector3[] Position, Velocity, Goal, GoalVelocity;

        public readonly byte[] Faction, Role, Action, State, Stage, Branch, Anatomy;

        public readonly float[] Health, MassKg, Strength;

        public readonly byte[] DetachedGroup;

        public readonly byte[] VisualArchetype;

        public readonly OUTMotorSettings Settings;

        public OUTMotorMeleeCombat Melee { get; private set; }

        private OUTMotorAnimationBank _animationBank;

        public OUTMotorAnimationBank AnimationBank

        {

            get { return _animationBank; }

            set { _animationBank=value;if(value)value.Warmup(); }

        }

        /// <summary>Main-thread FIFO ingress. Bounded storage and bounded work per physics tick.</summary>

        public int MaximumEventsPerTick { get; set; } = 4096;

        public int PendingEvents { get { return _events.Count; } }

        public int EventCapacity { get { return _events.Capacity; } }

        public int ProcessedEventsLastTick { get; private set; }

        public long RejectedEvents { get; private set; }

        private readonly OUTMotorEventQueue _events;

        private readonly uint[] _generation;

        private readonly byte[] _motionFamily;

        private readonly OUTMotorFootPlanner _footPlanner;

        private readonly byte[] _footPlannerActive;

        private readonly OUTMotorNative _native;

        private readonly string _nativeLoadError;

        public string NativeSolverLoadError { get { return _nativeLoadError; } }

        private bool _disposed;

        public bool NativeSolverAvailable { get { return _native!=null && !_disposed; } }

        public int NativeWorkerLimit { get { return _native!=null ? _native.WorkerLimit : 0; } }

        public bool NativeSolverEnabled { get; set; } = true;

        public int NativeWorkerCount { get; set; } = Mathf.Min(4,Mathf.Max(1,Environment.ProcessorCount/2));

        public void Dispose()

        {

            if(_disposed)return;

            _native?.Dispose();

            _disposed=true;

        }



        private readonly Vector3[] _old, _start, _force, _anchor, _intent;

        private readonly Quaternion[] _rotation;

        private readonly float[] _invMass, _lambda, _length, _elapsed, _phase, _motorGain, _motorDamp;

        private readonly float[] _hurt, _attackTime, _stumbleTime;

        private readonly float[] _gaitPhase, _gaitRate, _locomotionBlend;

        private readonly byte[] _lastLocomotionAction;

        private readonly byte[] _contact, _contactCount, _motorCell, _pending, _dwell, _errBin, _speedBin;

        private readonly float[] _baseMass = new float[Joints];

        private readonly float[] _linkAlpha = new float[Links];

        // Target generation is sequential on the simulation thread, so one 18-joint scratch

        // buffer suffices for all actors. No additional per-actor pose copies are retained.

        private readonly Vector3[] _targetBefore = new Vector3[Joints];



        public OUTMotorWorld(OUTMotorSettings settings, int count, int eventCapacity = 8192)

        {

            Settings = settings;

            AnimationBank=settings?settings.animationBank:null;

            Capacity = Mathf.Clamp(count, 1, 4096);

            VisualArchetype=new byte[Capacity];

            _events = new OUTMotorEventQueue(eventCapacity);

            _generation = new uint[Capacity];

            _motionFamily = new byte[Capacity];

            _footPlanner = new OUTMotorFootPlanner(Capacity);

            _footPlannerActive = new byte[Capacity];

            // Warm the shared cache during construction, never on the first simulation tick.

            OUTMotorMotionCache.GetClip(0);

            int pts = Capacity * Joints;

            Position = new Vector3[pts]; Velocity = new Vector3[pts]; Goal = new Vector3[pts]; GoalVelocity = new Vector3[pts];

            _old = new Vector3[pts]; _start = new Vector3[pts]; _force = new Vector3[pts];

            _invMass = new float[pts]; _contact = new byte[pts]; DetachedGroup = new byte[pts];

            _lambda = new float[Capacity * Links];

            _length = new float[Links];

            _anchor = new Vector3[Capacity]; _rotation = new Quaternion[Capacity]; _intent = new Vector3[Capacity];

            _elapsed = new float[Capacity]; _phase = new float[Capacity]; _motorGain = new float[Capacity]; _motorDamp = new float[Capacity];

            _hurt = new float[Capacity]; _attackTime = new float[Capacity];_stumbleTime=new float[Capacity];

            _gaitPhase=new float[Capacity];_gaitRate=new float[Capacity];_locomotionBlend=new float[Capacity];

            _lastLocomotionAction=new byte[Capacity];

            _contactCount = new byte[Capacity]; _motorCell = new byte[Capacity]; _pending = new byte[Capacity]; _dwell = new byte[Capacity];

            _errBin = new byte[Capacity]; _speedBin = new byte[Capacity];

            Anatomy = new byte[Capacity];

            Faction = new byte[Capacity]; Role = new byte[Capacity]; Action = new byte[Capacity]; State = new byte[Capacity];

            Stage = new byte[Capacity]; Branch = new byte[Capacity]; Health = new float[Capacity]; MassKg = new float[Capacity];

            Strength = new float[Capacity];

            float total = 0f;

            for (int j=0; j<Joints; j++) total += OUTMotorData.RelativeMass[j];

            for (int j=0; j<Joints; j++) _baseMass[j] = OUTMotorData.RelativeMass[j] * 70f / total;

            for (int i=0; i<Links; i++)

            {

                _length[i] = Vector3.Distance(OUTMotorData.Pose(0, OUTMotorData.EdgesA[i]),

                                              OUTMotorData.Pose(0, OUTMotorData.EdgesB[i]));

                _linkAlpha[i] = ((i<17?2e-8f:5e-8f) / (StepSeconds * StepSeconds));

            }

            Count=Capacity;

            int columns=Mathf.Max(1,Mathf.CeilToInt(Mathf.Sqrt((Count+1)/2f)));

            int rows=Mathf.Max(1,Mathf.CeilToInt((Count+1)/2f/columns));

            for (int i=0;i<Count;i++)

            {

                float a=(i & 1)==0 ? -1f : 1f;

                int k=i>>1;

                Spawn(i,new Vector3(a*(1.5f+(k%columns)*1.03f),0, ((k/columns)-(rows-1)*.5f)*1.1f),a<0?90f:-90f,52f+(i*29%55),i%2,(i%5==0)?OUTMotorRole.Thrower:OUTMotorRole.Zombie);

            }

            _native=OUTMotorNative.TryCreate(Capacity,_baseMass,_length,Position,Velocity,Goal,GoalVelocity,_invMass,

                DetachedGroup,_contactCount,_rotation,_motorGain,_motorDamp,Strength,State,out _nativeLoadError);

            Melee=new OUTMotorMeleeCombat(this);

        }



        private int Id(int actor,int bone) { return actor*Joints+bone; }

        public Vector3 GetJoint(int actor, int bone) { return Position[Id(actor,bone)]; }

        public Quaternion GetHeading(int actor) { return _rotation[actor]; }

        public float GetLocomotionPhase(int actor) { return Mathf.Repeat(_gaitPhase[actor]+_phase[actor],1f); }

        public Vector3 GetAnchor(int actor) { return _anchor[actor]; }

        public OUTMotorState GetState(int actor) { return (OUTMotorState)State[actor]; }

        public bool IsDetached(int actor,int bone) { return DetachedGroup[Id(actor,bone)]!=0; }



        public void Spawn(int actor,Vector3 origin,float yawDegrees,float massKg,int faction,OUTMotorRole role)

        {

            if (actor < 0 || actor >= Count) throw new ArgumentOutOfRangeException(nameof(actor));

            _generation[actor]++;

            _motionFamily[actor]=byte.MaxValue;

            Anatomy[actor]=(byte)OUTMotorAnatomy.Human;

            VisualArchetype[actor]=0;

            _contactCount[actor]=_motorCell[actor]=_pending[actor]=_dwell[actor]=_errBin[actor]=_speedBin[actor]=0;

            _anchor[actor]=origin;

            _rotation[actor]=Quaternion.Euler(0,yawDegrees,0);

            Faction[actor]=(byte)(faction&1); Role[actor]=(byte)role; Action[actor]=(byte)OUTMotorAction.Idle;

            State[actor]=(byte)OUTMotorState.Active; Stage[actor]=0; Branch[actor]=(byte)((actor&1)<<1);

            _elapsed[actor]=0; _phase[actor]=(actor*.618034f)%1f; _motorGain[actor]=.6f; _motorDamp[actor]=1;

            Health[actor]=100; Strength[actor]=1; _hurt[actor]=0; _attackTime[actor]=0;_stumbleTime[actor]=0;

            _gaitPhase[actor]=0;_gaitRate[actor]=1;_locomotionBlend[actor]=0;_lastLocomotionAction[actor]=(byte)OUTMotorAction.Walk;

            Melee?.Cancel(actor);

            Melee?.CancelTarget(actor);

            MassKg[actor]=Mathf.Clamp(massKg,25,180);

            for (int j=0;j<Joints;j++)

            {

                int p=Id(actor,j);

                _invMass[p]=70f / (MassKg[actor]*_baseMass[j]);

                Position[p]=origin+_rotation[actor]*OUTMotorData.Pose(0,j);

                _old[p]=_start[p]=Goal[p]=Position[p];

                Velocity[p]=GoalVelocity[p]=_force[p]=_intent[actor]=Vector3.zero;

                DetachedGroup[p]=_contact[p]=0;

            }

            _footPlanner.ResetActor(actor,Position,actor*Joints,_rotation[actor]);

            _footPlannerActive[actor]=1;

        }



        /// <summary>Enqueue on the simulation thread; false means invalid input or a full queue.</summary>

        public bool QueueImpulse(int actor, int joint, Vector3 impulse)

        { return QueueEvent(OUTMotorEventKind.Impulse, actor, joint, impulse, 0f); }

        public bool QueueDamage(int actor, int joint, Vector3 impulse, float damage)

        { return QueueEvent(OUTMotorEventKind.Damage, actor, joint, impulse, damage); }

        public bool QueueSever(int actor, int joint)

        { return QueueEvent(OUTMotorEventKind.Sever, actor, joint, Vector3.zero, 0f); }

        private bool QueueEvent(OUTMotorEventKind kind, int actor, int joint, Vector3 impulse, float damage)

        {

            if ((uint)actor >= (uint)Count || (uint)joint >= Joints || !Finite(impulse.x) ||

                !Finite(impulse.y) || !Finite(impulse.z) || !Finite(damage))

            { RejectedEvents++; return false; }

            if (!_events.TryEnqueue(new OUTMotorEvent(kind, actor, joint, impulse, damage, _generation[actor])))

            { RejectedEvents++; return false; }

            return true;

        }

        private static bool Finite(float value) { return !float.IsNaN(value) && !float.IsInfinity(value); }

        private void DrainEvents()

        {

            ProcessedEventsLastTick = 0;

            int budget = Mathf.Clamp(MaximumEventsPerTick, 1, _events.Capacity);

            while (ProcessedEventsLastTick < budget && _events.TryDequeue(out OUTMotorEvent item))

            {

                ProcessedEventsLastTick++;

                // A respawn creates a new actor incarnation; stale queued hits must not damage it.

                if (item.Generation != _generation[item.Actor]) continue;

                switch (item.Kind)

                {

                    case OUTMotorEventKind.Impulse: ApplyImpulse(item.Actor, item.Joint, item.Impulse); break;

                    case OUTMotorEventKind.Damage: ApplyDamage(item.Actor, item.Joint, item.Impulse, item.Damage); break;

                    case OUTMotorEventKind.Sever: Sever(item.Actor, item.Joint); break;

                }

            }

        }



        public bool HasSupportLegLoss(int actor)

        { return IsDetached(actor,11)||IsDetached(actor,15); }

        public void SetAnatomy(int actor,OUTMotorAnatomy anatomy)

        {

            if((uint)actor>=(uint)Count)throw new ArgumentOutOfRangeException(nameof(actor));

            if(anatomy!=OUTMotorAnatomy.Human&&anatomy!=OUTMotorAnatomy.Zombie)

                throw new ArgumentOutOfRangeException(nameof(anatomy));

            Anatomy[actor]=(byte)anatomy;

            if(anatomy==OUTMotorAnatomy.Human&&HasSupportLegLoss(actor))BeginCrawl(actor);

        }

        /// <summary>Low prone locomotion after support-leg loss; never restores an upright recovery target.</summary>

        public void BeginCrawl(int actor)

        {

            if((uint)actor>=(uint)Count)throw new ArgumentOutOfRangeException(nameof(actor));

            if(State[actor]==(byte)OUTMotorState.Dead||State[actor]==(byte)OUTMotorState.Crawling)return;

            State[actor]=(byte)OUTMotorState.Crawling;

            Strength[actor]=.6f;

            _anchor[actor]=Position[actor*Joints];_anchor[actor].y=0;

            _elapsed[actor]=0;

            _intent[actor]=_rotation[actor]*Vector3.forward*.12f;

            Action[actor]=(byte)OUTMotorAction.Idle;

            for(int j=0;j<Joints;j++)_start[actor*Joints+j]=Goal[actor*Joints+j]=Position[actor*Joints+j];

        }



        /// <summary>Choose a shared locomotion/personality family; attacks and staged recovery retain their own clips.</summary>

        public void SetMotionFamily(int actor, OUTMotorMotionFamily family)

        {

            if ((uint)actor >= (uint)Count) throw new ArgumentOutOfRangeException(nameof(actor));

            if ((uint)family >= OUTMotorMotionCache.Count / OUTMotorMotionCache.VariantsPerFamily)

                throw new ArgumentOutOfRangeException(nameof(family));

            _motionFamily[actor]=(byte)family;

        }

        public void ClearMotionFamily(int actor)

        {

            if ((uint)actor >= (uint)Count) throw new ArgumentOutOfRangeException(nameof(actor));

            _motionFamily[actor]=byte.MaxValue;

        }

        private int SelectMotion(int actor, OUTMotorAction action)

        {

            OUTMotorMotionFamily family;

            switch(action)

            {

                case OUTMotorAction.Strike: family=OUTMotorMotionFamily.Strike; break;

                case OUTMotorAction.Throw: family=OUTMotorMotionFamily.Throw; break;

                case OUTMotorAction.Crouch: family=OUTMotorMotionFamily.Crouch; break;

                case OUTMotorAction.Guard: case OUTMotorAction.Reach: family=OUTMotorMotionFamily.Guard; break;

                default:

                    family=action==OUTMotorAction.Idle ? OUTMotorMotionFamily.Idle

                        : action==OUTMotorAction.Run ? OUTMotorMotionFamily.Run

                        : _motionFamily[actor]!=byte.MaxValue ? (OUTMotorMotionFamily)_motionFamily[actor]

                        : action==OUTMotorAction.Walk ? OUTMotorMotionFamily.Walk : OUTMotorMotionFamily.Idle;

                    break;

            }

            return OUTMotorMotionCache.Select(family,(uint)actor+1u);

        }

        private OUTMotorMotionFamily SelectAuthoredFamily(int actor,OUTMotorAction action)

        {

            switch(action)

            {

                case OUTMotorAction.Walk:

                    if(_motionFamily[actor]==(byte)OUTMotorMotionFamily.Drunk && _animationBank &&

                        _animationBank.TryGetClip(OUTMotorMotionFamily.Drunk,out _))return OUTMotorMotionFamily.Drunk;

                    return Anatomy[actor]==(byte)OUTMotorAnatomy.Zombie ? OUTMotorMotionFamily.Zombie:OUTMotorMotionFamily.Walk;

                case OUTMotorAction.Run: return OUTMotorMotionFamily.Run;

                case OUTMotorAction.Strike: return OUTMotorMotionFamily.Strike;

                case OUTMotorAction.Throw: return OUTMotorMotionFamily.Throw;

                case OUTMotorAction.Guard: case OUTMotorAction.Reach: return OUTMotorMotionFamily.Guard;

                case OUTMotorAction.Crouch: return OUTMotorMotionFamily.Crouch;

                default: return OUTMotorMotionFamily.Idle;

            }

        }



        public void SetAction(int actor, OUTMotorAction action)

        {

            if (State[actor]==(byte)OUTMotorState.Active && !IsAttacking(actor) && !IsStumbling(actor)) Action[actor]=(byte)action;

        }

        public void SetLocomotion(int actor, Vector3 worldDirection, float speed)

        {

            bool crawling=Anatomy[actor]==(byte)OUTMotorAnatomy.Human&&HasSupportLegLoss(actor);

            if(crawling){BeginCrawl(actor);speed=Mathf.Min(speed,.2f);}

            if(IsAttacking(actor)||IsStumbling(actor)){_intent[actor]=Vector3.zero;return;}

            Vector3 v = new Vector3(worldDirection.x,0,worldDirection.z);

            float mag=v.magnitude;

            _intent[actor]=mag>.001f ? v/mag*Mathf.Max(0,speed) : Vector3.zero;

            if(_intent[actor].sqrMagnitude>.0001f && State[actor]==0)

            {

                Action[actor]=(byte)(speed>1.0f?OUTMotorAction.Run:OUTMotorAction.Walk);

                _lastLocomotionAction[actor]=Action[actor];

            }

            else if(State[actor]==0 && _attackTime[actor]<=0) Action[actor]=(byte)OUTMotorAction.Idle;

        }

        public void Strike(int actor) { BeginAttack(actor,OUTMotorAction.Strike); }

        public bool IsAttacking(int actor) => _attackTime[actor]>0 &&

            (Action[actor]==(byte)OUTMotorAction.Strike || Action[actor]==(byte)OUTMotorAction.Throw);

        public bool IsStumbling(int actor) => _stumbleTime[actor]>0 && State[actor]==(byte)OUTMotorState.Active;

        public float GetAttackProgress(int actor) => Mathf.Clamp01(1f-_attackTime[actor]/AttackDuration);

        public void BeginAttack(int actor, OUTMotorAction action)

        {

            if(State[actor]!=(byte)OUTMotorState.Active || IsAttacking(actor)||IsStumbling(actor))return;

            Action[actor]=(byte)action;

            _intent[actor]=Vector3.zero;

            _attackTime[actor]=AttackDuration;

        }

        public void ApplyImpulse(int actor,int joint,Vector3 impulse)

        {

            int i=Id(actor,joint);Velocity[i]+=impulse*_invMass[i];

            _hurt[actor]=.9f;

            if(impulse.sqrMagnitude>1800f && State[actor]==0) StartFall(actor);

            else if(impulse.sqrMagnitude>400f && State[actor]==0 && _animationBank &&

                _animationBank.TryGetClip("Stumble",out _))

            {

                _stumbleTime[actor]=.6f;_attackTime[actor]=0;_intent[actor]=Vector3.zero;

                Action[actor]=(byte)OUTMotorAction.Idle;Melee?.Cancel(actor);

            }

        }

        public void ApplyDamage(int actor,int joint,Vector3 impulse,float damage)

        {

            if(State[actor]==(byte)OUTMotorState.Dead) return;

            ApplyImpulse(actor,joint,impulse);

            Health[actor]=Mathf.Max(0,Health[actor]-Mathf.Max(0,damage));

            if(Health[actor]<=0)

            {

                State[actor]=(byte)OUTMotorState.Dead;

                Strength[actor]=0;

            }

            else if(damage>32 && (joint==5||joint==8||joint==11||joint==15))

                Sever(actor,joint);

        }



        // Disconnection keeps physical particles alive, but removes cross-component links.

        public void Sever(int actor,int joint)

        {

            int group=joint==5||joint==6?1:joint==8||joint==9?2:joint==11||joint==12||joint==13?3:joint==15||joint==16||joint==17?4:0;

            if(group==0)return;

            int start=group==1?5:group==2?8:group==3?11:15;

            int end=group==1?6:group==2?9:group==3?13:17;

            if(DetachedGroup[Id(actor,start)]!=0)return;

            for(int j=start;j<=end;j++)DetachedGroup[Id(actor,j)]=(byte)group;

            SeveredCount++;

            if(group>=3&&Anatomy[actor]==(byte)OUTMotorAnatomy.Human)BeginCrawl(actor);

        }

        private bool SameGroup(int a,int j,int k) { return DetachedGroup[Id(a,j)]==DetachedGroup[Id(a,k)]; }

        public void StartFall(int actor)

        {

            if(State[actor]==(byte)OUTMotorState.Dead)return;

            State[actor]=(byte)OUTMotorState.Falling;_elapsed[actor]=0f;

            _attackTime[actor]=0;_stumbleTime[actor]=0;_intent[actor]=Vector3.zero;

            Action[actor]=(byte)OUTMotorAction.Idle;Melee?.Cancel(actor);

        }

        public void BeginRecovery(int actor)

        {

            if(Anatomy[actor]==(byte)OUTMotorAnatomy.Human&&HasSupportLegLoss(actor))

            { BeginCrawl(actor); return; }

            if(State[actor]==(byte)OUTMotorState.Dead)return;

            Stage[actor]=(byte)(Vector3.Dot(_rotation[actor]*Vector3.forward,

                                        (GetJoint(actor,2)-GetJoint(actor,0)).normalized)>.15f?0:1);

            State[actor]=(byte)OUTMotorState.Recovering;_elapsed[actor]=0f;

            int o=actor*Joints;

            for(int j=0;j<Joints;j++)_start[o+j]=Goal[o+j]=Position[o+j];

            _anchor[actor]=Position[o];_anchor[actor].y=0;

        }

        public void DropAll(Vector3 impulse)

        {

            for(int a=0;a<Count;a++){StartFall(a);ApplyImpulse(a,2,impulse);}

        }



        private static float Smooth(float t) { t=Mathf.Clamp01(t);return t*t*(3f-2f*t); }

        private void UpdateTarget(int a,float dt)

        {

            TargetPoseUpdateCount++;

            int o=a*Joints;

            if(State[a]!=(byte)OUTMotorState.Active)_footPlannerActive[a]=0;

            if(State[a]==(byte)OUTMotorState.Dead || State[a]==(byte)OUTMotorState.Passive) return;

            if(State[a]==(byte)OUTMotorState.Crawling)

            {

                float cycle=SimulationSeconds*3f+_phase[a]*Mathf.PI*2f;

                float blendIn=Smooth(_elapsed[a]/.35f);

                for(int j=0;j<Joints;j++)

                {

                    Vector3 local=OUTMotorData.Blend(8,13,.15f,j);

                    if(_animationBank && _animationBank.TrySample(OUTMotorMotionFamily.Crouch,SimulationSeconds+_phase[a],j,out Vector3 crawlLocal))

                        local=crawlLocal;

                    if(j==5||j==6||j==8||j==9)

                        local+=new Vector3(0,.025f*Mathf.Abs(Mathf.Sin(cycle)),.055f*Mathf.Sin(cycle+(j<7?0:Mathf.PI)));

                    Vector3 target=Vector3.LerpUnclamped(_start[o+j],_anchor[a]+_rotation[a]*local,blendIn);

                    GoalVelocity[o+j]=(target-Goal[o+j])/Mathf.Max(.008f,dt);

                    Goal[o+j]=target;

                }

                return;

            }

            if(State[a]==(byte)OUTMotorState.Falling)

            {

                for(int j=0;j<Joints;j++){Goal[o+j]=Position[o+j];GoalVelocity[o+j]=Vector3.zero;}

                float t=SimulationSeconds*6f+a;

                for(int n=0;n<2;n++)

                {

                    int joint=(n==0?6:9),parent=2;

                    Vector3 chest=Position[o+parent],head=Position[o];

                    if(_animationBank && _elapsed[a]<.85f && _animationBank.TryGetClip("Stumble",out OUTMotorBakedClip brace) &&

                        _animationBank.TrySample(brace,_elapsed[a]/.85f*brace.duration,joint,out Vector3 handLocal) &&

                        _animationBank.TrySample(brace,_elapsed[a]/.85f*brace.duration,2,out Vector3 chestLocal))

                    {Goal[o+joint]=chest+_rotation[a]*(handLocal-chestLocal);continue;}

                    Vector3 reach=new Vector3((n==0?-.29f:.29f)+Mathf.Sin(t+n*2f)*.12f,

                                             .23f+Mathf.Sin(t*1.13f+n*2)*.18f,

                                             Mathf.Cos(t*.93f+n*3)*.12f);

                    Goal[o+joint]=chest+reach+(chest-head).normalized*.12f;

                }

                return;

            }

            int p0=0,p1=1;float blend=.03f;

            OUTMotorAction act=(OUTMotorAction)Action[a];

            if(State[a]==(byte)OUTMotorState.Recovering)

            {

                int st=Stage[a];p0=-1;p1=RecoveryPose[st];

                if(st==0)p1=(Branch[a]&2)==0?10:11;

                blend=Smooth(_elapsed[a]/RecoveryTime[st]);

            }

            else if(act==OUTMotorAction.Walk||act==OUTMotorAction.Run)

            {

                float phase=(SimulationSeconds*(act==OUTMotorAction.Run?1.65f:.8f)+_phase[a])%1f*4;

                int i=(int)phase;p0=4+i;p1=4+((i+1)&3);blend=Smooth(phase-i);

            }

            else if(act==OUTMotorAction.Crouch){p0=0;p1=2;blend=.8f;}

            else if(act==OUTMotorAction.Guard){p0=0;p1=3;blend=.9f;}

            else if(act==OUTMotorAction.Reach||act==OUTMotorAction.Strike||act==OUTMotorAction.Throw)

            {p0=0;p1=3;blend=.28f;}

            OUTMotorAction poseAction=act==OUTMotorAction.Idle && _locomotionBlend[a]>0

                ? (OUTMotorAction)_lastLocomotionAction[a]:act;

            int clip=SelectMotion(a,poseAction);

            OUTMotorMotionClip motion=OUTMotorMotionCache.GetClip(clip);

            bool cached=State[a]==(byte)OUTMotorState.Active;

            bool gaitPose=poseAction==OUTMotorAction.Walk||poseAction==OUTMotorAction.Run;

            float motionTime=gaitPose?_gaitPhase[a]+_phase[a]:motion.Looping ? SimulationSeconds/motion.Duration+_phase[a]

                : act==OUTMotorAction.Strike||act==OUTMotorAction.Throw ? GetAttackProgress(a) : 0f;

            OUTMotorMotionFamily bankFamily=SelectAuthoredFamily(a,poseAction);

            OUTMotorBakedClip authoredClip=default;

            bool authored=cached && _animationBank && _animationBank.TryGetClip(bankFamily,out authoredClip);

            if(authored && act==OUTMotorAction.Strike && (VisualArchetype[a]==5||VisualArchetype[a]==10) &&

                _animationBank.TryGetClip("Sword_Attack",out OUTMotorBakedClip weaponClip))authoredClip=weaponClip;

            if(authored && (act==OUTMotorAction.Idle||act==OUTMotorAction.Guard) && (VisualArchetype[a]==5||VisualArchetype[a]==10) &&

                _animationBank.TryGetClip("Sword_Idle",out OUTMotorBakedClip readyClip))authoredClip=readyClip;

            bool locomotion=act==OUTMotorAction.Walk||act==OUTMotorAction.Run;

            float authoredSeconds=0;

            if(gaitPose)

                _gaitRate[a]=Mathf.Clamp(_intent[a].magnitude/Mathf.Max(.1f,authored?authoredClip.nominalSpeed:motion.NominalSpeed),.1f,2f)

                    /Mathf.Max(.1f,authored?authoredClip.duration:motion.Duration);

            if(authored)

            {

                authoredSeconds=gaitPose?(_gaitPhase[a]+_phase[a])*authoredClip.duration:

                    authoredClip.looping?SimulationSeconds+_phase[a]*authoredClip.duration:GetAttackProgress(a)*authoredClip.duration;

            }

            Vector3 basePos=_anchor[a];Quaternion rot=_rotation[a];

            OUTMotorBakedClip idleMotion=default,hitMotion=default,stumbleMotion=default;

            bool blendIdle=gaitPose && _animationBank && _animationBank.TryGetClip(OUTMotorMotionFamily.Idle,out idleMotion);

            bool blendHit=cached && _animationBank && _hurt[a]>0 && _animationBank.TryGetClip(OUTMotorMotionFamily.Hit,out hitMotion);

            bool blendStumble=cached && _stumbleTime[a]>0 && _animationBank && _animationBank.TryGetClip("Stumble",out stumbleMotion);

            OUTMotorBakedClip carryMotion=default;

            bool carryWeapon=gaitPose && _animationBank && (VisualArchetype[a]==5||VisualArchetype[a]==10) &&

                _animationBank.TryGetClip("Sword_Idle",out carryMotion);

            for(int j=0;j<Joints;j++)

            {

                int i=o+j;

                if(cached)_targetBefore[j]=Goal[i];

                Vector3 local=cached ? OUTMotorMotionCache.SampleNormalized(clip,motionTime,j)

                    : p0<0?OUTMotorData.Pose(p1,j):OUTMotorData.Blend(p0,p1,blend,j);

                if(authored && _animationBank.TrySample(authoredClip,authoredSeconds,j,out Vector3 authoredLocal))

                {

                    local=authoredLocal;

                }

                // Fade starts/stops; steady slow walking keeps the source stride and slows

                // its clock instead of shrinking the legs toward idle on every frame.

                if(gaitPose)

                {

                    Vector3 idle=OUTMotorData.Pose(0,j);

                    if(blendIdle && _animationBank.TrySample(idleMotion,SimulationSeconds,j,out Vector3 idleSample))idle=idleSample;

                    local=Vector3.LerpUnclamped(idle,local,_locomotionBlend[a]);

                }

                if(carryWeapon && j>=4 && j<=9 && _animationBank.TrySample(carryMotion,SimulationSeconds,j,out Vector3 carryLocal))

                    local=Vector3.LerpUnclamped(local,carryLocal,.85f);

                if(cached && !authored && act==OUTMotorAction.Strike && (j==8||j==9))

                {

                    float attack=GetAttackProgress(a);

                    Vector3 windup=j==9?new Vector3(.39f,1.52f,-.12f):new Vector3(.48f,1.38f,-.12f);

                    Vector3 contact=j==9?new Vector3(.20f,1.12f,.58f):new Vector3(.42f,1.25f,.26f);

                    float swing=Mathf.SmoothStep(0,1,Mathf.Clamp01((attack-.22f)/.32f));

                    Vector3 striking=Vector3.LerpUnclamped(windup,contact,swing);

                    if(attack<.22f)striking=Vector3.LerpUnclamped(OUTMotorData.Pose(0,j),windup,attack/.22f);

                    if(attack>.64f)striking=Vector3.LerpUnclamped(contact,OUTMotorData.Pose(0,j),(attack-.64f)/.36f);

                    local=striking;

                }

                if(blendHit && _animationBank.TrySample(hitMotion,(1f-_hurt[a]/.9f)*hitMotion.duration,j,out Vector3 hitLocal))

                    local=Vector3.LerpUnclamped(local,hitLocal,Mathf.Clamp01(_hurt[a]*.4f));

                if(blendStumble && _animationBank.TrySample(stumbleMotion,(1f-_stumbleTime[a]/.6f)*stumbleMotion.duration,j,out Vector3 stumbleLocal))

                    local=Vector3.LerpUnclamped(local,stumbleLocal,Mathf.Sin(Mathf.PI*(1f-_stumbleTime[a]/.6f))*.85f);

                Vector3 target=basePos+rot*local;

                if(p0<0)target=Vector3.LerpUnclamped(_start[i],target,blend);

                if((j==6||j==9)&&act==OUTMotorAction.Reach)

                {

                    float sign=j==6?-1:1;

                    float f=act==OUTMotorAction.Reach? 0.52f:act==OUTMotorAction.Throw? 0.43f:.45f;

                    target+=rot*new Vector3(sign*.08f,.14f,f*Mathf.Clamp01(_attackTime[a]+.4f));

                }

                GoalVelocity[i]=(target-Goal[i])/Mathf.Max(.008f,dt);

                Goal[i]=target;

            }

            if(cached && _stumbleTime[a]<=0)

            {

                if(_footPlannerActive[a]==0)

                {

                    _footPlanner.ResetActor(a,Position,o,rot);

                    _footPlannerActive[a]=1;

                }

                byte footMask=locomotion?(authored?_animationBank.GetFootContactMask(bankFamily,authoredSeconds)

                    :OUTMotorMotionCache.GetFootContactMask(clip,motionTime)):(byte)3;

                _footPlanner.Apply(a,dt,authored?authoredSeconds/authoredClip.duration:motionTime,footMask,

                    authored?authoredClip.duration:motion.Duration,locomotion?_intent[a]:Vector3.zero,

                    rot,basePos,Goal,GoalVelocity,DetachedGroup,o,act==OUTMotorAction.Run,authored);

                // Commit after the foot planner, which may replace sampled ankle/toe/knee

                // goals. Sub-resolution changes do not become held feed-forward velocity.

                float resolutionSquared=GoalCorrectionResolution*GoalCorrectionResolution;

                for(int j=0;j<Joints;j++)

                {

                    int i=o+j;

                    if(DetachedGroup[i]!=0)continue;

                    Vector3 previous=_targetBefore[j],delta=Goal[i]-previous;

                    if(delta.sqrMagnitude<=resolutionSquared)

                    {Goal[i]=previous;GoalVelocity[i]=Vector3.zero;}

                    else GoalVelocity[i]=delta/Mathf.Max(.008f,dt);

                }

            }

            else if(_stumbleTime[a]>0)_footPlannerActive[a]=0;

        }



        private static byte Bin(float x,float a,float b,float c,byte previous)

        {

            byte target=(byte)(x>c?3:x>b?2:x>a?1:0);

            if(target==previous)return target;

            float threshold=target>previous?(previous==0?a:previous==1?b:c):(target==0?a:target==1?b:c);

            float margin=Mathf.Abs(threshold)*.11f+.013f;

            if(target>previous&&x<threshold+margin)return previous;

            if(target<previous&&x>threshold-margin)return previous;

            return target;

        }

        private void MotorQuantize(int a)

        {

            int o=a*Joints;

            float error=0,speed=0;

            for(int i=0;i<4;i++)

            {

                int j=o+MotorProbe[i];error+=(Position[j]-Goal[j]).magnitude;

                Vector3 v=Velocity[j];speed+=Mathf.Abs(v.x)+Mathf.Abs(v.y)+Mathf.Abs(v.z);

            }

            error*=.25f;speed/=12f;

            byte eb=Bin(error,.08f,.25f,.58f,_errBin[a]),sb=Bin(speed,.4f,1.7f,4.3f,_speedBin[a]);

            _errBin[a]=eb;_speedBin[a]=sb;

            int cc=_contactCount[a];int cb=cc>5?3:cc>2?2:cc>0?1:0;

            int mode=State[a]==(byte)OUTMotorState.Falling?2:State[a]==(byte)OUTMotorState.Recovering?3:_attackTime[a]>0?1:0;

            byte cell=(byte)((mode<<6)|(eb<<4)|(sb<<2)|cb);

            if(_pending[a]==cell)_dwell[a]=(byte)Mathf.Min(255,_dwell[a]+1);

            else {_pending[a]=cell;_dwell[a]=0;}

            if(_dwell[a]>=6||mode==2)_motorCell[a]=cell;

            int profile=Settings&&Settings.responseProfile ? Settings.responseProfile.GetCode(_motorCell[a])

                : OUTMotorData.ResponseCode(_motorCell[a]>>6,(_motorCell[a]>>4)&3,(_motorCell[a]>>2)&3,_motorCell[a]&3);

            float target=OUTMotorData.ResponseGain(profile);

            if(mode==2)target=Mathf.Min(target,.29f);

            if(State[a]==(byte)OUTMotorState.Dead||State[a]==(byte)OUTMotorState.Passive)target=0;

            _motorGain[a]+=Mathf.Clamp(target-_motorGain[a],-.09f,.035f);

            _motorDamp[a]=OUTMotorData.ResponseDamping(profile);

        }



        public void Tick()

        {

            if(_disposed)throw new ObjectDisposedException(nameof(OUTMotorWorld));

            DrainEvents();

            Frame++;SimulationSeconds+=StepSeconds;

            int hz=Settings?Settings.motorHz:20;

            int every=Mathf.Max(1,120/Mathf.Max(1,hz));

            for(int a=0;a<Count;a++)

            {

                UpdateFSM(a);

                bool moving=State[a]==(byte)OUTMotorState.Active && _intent[a].sqrMagnitude>.0001f;

                if(moving)

                {

                    _gaitPhase[a]+=StepSeconds*_gaitRate[a];

                    if(_gaitPhase[a]>=1f)_gaitPhase[a]-=1f;

                }

                _locomotionBlend[a]=Mathf.MoveTowards(_locomotionBlend[a],moving?1f:0f,StepSeconds*5f);

                if(IsAttacking(a) && Melee!=null)

                {

                    int opponent=Melee.GetTarget(a);

                    if(opponent>=0)

                    {

                        Vector3 facing=GetJoint(opponent,0)-GetJoint(a,0);facing.y=0;

                        if(facing.sqrMagnitude>.001f)_rotation[a]=Quaternion.RotateTowards(_rotation[a],Quaternion.LookRotation(facing),360f*StepSeconds);

                    }

                }

                if((State[a]==(byte)OUTMotorState.Active||State[a]==(byte)OUTMotorState.Crawling) && _intent[a].sqrMagnitude>.002f)

                {

                    Vector3 delta=Position[a*Joints]-_anchor[a];

                    delta.y=0; // Anchor is on the ground; pelvis height must not disable horizontal locomotion.

                    if(delta.sqrMagnitude<.8f)

                    {

                        _anchor[a]+=_intent[a]*StepSeconds;

                        Quaternion towards=Quaternion.LookRotation(_intent[a].normalized,Vector3.up);

                        _rotation[a]=Quaternion.RotateTowards(_rotation[a],towards,150f*StepSeconds);

                    }

                }

                // Settled idle targets need only 10 Hz. A moving/finishing foot, attack,

                // hit, fall or recovery retains 20 Hz; the solver remains at 120 Hz.

                bool settled=State[a]==(byte)OUTMotorState.Active&&Action[a]==(byte)OUTMotorAction.Idle

                    &&_intent[a].sqrMagnitude<=.0001f&&_hurt[a]<=0&&_attackTime[a]<=0

                    &&!_footPlanner.HasUnfinishedStep(a);

                int targetEvery=settled?12:6;

                if((Frame-1+a)%targetEvery==0)UpdateTarget(a,targetEvery*StepSeconds);

                if((Frame-1+a)%every==0)MotorQuantize(a);

            }

            if(!(NativeSolverEnabled && _native!=null && _native.Step(Settings,Count,NativeWorkerCount)))

                for(int a=0;a<Count;a++)Integrate(a);

            Melee?.Tick();

        }

        private void UpdateFSM(int a)

        {

            _elapsed[a]+=StepSeconds;_hurt[a]=Mathf.Max(0,_hurt[a]-StepSeconds);

            _stumbleTime[a]=Mathf.Max(0,_stumbleTime[a]-StepSeconds);

            _attackTime[a]=Mathf.Max(0,_attackTime[a]-StepSeconds);

            var state=(OUTMotorState)State[a];

            if(state==OUTMotorState.Falling && Settings && Settings.enableAutoRecovery && _elapsed[a]>1.35f && _contactCount[a]>0)

                BeginRecovery(a);

            else if(state==OUTMotorState.Recovering)

            {

                int st=Stage[a];float duration=RecoveryTime[st];

                float pelvisY=Position[a*Joints].y;

                bool gate=st<4?_contactCount[a]>0:st<6?pelvisY>.28f:st==6?pelvisY>.57f:pelvisY>.68f;

                if(_elapsed[a]>duration&&(gate||_elapsed[a]>duration+1.1f))

                {

                    if(st>=7)

                    {

                        if(pelvisY>.65f){State[a]=(byte)OUTMotorState.Active;Recoveries++;Action[a]=(byte)OUTMotorAction.Idle;}

                        else {State[a]=(byte)OUTMotorState.Falling;}

                        _elapsed[a]=0;

                    }

                    else

                    {

                        for(int j=0;j<Joints;j++)_start[Id(a,j)]=Goal[Id(a,j)];

                        Stage[a]++;_elapsed[a]=0f;

                    }

                }

            }

            else if(state==OUTMotorState.Active && Position[a*Joints+2].y<.45f && _elapsed[a]>.8f)

                StartFall(a);

            if(_attackTime[a]==0 && (Action[a]==(byte)OUTMotorAction.Strike || Action[a]==(byte)OUTMotorAction.Throw)) Action[a]=(byte)OUTMotorAction.Idle;

        }



        private void Integrate(int a)

        {

            int o=a*Joints;

            bool active=State[a]!= (byte)OUTMotorState.Dead && State[a]!=(byte)OUTMotorState.Passive;

            for(int j=0;j<Joints;j++){_old[o+j]=Position[o+j];_force[o+j]=Vector3.zero;_contact[o+j]=0;}

            if(active)

            {

                for(int k=0;k<Controlled.Length;k++)

                {

                    int j=Controlled[k],idx=o+j;

                    if(DetachedGroup[idx]!=0)continue;

                    Vector3 displacement=Position[idx]-Goal[idx];

                    Vector3 sample=OUTMotorData.SampleField(displacement);

                    float gain=_motorGain[a]*Strength[a]*(Settings?Settings.muscleStrength:1f);

                    if(State[a]==(byte)OUTMotorState.Falling)gain*=((j==6||j==9)? 0.72f:.05f);

                    float g=k==0?420f:k>=4?240f:230f;

                    float damper=(k==0?34f:25f)*_motorDamp[a]*(Settings?Settings.damping:1f);

                    float maxForce=_baseMass[j]*95f*gain;

                    Vector3 desired=_baseMass[j]*gain*(g*sample-damper*(Velocity[idx]-.55f*GoalVelocity[idx]));

                    _force[idx]=new Vector3(Mathf.Clamp(desired.x,-maxForce,maxForce),

                                             Mathf.Clamp(desired.y,-maxForce,maxForce),

                                             Mathf.Clamp(desired.z,-maxForce,maxForce));

                }

                if(_contactCount[a]>0)

                {

                    float assist=State[a]==(byte)OUTMotorState.Crawling ? .2f : (Settings?Settings.supportAssistance:.9f);

                    for(int j=0;j<Joints;j++)if(DetachedGroup[o+j]==0)

                        _force[o+j].y += Gravity*assist/_invMass[o+j];

                }

                else

                {

                    // Enforce internal-only reaction when airborne; motors cannot accelerate COM for free.

                    Vector3 total=Vector3.zero;

                    for(int j=0;j<Joints;j++)total+=_force[o+j];

                    for(int j=0;j<Joints;j++)_force[o+j]-=total*(_baseMass[j]/70f);

                }

            }

            float drag=1f/(1f+(Settings?Settings.airResistance:.18f)*StepSeconds);

            for(int j=0;j<Joints;j++)

            {

                int idx=o+j;

                Velocity[idx]=(Velocity[idx]+StepSeconds*(_force[idx]*_invMass[idx]+Vector3.down*Gravity))*drag;

                Position[idx]+=Velocity[idx]*StepSeconds;

            }

            Array.Clear(_lambda,a*Links,Links);

            int iter=Settings?Settings.constraintIterations:3;

            for(int i=0;i<iter;i++)

            {

                if(Settings==null||Settings.enableJointLimits)SolveJoints(a);

                for(int j=0;j<Links;j++)SolveLength(a,j);

                GroundContacts(a);

            }

            if(Settings==null||Settings.enableSelfCollisions)SelfCollision(a);

            for(int j=0;j<Links;j++)SolveLength(a,j);

            GroundContacts(a);

            // Finish yaw after all ankle-moving length/contact projections, once per tick.

            if(Settings==null||Settings.enableJointLimits)

            {

                Vector3 forward=_rotation[a]*Vector3.forward,right=_rotation[a]*Vector3.right;

                FootYaw(a,12,13,forward,right);FootYaw(a,16,17,forward,right);

            }

            int count=0;

            for(int j=0;j<Joints;j++)

            {

                int idx=o+j;Vector3 velocity=(Position[idx]-_old[idx])/StepSeconds;

                Velocity[idx]=new Vector3(Mathf.Clamp(velocity.x,-18,18),Mathf.Clamp(velocity.y,-18,18),Mathf.Clamp(velocity.z,-18,18));

                if(_contact[idx]!=0)

                {

                    count++;Vector3 v=Velocity[idx];float fric=(j==12||j==13||j==16||j==17)? 0.72f:.87f;

                    Velocity[idx]=new Vector3(v.x*fric,Mathf.Max(0,v.y),v.z*fric);

                }

            }

            _contactCount[a]=(byte)count;

            DampLinks(a);

        }



        private void SolveLength(int a,int constraint)

        {

            int A=OUTMotorData.EdgesA[constraint],B=OUTMotorData.EdgesB[constraint];

            if(!SameGroup(a,A,B))return;

            int i=Id(a,A),j=Id(a,B),k=a*Links+constraint;

            Vector3 delta=Position[j]-Position[i];

            float length=delta.magnitude;

            if(length<1e-6f)return;

            float wi=_invMass[i],wj=_invMass[j],alpha=_linkAlpha[constraint];

            float dl=(-(length-_length[constraint])-alpha*_lambda[k])/(wi+wj+alpha);

            _lambda[k]+=dl;

            Vector3 direction=delta*(dl/length);

            Position[i]-=direction*wi;Position[j]+=direction*wj;

        }



        private void SolveJoints(int a)

        {

            // Low-cost angular cones; no acos and no articulation engine.

            Cone(a,10,11,12,-.866f,1f);Cone(a,14,15,16,-.866f,1f);

            Cone(a,4,5,6,-.906f,1f);Cone(a,7,8,9,-.906f,1f);

            Cone(a,0,1,2,.906f,1f);Cone(a,0,2,3,.573f,1f);

            // Plane branch (bend toward front of current avatar); prevents mirrored knee/elbow flipping.

            Vector3 forward=_rotation[a]*Vector3.forward;

            HingePlane(a,10,11,12, forward,.035f);

            HingePlane(a,14,15,16, forward,.035f);

            Vector3 right=_rotation[a]*Vector3.right;

            Vector3 up=_rotation[a]*Vector3.up;

            HingePlane(a,4,5,6,-.6f*right-.7f*up+.45f*forward,.11f);

            HingePlane(a,7,8,9,.6f*right-.7f*up+.45f*forward,.11f);

        }



        private void FootYaw(int actor,int ankle,int toe,Vector3 forward,Vector3 right)

        {

            if(State[actor]!=(byte)OUTMotorState.Active || IsDetached(actor,ankle) || IsDetached(actor,toe) || !SameGroup(actor,ankle,toe))return;

            int i=Id(actor,ankle),j=Id(actor,toe);

            Vector3 delta=Position[j]-Position[i],horizontal=new Vector3(delta.x,0,delta.z);

            float length=Mathf.Sqrt(delta.x*delta.x+delta.z*delta.z);

            if(length<1e-5f || Vector3.Dot(forward,horizontal)>=.5f*length)return;

            // A coarse yaw limit closes the otherwise free ankle swivel. No toe motor

            // or extra iterations; leave ankle position, toe height and foot length alone.

            float side=Vector3.Dot(right,horizontal)<-1e-5f?-1f:1f;

            Vector3 clamped=forward*(length*.5f)+right*(side*length*.8660254f);

            Vector3 position=Position[j];position.x=Position[i].x+clamped.x;position.z=Position[i].z+clamped.z;

            Position[j]=position;

        }



        private void Cone(int a,int j0,int j1,int j2,float minCos,float maxCos)

        {

            if(!SameGroup(a,j0,j1)||!SameGroup(a,j1,j2))return;

            int i=Id(a,j0),j=Id(a,j1),k=Id(a,j2);

            Vector3 u=Position[j]-Position[i],v=Position[k]-Position[j];

            float lu=u.magnitude,lv=v.magnitude;

            if(lu<1e-5f||lv<1e-5f)return;

            u/=lu;v/=lv;

            float cos=Vector3.Dot(u,v),target=Mathf.Clamp(cos,minCos,maxCos);

            if(Mathf.Abs(target-cos)<1e-5f)return;

            // Small-step PBD approximate gradient, mass weighted.

            Vector3 g0=-(v-cos*u)/lu, g2=(u-cos*v)/lv, g1=-g0-g2;

            float wi=_invMass[i],wj=_invMass[j],wk=_invMass[k];

            float denom=wi*g0.sqrMagnitude+wj*g1.sqrMagnitude+wk*g2.sqrMagnitude;

            if(denom<1e-7f)return;

            float lambda=Mathf.Clamp((target-cos)/denom,-.1f,.1f);

            Position[i]+=wi*lambda*g0;Position[j]+=wj*lambda*g1;Position[k]+=wk*lambda*g2;

        }

        private void HingePlane(int a,int j0,int j1,int j2,Vector3 reference,float slack)

        {

            if(!SameGroup(a,j0,j1)||!SameGroup(a,j1,j2))return;

            int i=Id(a,j0),j=Id(a,j1),k=Id(a,j2);

            Vector3 chord=(Position[k]-Position[i]).normalized;

            Vector3 normal=Vector3.ProjectOnPlane(reference,chord);

            if(normal.sqrMagnitude<.0001f)return;normal.Normalize();

            Vector3 middle=Position[j]-(Position[i]+Position[k])*.5f;

            float side=Vector3.Dot(middle,normal);

            if(side<.007f)

            {

                float m= Mathf.Min(.08f,.007f-side);

                Position[j]+=normal*(m*.8f);

                Position[i]-=normal*(m*.2f);

                Position[k]-=normal*(m*.2f);

            }

            Vector3 binormal=Vector3.Cross(chord,normal);

            float plane=Vector3.Dot(middle,binormal);

            if(Mathf.Abs(plane)>slack)Position[j]-=binormal*(plane-Mathf.Sign(plane)*slack)*.5f;

        }

        private void GroundContacts(int a)

        {

            int o=a*Joints;

            for(int j=0;j<Joints;j++)

            {

                int i=o+j;float min=OUTMotorData.Radii[j];

                // A 0.1 mm contact skin prevents tiny native/JIT rounding differences

                // from toggling floor friction (observed at 1.4 micrometers of separation).

                if(Position[i].y<min+.0001f)

                {

                    Vector3 p=Position[i];p.y=min;Position[i]=p;_contact[i]=1;

                }

            }

        }

        private void SelfCollision(int a)

        {

            for(int k=0;k<CollisionA.Length;k++)

            {

                int aa=CollisionA[k],bb=CollisionB[k];

                if(!SameGroup(a,aa,bb))continue;

                int i=Id(a,aa),j=Id(a,bb);

                Vector3 delta=Position[j]-Position[i];

                float d=delta.magnitude,limit=(OUTMotorData.Radii[aa]+OUTMotorData.Radii[bb])*.89f;

                if(d>=limit||d<.0001f)continue;

                float wi=_invMass[i],wj=_invMass[j],correction=Mathf.Min(.065f,limit-d);

                Vector3 n=delta*(correction/d/(wi+wj));

                Position[i]-=n*wi;Position[j]+=n*wj;

            }

        }

        private void DampLinks(int a)

        {

            float coeff=(Settings?Settings.damping:1f)*StepSeconds*7f;

            coeff=coeff/(1+coeff);

            for(int k=0;k<17;k++)

            {

                int j0=OUTMotorData.EdgesA[k],j1=OUTMotorData.EdgesB[k];

                if(!SameGroup(a,j0,j1))continue;

                int i=Id(a,j0),j=Id(a,j1);

                float wi=_invMass[i],wj=_invMass[j];

                Vector3 impulse=(Velocity[j]-Velocity[i])*(coeff/(wi+wj));

                Velocity[i]+=impulse*wi;Velocity[j]-=impulse*wj;

            }

        }

        public int ApproximateStateBytes()

        {

            // Managed CPU element payloads, excluding headers, shared caches, render/GPU and native context storage.

            int points=Capacity*Joints;

            int vectors=7*points+2*Capacity;

            int floats=points+Capacity*Links+9*Capacity+Links*2+Joints;

            return vectors*12+Capacity*16+floats*4+points*2+Capacity*14+Capacity*4+EventCapacity*32+Capacity*140;

        }

    }

}
