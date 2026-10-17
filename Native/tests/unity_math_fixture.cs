// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
// Standalone test-only arithmetic shim, not Unity or a replacement Unity runtime.
using System;
namespace UnityEngine {
public static class Mathf {
 public const float PI=(float)Math.PI;
 public static float Sin(float x)=>MathF.Sin(x);public static float Cos(float x)=>MathF.Cos(x);public static float Sqrt(float x)=>MathF.Sqrt(x);public static float Floor(float x)=>MathF.Floor(x);public static float Abs(float x)=>MathF.Abs(x);
 public static float Min(float a,float b)=>MathF.Min(a,b);public static int Min(int a,int b)=>Math.Min(a,b);public static float Max(float a,float b)=>MathF.Max(a,b);public static int Max(int a,int b)=>Math.Max(a,b);
 public static float Clamp(float v,float a,float b)=>MathF.Max(a,MathF.Min(b,v));public static float Clamp01(float v)=>Clamp(v,0,1);public static float Repeat(float v,float n)=>v-MathF.Floor(v/n)*n;
}
public struct Vector3 {
 public float x,y,z;public Vector3(float x,float y,float z){this.x=x;this.y=y;this.z=z;}
 public static Vector3 zero=>new Vector3();public static Vector3 forward=>new Vector3(0,0,1);public static Vector3 right=>new Vector3(1,0,0);public static Vector3 up=>new Vector3(0,1,0);
 public float sqrMagnitude=>x*x+y*y+z*z;public float magnitude=>MathF.Sqrt(sqrMagnitude);public Vector3 normalized{get{float n=magnitude;return n>1e-5f?this/n:zero;}}
 public static Vector3 operator +(Vector3 a,Vector3 b)=>new Vector3(a.x+b.x,a.y+b.y,a.z+b.z);public static Vector3 operator -(Vector3 a,Vector3 b)=>new Vector3(a.x-b.x,a.y-b.y,a.z-b.z);public static Vector3 operator *(Vector3 a,float b)=>new Vector3(a.x*b,a.y*b,a.z*b);public static Vector3 operator /(Vector3 a,float b)=>new Vector3(a.x/b,a.y/b,a.z/b);
 public static float Distance(Vector3 a,Vector3 b)=>(a-b).magnitude;public static float Dot(Vector3 a,Vector3 b)=>a.x*b.x+a.y*b.y+a.z*b.z;public static Vector3 LerpUnclamped(Vector3 a,Vector3 b,float t)=>a+(b-a)*t;
 public static Vector3 ProjectOnPlane(Vector3 v,Vector3 n){float d=n.sqrMagnitude;return d>1e-8f?v-n*(Dot(v,n)/d):v;}
}
public struct Quaternion {
 public float x,y,z,w;public static Quaternion Yaw(float degrees){float half=degrees*Mathf.PI/360;return new Quaternion{y=MathF.Sin(half),w=MathF.Cos(half)};}
 public static Vector3 operator *(Quaternion q,Vector3 v){float a=q.x*2,b=q.y*2,c=q.z*2,xx=q.x*a,yy=q.y*b,zz=q.z*c,xy=q.x*b,xz=q.x*c,yz=q.y*c,wx=q.w*a,wy=q.w*b,wz=q.w*c;return new Vector3((1-(yy+zz))*v.x+(xy-wz)*v.y+(xz+wy)*v.z,(xy+wz)*v.x+(1-(xx+zz))*v.y+(yz-wx)*v.z,(xz-wy)*v.x+(yz+wx)*v.y+(1-(xx+yy))*v.z);}
}
}
namespace OUT.Motor {
using UnityEngine;
// Independently repeated small synthetic test fixture; no shipped OUT Motor bank.
public static class OUTMotorData {
 public const int Joints=18;public static readonly int[] Mirrored=new int[18];public static readonly float[] Radii={.12f,.13f,.16f,.145f,.075f,.065f,.068f,.075f,.065f,.068f,.1f,.078f,.073f,.052f,.1f,.078f,.073f,.052f};
 static OUTMotorData(){for(int j=0;j<18;j++)Mirrored[j]=j>=4&&j<=6?j+3:j>=7&&j<=9?j-3:j>=10&&j<=13?j+4:j>=14?j-4:j;}
 public static Vector3 Pose(int p,int j){Vector3 v;if(j<4)v=new Vector3(0,.95f+j*.24f,0);else if(j<10){int k=(j-4)%3;float side=j<7?-1:1;v=new Vector3(side*(.2f+k*.2f),1.45f-k*.2f,.02f*k);}else{int k=(j-10)%4;float side=j<14?-1:1;v=new Vector3(side*.13f,k==0?.92f:k==1?.5f:k==2?.085f:.055f,k==3?.18f:k==1?.02f:0);}if(p==2)v.y*=.72f;if(p>=8&&p<=15){float y=v.y;v.y=.12f+MathF.Abs(v.x)*.2f;v.z+=y*.72f;}return v;}
 public static Vector3 Blend(int p,int q,float t,int j)=>Vector3.LerpUnclamped(Pose(p,j),Pose(q,j),t);
}
}
