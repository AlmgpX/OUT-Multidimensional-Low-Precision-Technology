// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.Diagnostics;

using UnityEngine;

using Debug = UnityEngine.Debug;



namespace OUT.Motor

{

    /// <summary>

    /// Pipeline-independent Unity 6 entrypoint for 1..4096 independent particle skeletons,

    /// shared flow fields, motor FSM, pooled physical rocks and instanced proxy rendering.

    /// Intentionally separated from other OUT CORE systems. No forced NavMeshAgent/Rigidbody.

    /// </summary>

    [DisallowMultipleComponent]

    public sealed class OUTMotorCrowd : MonoBehaviour

    {

        public OUTMotorSettings settings;

        [Tooltip("Optional shared spawn preset. Scenario demos can override it with two faction presets.")]

        public OUTMotorArchetype archetype;

        [Range(1,4096)] public int zombieCount=512;

        public GameObject meleeZombiePrefab;

        public GameObject throwerZombiePrefab;

        public Material teamAMaterial,teamBMaterial;

        public Vector3 teamAGoal=new Vector3(-12,0,0);

        public Vector3 teamBGoal=new Vector3(12,0,0);

        public bool simulate=true;

        public bool enableAI=true;

        public bool drawProxySkeletons=true;

        public bool enableBlood=true;

        public OUTMotorAnatomy defaultAnatomy=OUTMotorAnatomy.Human;

        [Tooltip("Explicit opt-in: use the private local bank instead of the shipped standard bank when no settings bank is assigned.")]

        public bool useLocalMotionBank=false;

        [Range(0,128)] public int maximumVisualRigs=16;



        public OUTMotorWorld World { get; private set; }

        public OUTMotorProjectiles Projectiles { get; private set; }

        public OUTMotorFlowField Flow { get; private set; }

        public OUTMotorBloodPool Blood { get; private set; }

        public OUTMotorInjuryScheduler Injuries { get; private set; }

        public float LastSimulationMilliseconds { get; private set; }

        public int DrawCalls { get { return rendererData==null?0:rendererData.DrawCalls; } }

        public bool UsesGpuPalette { get { return rendererData!=null && rendererData.UsesGpuPalette; } }



        private OUTMotorInstancedRenderer rendererData;

        private OUTMotorRigView[] rigViews;

        private float[] cooldown;

        private float accumulator,brainClock;

        private readonly Stopwatch stopwatch=new Stopwatch();

        private OUTMotorSettings ownedSettings;

        private const float H=OUTMotorWorld.StepSeconds;

        private int[] spatialHead,spatialNext;

        private int[] spatialX,spatialZ;

        private const int SpatialBuckets=16384;

        private const float CollisionCell=1.1f;

        public event Action<int,int,Vector3,float> Hit;



        private void Awake()

