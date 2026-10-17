// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using UnityEngine;

using UnityEngine.Rendering;



namespace OUT.Motor

{

    /// <summary>

    /// Shared GPU joint palette and procedural low-poly bodies, with a CPU-matrix fallback.

    /// No per-actor GameObjects for the procedural crowd.

    /// </summary>

    public sealed class OUTMotorInstancedRenderer : IDisposable

    {

        private const int Batch=1023;

        private readonly Matrix4x4[] matrices = new Matrix4x4[Batch];

        private readonly Matrix4x4[] projectileMatrices = new Matrix4x4[OUTMotorProjectiles.Capacity];

        private readonly bool[] skinned;

        private readonly Material[] materials=new Material[2];

        private readonly Mesh tube;

        private readonly bool[] ownsMaterial=new bool[2];

        private int batches;

        private readonly Material[] paletteMaterials = new Material[2];

        private ComputeBuffer nodeBuffer, actorBuffer;

        private readonly Vector4[] nodes;

        private readonly int[] actorIndices;

        private readonly int capacity;

        private readonly Material bloodMaterial;

        private readonly OUTMotorEquipmentRenderer equipment;

        public bool UsesGpuPalette { get { return nodeBuffer != null; } }

        public int DrawCalls { get { return batches; } }



        public OUTMotorInstancedRenderer(Material team0,Material team1,int maximumActors=512)

        {

            capacity=Mathf.Clamp(maximumActors,1,4096);

            skinned=new bool[capacity];nodes=new Vector4[capacity*OUTMotorWorld.Joints];

            actorIndices=new int[capacity*2];

            tube=CreateSixSidedTube();

            Material[] provided={team0,team1};

            Color[] colors={new Color(.35f,.70f,.50f),new Color(.62f,.46f,.40f)};

            for(int i=0;i<2;i++)

            {

                if(provided[i])materials[i]=provided[i];

                else

                {

                    Shader shader=Shader.Find("OUT/Motor/Demo Low Cost");

                    if(!shader)shader=Shader.Find("Universal Render Pipeline/Lit");

                    if(!shader)shader=Shader.Find("Standard");

                    materials[i]=new Material(shader);

                    if(materials[i].HasProperty("_BaseColor"))materials[i].SetColor("_BaseColor",colors[i]);

                    else materials[i].color=colors[i];

                    ownsMaterial[i]=true;

                }

                // Never mutate a caller-owned material asset.

                if(!ownsMaterial[i]) { materials[i]=new Material(materials[i]);ownsMaterial[i]=true; }

                materials[i].enableInstancing=true;

            }

            bloodMaterial=new Material(materials[1]) { enableInstancing=true };

            if(bloodMaterial.HasProperty("_BaseColor"))bloodMaterial.SetColor("_BaseColor",new Color(.42f,.018f,.025f));

            else bloodMaterial.color=new Color(.42f,.018f,.025f);

            Shader palette=Resources.Load<Shader>("OUTMotorCrowd");

            if(palette && palette.isSupported && SystemInfo.supportsInstancing && SystemInfo.supportsComputeShaders)

            {

                nodeBuffer=new ComputeBuffer(nodes.Length,16);

                actorBuffer=new ComputeBuffer(actorIndices.Length,4);

                for(int i=0;i<2;i++)

                {

                    paletteMaterials[i]=new Material(palette) { enableInstancing=true };

                    Color color=materials[i].HasProperty("_BaseColor")?materials[i].GetColor("_BaseColor"):materials[i].color;

                    paletteMaterials[i].SetColor("_BaseColor",color);

                    paletteMaterials[i].SetBuffer("_Nodes",nodeBuffer);

                    paletteMaterials[i].SetBuffer("_Actors",actorBuffer);

                }

            }

            equipment=new OUTMotorEquipmentRenderer(UsesGpuPalette?palette:null,materials[0],nodeBuffer,actorBuffer);

        }

        public void SetSkinned(int actor,bool enabled) {if(actor>=0&&actor<skinned.Length)skinned[actor]=enabled;}

        public void Draw(OUTMotorWorld world,OUTMotorProjectiles shots,OUTMotorBloodPool blood=null)

