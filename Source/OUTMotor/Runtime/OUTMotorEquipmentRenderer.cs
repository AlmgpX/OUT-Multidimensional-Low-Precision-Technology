// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.Collections.Generic;

using UnityEngine;

using UnityEngine.Rendering;

namespace OUT.Motor

{

    /// <summary>Six shared cosmetic meshes, one instanced batch per equipment type.

    /// No actor objects/materials; matrices attach directly to simulated head/hand joints.</summary>

    internal sealed class OUTMotorEquipmentRenderer : IDisposable

    {

        private const int Batch=1023;

        private readonly Mesh[] meshes=new Mesh[6];

        private readonly Material[] materials=new Material[6];

        private readonly Matrix4x4[] matrices=new Matrix4x4[Batch];

        private readonly bool palette;

        public OUTMotorEquipmentRenderer(Shader shader,Material fallback,ComputeBuffer nodes,ComputeBuffer actors)

        {

            palette=shader&&nodes!=null;

            meshes[0]=Cap();meshes[1]=Baton();meshes[2]=Rifle();meshes[3]=Flag();meshes[4]=Stone();meshes[5]=Sword();

            Color[] colors={new Color(.055f,.09f,.17f),new Color(.07f,.075f,.085f),new Color(.12f,.13f,.14f),

                new Color(.78f,.12f,.075f),new Color(.43f,.45f,.44f),new Color(.65f,.69f,.74f)};

            for(int item=0;item<6;item++)

            {

                if(palette&&item>0){materials[item]=materials[0];continue;}

                var material=palette?new Material(shader):new Material(fallback);

                material.name="OUT Motor / shared equipment "+item;material.enableInstancing=true;

                if(palette)

                {

                    material.SetFloat("_EquipmentMode",1);material.SetBuffer("_Nodes",nodes);material.SetBuffer("_Actors",actors);

                }

                else if(material.HasProperty("_BaseColor"))material.SetColor("_BaseColor",colors[item]);

                else material.color=colors[item];

                materials[item]=material;

            }

        }

        public int Draw(OUTMotorWorld world,bool[] skinned,float cameraRelative)

        {

            int draws=0;

            if(palette)materials[0].SetFloat("_CameraRelative",cameraRelative);

            for(int item=0;item<6;item++)

            {

                int count=0;

                for(int actor=0;actor<world.Count;actor++)

                {

                    int type=world.VisualArchetype[actor];

                    if(skinned[actor]||type==7)continue;

                    bool present=item==0?(type==5||type==6):item==1?type==5:item==2?type==6:item==3?type==8:item==4?type==9:type==10;

                    if(!present)continue;

                    Quaternion heading=world.GetHeading(actor),rotation=heading;

                    Vector3 position;

                    if(item==0)

                    {

                        Vector3 up=(world.GetJoint(actor,3)-world.GetJoint(actor,2)).normalized;

                        if(up.sqrMagnitude<.01f)up=Vector3.up;

                        rotation=Quaternion.FromToRotation(heading*Vector3.up,up)*heading;

                        position=world.GetJoint(actor,3)+up*.012f;

                    }

                    else if(item==3)

                    {

                        // Shared wind direction keeps banners readable across opposing headings.

                        position=world.GetJoint(actor,6);rotation=Quaternion.identity;

                    }

                    else

                    {

                        position=world.GetJoint(actor,9);

                        if(item==1||item==5)

                        {

                            OUTMotorWeaponGeometry.GetPose(world,actor,item==1?OUTMotorMeleeWeapon.Baton:OUTMotorMeleeWeapon.Sword,

                                out position,out rotation,out _,out _);

                        }

                    }

                    matrices[count++]=Matrix4x4.TRS(position,rotation,Vector3.one);

                    if(count==Batch){Flush(item,count);draws++;count=0;}

                }

                if(count>0){Flush(item,count);draws++;}

            }

            return draws;

        }

        private void Flush(int item,int count)