        {

            if(!settings)

            {

                ownedSettings=ScriptableObject.CreateInstance<OUTMotorSettings>();

                settings=ownedSettings;

            }

            zombieCount=Mathf.Clamp(zombieCount,1,4096);

            World=new OUTMotorWorld(settings,zombieCount);

            if(!settings.animationBank)

            {

                World.AnimationBank=Resources.Load<OUTMotorAnimationBank>("OUTMotor_Standard_Motion");

                if(useLocalMotionBank)

                {

                    var local=Resources.Load<OUTMotorAnimationBank>("OUTMotor_Local_Mixamo");

                    if(local)World.AnimationBank=local;

                }

            }

            Projectiles=new OUTMotorProjectiles();

            Blood=new OUTMotorBloodPool();

            Injuries=new OUTMotorInjuryScheduler(World.Capacity);

            Injuries.LimbLost+=(a,j)=>

            {

                if(enableBlood)Blood.Emit(World.GetJoint(a,OUTMotorData.Parents[j]),Vector3.up+World.Velocity[a*OUTMotorWorld.Joints+j]*.05f,28);

            };

            Projectiles.OnImpact+=(a,j,p,d)=>

            {

                if(enableBlood)Blood.Emit(p,Vector3.up+World.Velocity[a*OUTMotorWorld.Joints+j]*.08f,Mathf.Clamp((int)d,4,24));

                Hit?.Invoke(a,j,p,d);

            };

            World.Melee.OnHit+=(attacker,victim)=>

            {

                Vector3 point=World.Melee.LastHitPoint;

                int joint=World.Melee.LastHitJoint;

                if(enableBlood)Blood.Emit(point,Vector3.up+World.Velocity[attacker*OUTMotorWorld.Joints+9]*.05f,

                    Mathf.Clamp((int)World.Melee.LastHitDamage,4,24));

                Hit?.Invoke(victim,joint,point,World.Melee.LastHitDamage);

            };

            Flow=new OUTMotorFlowField(settings.navGridWidth,settings.navCellSize,Vector2.zero);

            Flow.Bake(settings.obstacleMask);

            Flow.Rebuild(teamAGoal,teamBGoal);

            cooldown=new float[World.Count];

            rendererData=new OUTMotorInstancedRenderer(teamAMaterial,teamBMaterial,World.Capacity);

            rigViews=new OUTMotorRigView[World.Count];

            spatialHead=new int[SpatialBuckets];spatialNext=new int[World.Count];

            spatialX=new int[World.Count];spatialZ=new int[World.Count];

            int live=0;

            for(int a=0;a<World.Count && live<maximumVisualRigs;a++)

            {

                GameObject prefab=World.Role[a]==(byte)OUTMotorRole.Thrower?throwerZombiePrefab:meleeZombiePrefab;

                if(!prefab)continue;

                var go=Instantiate(prefab,transform);

                go.name="OUT_Motor_"+a.ToString("D3");

                var view=go.GetComponent<OUTMotorRigView>();

                if(!view)view=go.AddComponent<OUTMotorRigView>();

                if(!view.Bind(World,a))

                {

                    Debug.LogWarning("OUT_Motor: prefab has no complete humanoid mapping. Leaving proxy for actor "+a,go);

                    Destroy(go);

                    continue;

                }

                rigViews[a]=view;rendererData.SetSkinned(a,true);live++;

            }

            for(int a=0;a<World.Count;a++)

            {

                World.SetAnatomy(a,defaultAnatomy);

                if(archetype)

                {

                    World.Spawn(a,World.GetJoint(a,0)-Vector3.up*OUTMotorData.Pose(0,0).y,World.Faction[a]==0?90:-90,

                        archetype.mass,World.Faction[a],OUTMotorRole.Zombie);

                    archetype.Apply(World,a,a);

                }

            }

        }

        /// <summary>Attach an already-built skinned rig without another per-agent physics component.</summary>

        public bool AttachVisualRig(int actor, OUTMotorRigView rig)

        {

            if (World == null || rendererData == null || rig == null ||

                actor < 0 || actor >= World.Count) return false;

            if (!rig.Bind(World, actor)) return false;

            rigViews[actor] = rig;

            rendererData.SetSkinned(actor, true);

            return true;

        }



        private void FixedUpdate()

        {

            if(!simulate||World==null)return;

            stopwatch.Restart();

            float interval=1f/Mathf.Max(1,settings.aiHz);

            brainClock+=Time.fixedDeltaTime;

            if(enableAI && brainClock>=interval)

            {

                // Do not run one pathfinding solver per actor.

                brainClock%=interval;UpdateBrains();

            }

            accumulator+=Time.fixedDeltaTime;

            int steps=0;

            while(accumulator>=H&&steps<6)

            {

                World.Tick();

                Injuries.Tick(World,settings.tearOverstressedLimbs,settings.tearLengthRatio);

                Projectiles.Tick(World,H);

                if(enableBlood)Blood.Tick(H);

                if(settings.enableBodyCollisions && (World.Frame&1)==0)ResolveCrowdContacts();

                accumulator-=H;steps++;

            }

            if(steps>=6&&accumulator>=H)accumulator=H; // bounded catch-up prevents spiral of death

            stopwatch.Stop();

            LastSimulationMilliseconds=(float)stopwatch.Elapsed.TotalMilliseconds;

        }

