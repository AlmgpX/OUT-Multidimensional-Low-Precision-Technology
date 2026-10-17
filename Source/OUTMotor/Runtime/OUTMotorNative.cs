// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.Runtime.InteropServices;

using UnityEngine;

namespace OUT.Motor

{

    /// <summary>One C ABI call per world step. Stable managed arrays pinned for world lifetime; no per-step copies.</summary>

    internal sealed class OUTMotorNative : IDisposable

    {

        private const string Library="OUTMotorNative";

        [StructLayout(LayoutKind.Sequential)] private struct Buffers

        {

            public IntPtr Position,Velocity,Goal,GoalVelocity,InverseMass,Detached,ContactCount,Rotation,Gain,Damping,Strength,State;

        }

        [StructLayout(LayoutKind.Sequential)] private struct Config

        {

            public float MuscleStrength,Damping,SupportAssistance,AirResistance;

            public int Iterations,JointLimits,SelfCollisions,WorkerCount;

        }

        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] private static extern int out_motor_abi();

        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] private static extern IntPtr out_motor_create(int capacity,[In]float[] mass,[In]float[] length,[In]Vector3[] field);

        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] private static extern void out_motor_destroy(IntPtr context);

        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] private static extern int out_motor_step(IntPtr context,ref Buffers buffers,ref Config config,int count);

        [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] private static extern int out_motor_worker_limit();

        public int WorkerLimit { get; private set; }

        private readonly GCHandle[] pins=new GCHandle[12];

        private Buffers buffers;

        private IntPtr context;

        private int pinned;

        private IntPtr Pin(Array array){var handle=GCHandle.Alloc(array,GCHandleType.Pinned);pins[pinned++]=handle;return handle.AddrOfPinnedObject();}

        private OUTMotorNative(){}

        public static OUTMotorNative TryCreate(int capacity,float[] mass,float[] length,Vector3[] position,Vector3[] velocity,

            Vector3[] goal,Vector3[] goalVelocity,float[] inverseMass,byte[] detached,byte[] contactCount,

            Quaternion[] rotation,float[] gain,float[] damping,float[] strength,byte[] state,out string loadError)

        {

            loadError=null;

            var native=new OUTMotorNative();

            try

            {

                int abi=out_motor_abi();

                if(abi!=1){loadError="Native ABI mismatch: expected 1, received "+abi;return null;}

                native.WorkerLimit=out_motor_worker_limit();

                var field=new Vector3[27];

                for(int z=0;z<3;z++)for(int y=0;y<3;y++)for(int x=0;x<3;x++)

                    field[(z*3+y)*3+x]=OUTMotorData.SampleField(new Vector3(x-1,y-1,z-1)*OUTMotorData.FieldDomain);

                native.context=out_motor_create(capacity,mass,length,field);

                if(native.context==IntPtr.Zero){loadError="Native context construction failed.";return null;}

                native.buffers=new Buffers {Position=native.Pin(position),Velocity=native.Pin(velocity),Goal=native.Pin(goal),

                    GoalVelocity=native.Pin(goalVelocity),InverseMass=native.Pin(inverseMass),Detached=native.Pin(detached),

                    ContactCount=native.Pin(contactCount),Rotation=native.Pin(rotation),Gain=native.Pin(gain),Damping=native.Pin(damping),

                    Strength=native.Pin(strength),State=native.Pin(state)};

                return native;

            }

            catch(DllNotFoundException error){loadError=error.GetType().Name+": "+error.Message;native.Dispose();return null;}

            catch(EntryPointNotFoundException error){loadError=error.GetType().Name+": "+error.Message;native.Dispose();return null;}

            catch(BadImageFormatException error){loadError=error.GetType().Name+": "+error.Message;native.Dispose();return null;}

            catch{native.Dispose();throw;}

        }

        public bool Step(OUTMotorSettings settings,int count,int workers)

        {

            if(context==IntPtr.Zero)return false;

            var config=new Config {MuscleStrength=settings?settings.muscleStrength:1f,Damping=settings?settings.damping:1f,

                SupportAssistance=settings?settings.supportAssistance:.9f,AirResistance=settings?settings.airResistance:.18f,

                Iterations=Mathf.Clamp(settings?settings.constraintIterations:3,1,8),JointLimits=!settings||settings.enableJointLimits?1:0,

                SelfCollisions=!settings||settings.enableSelfCollisions?1:0,WorkerCount=Mathf.Clamp(workers,1,WorkerLimit)};

            return out_motor_step(context,ref buffers,ref config,count)!=0;

        }

        public void Dispose()

        {

            if(context!=IntPtr.Zero){out_motor_destroy(context);context=IntPtr.Zero;}

            for(int i=0;i<pinned;i++)if(pins[i].IsAllocated)pins[i].Free();

            pinned=0;

            GC.SuppressFinalize(this);

        }

        ~OUTMotorNative(){Dispose();}

    }

}