        {Graphics.DrawMeshInstanced(meshes[item],0,materials[item],matrices,count,null,ShadowCastingMode.Off,false);}

        public void Dispose()

        {

            for(int item=0;item<6;item++)

            {

                Destroy(meshes[item]);

                if(!palette||item==0)Destroy(materials[item]);

            }

        }

        private static void Destroy(UnityEngine.Object item)

        {if(Application.isPlaying)UnityEngine.Object.Destroy(item);else UnityEngine.Object.DestroyImmediate(item);}

        private static Mesh Cap()

        {

            var b=new Builder();

            b.Cylinder(new Vector3(0,.025f,0),.17f,.075f,8,new Color(.035f,.065f,.13f));

            b.Box(new Vector3(0,-.014f,.105f),new Vector3(.29f,.026f,.24f),new Color(.018f,.022f,.03f));

            b.Box(new Vector3(0,.022f,.164f),new Vector3(.045f,.04f,.012f),new Color(.86f,.68f,.20f));

            return b.Build("OUT Motor / police peaked cap");

        }

        private static Mesh Baton()

        {

            var b=new Builder();

            b.Cylinder(new Vector3(0,.22f,0),.025f,.65f,6,new Color(.045f,.052f,.065f));

            b.Box(new Vector3(.05f,-.02f,0),new Vector3(.14f,.027f,.035f),new Color(.09f,.1f,.12f));

            return b.Build("OUT Motor / police baton");

        }

        private static Mesh Rifle()

        {

            var b=new Builder();var metal=new Color(.095f,.11f,.13f);var wood=new Color(.27f,.13f,.065f);

            b.Box(new Vector3(0,.08f,.11f),new Vector3(.075f,.105f,.38f),metal);

            b.Box(new Vector3(0,.06f,-.22f),new Vector3(.065f,.14f,.30f),wood);

            b.Box(new Vector3(0,.09f,.51f),new Vector3(.025f,.035f,.48f),metal);

            b.Box(new Vector3(0,.065f,.30f),new Vector3(.07f,.08f,.20f),wood);

            b.Box(new Vector3(0,-.06f,.12f),new Vector3(.045f,.23f,.095f),metal);

            b.Box(new Vector3(0,-.055f,-.035f),new Vector3(.05f,.16f,.065f),new Color(.045f,.05f,.055f));

            b.Box(new Vector3(0,.14f,.61f),new Vector3(.018f,.08f,.028f),metal);

            return b.Build("OUT Motor / low-poly rifle");

        }

        private static Mesh Sword()

        {

            var b=new Builder();var steel=new Color(.66f,.71f,.76f);

            b.Box(new Vector3(0,-.045f,0),new Vector3(.047f,.23f,.047f),new Color(.12f,.075f,.045f));

            b.Box(new Vector3(0,.065f,0),new Vector3(.23f,.035f,.055f),new Color(.31f,.35f,.39f));

            b.Box(new Vector3(0,.45f,0),new Vector3(.07f,.74f,.026f),steel);

            Vector3 tip=OUTMotorWeaponGeometry.TipLocal(OUTMotorMeleeWeapon.Sword);

            Vector3 a=new Vector3(-.035f,.82f,-.013f),c=new Vector3(.035f,.82f,.013f);

            b.Triangle(a,tip,new Vector3(.035f,.82f,-.013f),steel);

            b.Triangle(new Vector3(.035f,.82f,-.013f),tip,c,steel);

            b.Triangle(c,tip,new Vector3(-.035f,.82f,.013f),steel);

            b.Triangle(new Vector3(-.035f,.82f,.013f),tip,a,steel);

            b.Cylinder(new Vector3(0,-.18f,0),.032f,.04f,6,new Color(.31f,.35f,.39f));

            return b.Build("OUT Motor / physical-tip sword");

        }

        private static Mesh Flag()