        {

            batches=0;

            if(UsesGpuPalette) { DrawPalette(world,shots);DrawEquipment(world);DrawBlood(blood);return; }

            for(int faction=0;faction<2;faction++)

            {

                int index=0;

                for(int a=0;a<world.Count;a++)

                {

                    if(world.Faction[a]!=faction||skinned[a]||world.VisualArchetype[a]==7)continue;

                    for(int j=1;j<OUTMotorWorld.Joints;j++)

                    {

                        int parent=OUTMotorData.Parents[j];

                        if(world.IsDetached(a,j)!=world.IsDetached(a,parent))continue;

                        Vector3 start=world.GetJoint(a,parent),end=world.GetJoint(a,j),axis=end-start;

                        float length=axis.magnitude;

                        if(length<.005f)continue;

                        float radius=OUTMotorData.Radii[j]*(.8f+(world.Role[a]==(byte)OUTMotorRole.Thrower? 0.15f:0));

                        matrices[index++]=Matrix4x4.TRS((start+end)*.5f,Quaternion.FromToRotation(Vector3.up,axis/length),new Vector3(radius,length,radius));

                        if(index==Batch){Flush(materials[faction],index);index=0;}

                    }

                }

                if(index>0)Flush(materials[faction],index);

            }

            int count=0;

            if(shots!=null)

            {

                for(int i=0;i<OUTMotorProjectiles.Capacity;i++)

                {

                    if(!shots.IsActive(i))continue;

                    float r=shots.GetRadius(i);

                    bool arrow=shots.GetKind(i)==OUTMotorProjectileKind.Arrow;

                    bool bullet=shots.GetKind(i)==OUTMotorProjectileKind.Bullet;

                    projectileMatrices[count++]=Matrix4x4.TRS(shots.GetPosition(i),(arrow||bullet)?Quaternion.FromToRotation(Vector3.up,shots.GetVelocity(i).normalized):Quaternion.identity,

                        bullet?new Vector3(.025f,.22f,.025f):arrow?new Vector3(.018f,.55f,.018f):new Vector3(r,r*2,r));

                }

                if(count>0)

                {

                    Array.Copy(projectileMatrices,matrices,count);

                    Flush(materials[1],count);

                }

            }

            DrawEquipment(world);

            DrawBlood(blood);

        }

        private void DrawEquipment(OUTMotorWorld world)

        {

            var pipeline=GraphicsSettings.currentRenderPipeline;

            float relative=pipeline&&pipeline.GetType().Name.Contains("HDRenderPipeline")?1:0;

            batches+=equipment.Draw(world,skinned,relative);

        }

        private void DrawBlood(OUTMotorBloodPool blood)

        {

            if(blood==null || blood.Active==0)return;

            int count=0;

            for(int i=0;i<OUTMotorBloodPool.Capacity;i++)

            {

                if(!blood.IsActive(i))continue;

                float r=blood.GetRadius(i);

                matrices[count++]=Matrix4x4.TRS(blood.GetPosition(i),Quaternion.identity,

                    new Vector3(r,blood.IsGrounded(i)?.004f:r*2,r));

                if(count==Batch) { Flush(bloodMaterial,count);count=0; }

            }

            if(count>0)Flush(bloodMaterial,count);

        }

        private void DrawPalette(OUTMotorWorld world,OUTMotorProjectiles shots)