        private void LateUpdate()

        {

            if(drawProxySkeletons && rendererData!=null && World!=null)rendererData.Draw(World,Projectiles,enableBlood?Blood:null);

        }

        private void UpdateBrains()

        {

            for(int a=0;a<World.Count;a++)

            {

                if(World.GetState(a)!=OUTMotorState.Active)continue;

                cooldown[a]-=1f/Mathf.Max(1,settings.aiHz);

                Vector3 me=World.GetJoint(a,2);

                int other=(a^1);

                if(other>=World.Count)other=(a>0?a-1:0);

                if(other==a||World.Faction[a]==World.Faction[other])continue;

                Vector3 target=World.GetJoint(other,2);

                Vector3 delta=target-me;float distance=delta.magnitude;

                OUTMotorRole role=(OUTMotorRole)World.Role[a];

                bool rifle=World.VisualArchetype[a]==6;

                bool throwing=!rifle && role==OUTMotorRole.Thrower && distance<15f && distance>2f;

                float meleeReach=World.VisualArchetype[a]==10?1.8f:World.VisualArchetype[a]==5?1.5f:1.15f;

                float stop=rifle?9f:throwing?9f:meleeReach*.8f;

                if(distance>stop)

                {

                    Vector3 dir=settings.enableNavigation

                        ? Flow.Direction(World.GetJoint(a,0),World.Faction[a]==0?1:0)

                        : new Vector3(delta.x,0,delta.z).normalized;

                    if(dir.sqrMagnitude<.001f)dir=new Vector3(delta.x,0,delta.z).normalized;

                    World.SetLocomotion(a,dir,archetype?archetype.speed:throwing? 0.42f:.7f);

                }

                else World.SetLocomotion(a,Vector3.zero,0);

                if(cooldown[a]>0)continue;

                if(rifle && distance<20f)

                {

                    World.SetAction(a,OUTMotorAction.Guard);

                    Projectiles.FireBullet(World.GetJoint(a,9),target-World.GetJoint(a,9),a,archetype?archetype.impulse:12);

                    cooldown[a]=Mathf.Max(.18f,archetype?archetype.attackInterval*.35f:.35f);

                }

                else if(throwing && distance<15f)

                {

                    Vector3 origin=World.GetJoint(a,9);

                    Vector3 aim=(target-origin)+Vector3.up*(distance*.13f);

                    World.BeginAttack(a,OUTMotorAction.Throw);

                    Projectiles.Fire(origin,aim,settings.projectileVelocity,archetype?archetype.impulse:settings.projectileImpulse,

                                     settings.projectileRadius,a);

                    cooldown[a]=(archetype?archetype.attackInterval:2f)+(a%7)*.18f;

                }

                else if(!throwing && distance<meleeReach)

                {

                    if(World.Melee.TryBegin(a,other,archetype?archetype.damage:5f+(a%7),archetype?archetype.impulse:32f+(a%9)*4f))

                        cooldown[a]=Mathf.Max(OUTMotorWorld.AttackDuration,archetype?archetype.attackInterval:.75f)+(a%11)*.037f;

                }

            }

        }



        private void ResolveCrowdContacts()