        {

            var b=new Builder();b.Cylinder(new Vector3(0,.52f,0),.016f,1.5f,6,new Color(.20f,.22f,.24f));

            Color cloth=new Color(.82f,.12f,.055f,.5f);

            for(int strip=0;strip<6;strip++)

            {

                float x0=.025f+strip*.14f,x1=x0+.14f;

                Vector3 a=new Vector3(x0,.67f,0),c=new Vector3(x1,1.22f,0);

                b.Quad(a,new Vector3(x1,.67f,0),c,new Vector3(x0,1.22f,0),cloth);

                b.Quad(new Vector3(x0,1.22f,0),c,new Vector3(x1,.67f,0),a,cloth);

            }

            return b.Build("OUT Motor / waving protest flag");

        }

        private static Mesh Stone()

        {

            var b=new Builder();Vector3 top=new Vector3(0,.095f,0),bottom=new Vector3(0,-.07f,0);

            Vector3[] ring={new Vector3(-.085f,0,0),new Vector3(0,0,.075f),new Vector3(.10f,0,0),new Vector3(0,0,-.08f)};

            for(int i=0;i<4;i++)

            {b.Triangle(top,ring[i],ring[(i+1)&3],new Color(.43f+i*.025f,.44f+i*.02f,.41f));

             b.Triangle(bottom,ring[(i+1)&3],ring[i],new Color(.35f,.37f,.36f));}

            return b.Build("OUT Motor / held stone");

        }

        private sealed class Builder

        {

            private readonly List<Vector3> vertices=new List<Vector3>();

            private readonly List<Color> colors=new List<Color>();

            private readonly List<int> indices=new List<int>();

            public void Triangle(Vector3 a,Vector3 b,Vector3 c,Color color)

            {

                int index=vertices.Count;vertices.Add(a);vertices.Add(b);vertices.Add(c);

                colors.Add(color);colors.Add(color);colors.Add(color);indices.Add(index);indices.Add(index+1);indices.Add(index+2);

            }

            public void Quad(Vector3 a,Vector3 b,Vector3 c,Vector3 d,Color color)

            {Triangle(a,b,c,color);Triangle(a,c,d,color);}

            public void Box(Vector3 center,Vector3 size,Color color)

            {

                Vector3 h=size*.5f;

                Vector3 a=center+new Vector3(-h.x,-h.y,-h.z),b=center+new Vector3(h.x,-h.y,-h.z),

                    c=center+new Vector3(h.x,h.y,-h.z),d=center+new Vector3(-h.x,h.y,-h.z),

                    e=center+new Vector3(-h.x,-h.y,h.z),f=center+new Vector3(h.x,-h.y,h.z),

                    g=center+new Vector3(h.x,h.y,h.z),k=center+new Vector3(-h.x,h.y,h.z);

                Quad(a,d,c,b,color);Quad(e,f,g,k,color);Quad(a,e,k,d,color);

                Quad(b,c,g,f,color);Quad(d,k,g,c,color);Quad(a,b,f,e,color);

            }

            public void Cylinder(Vector3 center,float radius,float height,int sides,Color color)

            {

                Vector3 bottom=center-Vector3.up*height*.5f,top=center+Vector3.up*height*.5f;

                for(int side=0;side<sides;side++)

                {

                    float a=side*Mathf.PI*2/sides,b=(side+1)*Mathf.PI*2/sides;

                    Vector3 va=new Vector3(Mathf.Cos(a)*radius,0,Mathf.Sin(a)*radius),vb=new Vector3(Mathf.Cos(b)*radius,0,Mathf.Sin(b)*radius);

                    Quad(bottom+va,top+va,top+vb,bottom+vb,color);

                    Triangle(bottom,bottom+va,bottom+vb,color);Triangle(top,top+vb,top+va,color);

                }

            }

            public Mesh Build(string name)

            {

                var mesh=new Mesh{name=name};mesh.SetVertices(vertices);mesh.SetColors(colors);mesh.SetTriangles(indices,0);

                mesh.RecalculateNormals();mesh.RecalculateBounds();return mesh;

            }

        }

    }

}