        {

            Vector3 min=world.Position[0],max=min;

            int total=world.Count*OUTMotorWorld.Joints;

            for(int i=0;i<total;i++)

            {

                Vector3 p=world.Position[i];

                nodes[i]=new Vector4(p.x,p.y,p.z,world.DetachedGroup[i]);

                min=Vector3.Min(min,p);max=Vector3.Max(max,p);

            }

            nodeBuffer.SetData(nodes,0,0,total);

            int countA=0,countB=0;

            for(int a=0;a<world.Count;a++)

            {

                if(skinned[a]||world.VisualArchetype[a]==7)continue;

                int packed=a|((world.Anatomy[a]&1)<<16)|((world.VisualArchetype[a]&15)<<17);

                if(world.Faction[a]==0)actorIndices[countA++]=packed;

                else actorIndices[capacity+countB++]=packed;

            }

            actorBuffer.SetData(actorIndices);

            Bounds bounds=new Bounds((min+max)*.5f,max-min+Vector3.one*2f);

            var pipeline=GraphicsSettings.currentRenderPipeline;

            float relative=pipeline && pipeline.GetType().Name.Contains("HDRenderPipeline")?1:0;

            for(int faction=0;faction<2;faction++)

            {

                int count=faction==0?countA:countB;

                if(count==0)continue;

                var material=paletteMaterials[faction];

                material.SetInt("_ActorOffset",faction*capacity);

                material.SetFloat("_CameraRelative",relative);

                Graphics.DrawMeshInstancedProcedural(tube,0,material,bounds,count*17,null,ShadowCastingMode.Off,false);

                batches++;

            }

            int shotCount=0;

            if(shots!=null)for(int i=0;i<OUTMotorProjectiles.Capacity;i++)

            {

                if(!shots.IsActive(i))continue;

                float r=shots.GetRadius(i);

                bool arrow=shots.GetKind(i)==OUTMotorProjectileKind.Arrow;

                bool bullet=shots.GetKind(i)==OUTMotorProjectileKind.Bullet;

                matrices[shotCount++]=Matrix4x4.TRS(shots.GetPosition(i),(arrow||bullet)?Quaternion.FromToRotation(Vector3.up,shots.GetVelocity(i).normalized):Quaternion.identity,

                    bullet?new Vector3(.025f,.22f,.025f):arrow?new Vector3(.018f,.55f,.018f):new Vector3(r,r*2,r));

            }

            if(shotCount>0)Flush(materials[1],shotCount);

        }

        private void Flush(Material material,int count)

        {

            Graphics.DrawMeshInstanced(tube,0,material,matrices,count,null,ShadowCastingMode.Off,false);

            batches++;

        }

        private static Mesh CreateSixSidedTube()

        {

            const int sides=6;

            const int rings=3;

            var vertices=new Vector3[sides*rings+2];

            var tris=new int[sides*18];

            var normals=new Vector3[vertices.Length];

            for(int i=0;i<sides;i++)

            {

                float t=i*2f*Mathf.PI/sides;float x=Mathf.Cos(t),z=Mathf.Sin(t);

                for(int ring=0;ring<rings;ring++)

                {

                    float taper=ring==1?1f:.82f;

                    vertices[i*rings+ring]=new Vector3(x*taper,ring*.5f-.5f,z*taper);

                    normals[i*rings+ring]=new Vector3(x,0,z);

                }

            }

            int bottom=sides*rings,top=bottom+1;vertices[bottom]=new Vector3(0,-.5f,0);vertices[top]=new Vector3(0,.5f,0);

            normals[bottom]=Vector3.down;normals[top]=Vector3.up;

            int n=0;

            for(int i=0;i<sides;i++)

            {

                int j=(i+1)%sides,a=i*rings,b=j*rings;

                for(int ring=0;ring<rings-1;ring++)

                {

                    tris[n++]=a+ring;tris[n++]=a+ring+1;tris[n++]=b+ring+1;

                    tris[n++]=a+ring;tris[n++]=b+ring+1;tris[n++]=b+ring;

                }

                tris[n++]=bottom;tris[n++]=a;tris[n++]=b;

                tris[n++]=top;tris[n++]=b+rings-1;tris[n++]=a+rings-1;

            }

            var mesh=new Mesh{name="OUT Motor / six-sided bone proxy"};

            mesh.vertices=vertices;mesh.normals=normals;mesh.triangles=tris;mesh.RecalculateBounds();

            return mesh;

        }

        public void Dispose()

        {

            equipment.Dispose();

            if(Application.isPlaying)UnityEngine.Object.Destroy(bloodMaterial);else UnityEngine.Object.DestroyImmediate(bloodMaterial);

            if(nodeBuffer!=null) { nodeBuffer.Release();nodeBuffer=null; }

            if(actorBuffer!=null) { actorBuffer.Release();actorBuffer=null; }

            for(int i=0;i<2;i++)if(paletteMaterials[i])

            {

                if(Application.isPlaying)UnityEngine.Object.Destroy(paletteMaterials[i]);

                else UnityEngine.Object.DestroyImmediate(paletteMaterials[i]);

            }

            if(Application.isPlaying)UnityEngine.Object.Destroy(tube);else UnityEngine.Object.DestroyImmediate(tube);

            for(int i=0;i<2;i++)if(ownsMaterial[i])

            {

                if(Application.isPlaying)UnityEngine.Object.Destroy(materials[i]);

                else UnityEngine.Object.DestroyImmediate(materials[i]);

            }

        }

    }

}