        {

            for(int i=0;i<spatialHead.Length;i++)spatialHead[i]=-1;

            for(int a=0;a<World.Count;a++)

            {

                if(World.VisualArchetype[a]==7)continue;

                Vector3 p=World.GetJoint(a,0);

                int x=Mathf.FloorToInt(p.x/CollisionCell);

                int z=Mathf.FloorToInt(p.z/CollisionCell);

                spatialX[a]=x;spatialZ[a]=z;

                int cell=SpatialHash(x,z);

                spatialNext[a]=spatialHead[cell];spatialHead[cell]=a;

            }

            for(int a=0;a<World.Count;a++)

            {

                if(World.VisualArchetype[a]==7)continue;

                Vector3 p=World.GetJoint(a,0);

                int x=spatialX[a],z=spatialZ[a];

                for(int iz=-1;iz<=1;iz++)for(int ix=-1;ix<=1;ix++)

                {

                    int xx=x+ix,zz=z+iz;

                    for(int b=spatialHead[SpatialHash(xx,zz)];b!=-1;b=spatialNext[b])

                    {

                        if(b<=a||spatialX[b]!=xx||spatialZ[b]!=zz)continue;

                        Vector3 other=World.GetJoint(b,0),delta=new Vector3(other.x-p.x,0,other.z-p.z);

                        float lenSq=delta.sqrMagnitude;

                        // One centimetre of proxy overlap is deliberately unresolved:

                        // tiny torso corrections otherwise excite the limbs every contact pass.

                        const float acceptedSeparation=.69f;

                        if(lenSq>=acceptedSeparation*acceptedSeparation||lenSq<.000001f)continue;

                        float overlap=Mathf.Min(.045f,(acceptedSeparation-Mathf.Sqrt(lenSq))*.35f);

                        Vector3 correction=delta.normalized*overlap;

                        float mA=World.MassKg[a],mB=World.MassKg[b],total=mA+mB;

                        Vector3 shiftA=correction*(mB/total),shiftB=correction*(mA/total);

                        for(int j=0;j<3;j++)

                        {

                            World.Position[a*OUTMotorWorld.Joints+j]-=shiftA;

                            World.Position[b*OUTMotorWorld.Joints+j]+=shiftB;

                        }

                    }

                }

            }

        }

        private static int SpatialHash(int x,int z)

        {

            unchecked { return ((x*73856093)^(z*19349663))&(SpatialBuckets-1); }

        }



        [ContextMenu("OUT Motor / Fire 64 physics balls")]

        public void FireVolley64()

        {

            if(World==null)return;

            Camera c=Camera.main;

            Vector3 origin=c?c.transform.position:new Vector3(0,3,-20f);

            for(int n=0;n<64;n++)

            {

                int a=(n*79)%World.Count;

                Vector3 target=World.GetJoint(a,n%OUTMotorWorld.Joints);

                Vector3 jitter=new Vector3(((n*13)%9-4)*.04f,((n*7)%9-4)*.03f,0);

                float speed=Mathf.Max(8f,settings.projectileVelocity);

                Vector3 delta=target-origin+jitter;

                float travel=delta.magnitude/speed;

                Vector3 ballistic=delta+Vector3.up*(0.5f*9.81f*travel*travel);

                Projectiles.Fire(origin,ballistic.normalized,speed,settings.projectileImpulse,settings.projectileRadius);

            }

        }

        [ContextMenu("OUT Motor / Drop all")]

        public void DropAll() {if(World!=null)World.DropAll(new Vector3(8,20,95));}

        [ContextMenu("OUT Motor / Rebuild navigation")]

        public void RefreshNavigation()

        {

            if(Flow==null)return;

            Flow.Bake(settings.obstacleMask);Flow.Rebuild(teamAGoal,teamBGoal);

        }

        private void OnDestroy()

        {

            if(rendererData!=null)rendererData.Dispose();

            if(World!=null)World.Dispose();

            if(ownedSettings)

            {

                if(Application.isPlaying)Destroy(ownedSettings);else DestroyImmediate(ownedSettings);

            }

        }

        private void OnValidate()

        {

            zombieCount=Mathf.Clamp(zombieCount,1,4096);

            maximumVisualRigs=Mathf.Clamp(maximumVisualRigs,0,128);

        }

    }

}
