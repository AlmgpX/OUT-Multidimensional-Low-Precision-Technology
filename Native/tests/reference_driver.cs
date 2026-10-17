// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
using System;
using System.IO;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Security.Cryptography;
using OUT.Guard;
using OUT.Motor;
using UnityEngine;
static class ReferenceDriver {
 static readonly byte[] TestKey=Enumerable.Range(0,32).Select(x=>(byte)x).ToArray();
 static readonly byte[] State=Encoding.UTF8.GetBytes("OUT interoperability / Я / 日本 / \0");
 static string Hex(byte[] b)=>Convert.ToHexString(b).ToLowerInvariant();
 static void Row(string tag,params object[] values)=>Console.WriteLine(tag+"\t"+string.Join("\t",values.Select(v=>v is double d?d.ToString("R",CultureInfo.InvariantCulture):v is float f?f.ToString("R",CultureInfo.InvariantCulture):v is bool b?b?"1":"0":Convert.ToString(v,CultureInfo.InvariantCulture))));
 static Guid Context=new Guid(Enumerable.Repeat((byte)0x22,16).ToArray());
 static byte[] Model(){var payload=new byte[GuardTemporalModel.PayloadBytes];for(int table=0;table<5;table++)for(int i=0;i<4096;i++){int v=table==4?(i%13==0?0:15):((i*7+table*3)%16);payload[table*2048+i/2]|=(byte)(v<<((i&1)*4));}return GuardTemporalModel.Encode(new Guid(Enumerable.Repeat((byte)0x11,16).ToArray()),Context,.05,.5,.4,45000,2,payload);}
 static void Interop(string dir,bool verify){Directory.CreateDirectory(dir);using var codec=new GuardCodec(TestKey);if(verify){if(!codec.TryDecode(Convert.FromHexString(File.ReadAllText(Path.Combine(dir,"native-envelope.hex"))),"interop/slot",out var result)||!result.SequenceEqual(State))throw new Exception("C# could not decode native envelope");Console.WriteLine("PASS C# decoded native AES/HMAC envelope");return;}
  File.WriteAllText(Path.Combine(dir,"csharp-envelope.hex"),Hex(codec.Encode(State,"interop/slot")));using var rsa=RSA.Create(2048);var key=rsa.ExportParameters(false);Directory.CreateDirectory(Path.Combine(dir,"release"));File.WriteAllBytes(Path.Combine(dir,"release/test.txt"),State);var record=GuardIntegrity.SignatureRecord("release/test.txt",Hex(SHA256.HashData(State)),"native-test");File.WriteAllText(Path.Combine(dir,"rsa-modulus.hex"),Hex(key.Modulus));File.WriteAllText(Path.Combine(dir,"rsa-exponent.hex"),Hex(key.Exponent));File.WriteAllText(Path.Combine(dir,"rsa-signature.hex"),Hex(rsa.SignData(record,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1)));Console.WriteLine("Generated transient C# interoperability fixtures");
 }
 static void Main(string[] args){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;if(args.Length>0){Interop(args[1],args[0]=="--interop-verify");return;}
  Row("K",Hex(GuardCodec.DeriveUserKey(TestKey,"alice","app")));var challenge=new GuardChallenge{sequence=7,nonce=Convert.ToBase64String(TestKey),expiresAt=123.5};Row("H",Hex(GuardHandshake.CreateReply(TestKey,challenge,State).authentication));Row("F",Hex(GuardIntegrity.SignatureRecord("release/test.txt",Hex(SHA256.HashData(State)),"native-test")));
  var movement=new GuardMovementTracker();for(int i=0;i<200;i++){double time=i*.05,x=i*.1,y=0,z=Math.Sin(i*.03)*.1;if(i%17==7)x+=20;if(i%23==6)y+=30;var m=movement.Observe(time,x,y,z,i==120);Row("M",m.accepted,m.hasEvidence,m.rebased,m.skipped,(int)m.violations,(int)m.signal,m.speedRatio,m.accelerationRatio,m.verticalRatio,m.predictionRatio,m.tableRisk);}
  var clock=new GuardClock(1,.1,3);clock.Start(0);for(int i=1;i<=160;i++){if(i==45)clock.Suspend(i*.1,out _);if(i==52)clock.Resume(i*.1);bool found=clock.Observe(i*.1,(i/20)%2==0?.2:.05,out double ratio);Row("C",found,ratio);}
  var temporal=new GuardTemporalRisk(GuardTemporalModel.Load(Model(),Context));for(int i=0;i<120;i++){double speed=(i%19)/9.0,acceleration=(i%7)/4.0;var a=temporal.Observe(new GuardTemporalObservation(i*.05,speed,acceleration,.2,.1),Context);Row("T",(int)a.status,(int)a.reasons,a.normal,a.lag,a.anomaly,a.uncertain,a.support,a.reviewSuggested);}
  var world=new GuardAuthoritativeWorld(new[]{new GuardBox(new GuardVector(-50,-1,-50),new GuardVector(50,0,50)),new GuardBox(new GuardVector(2,0,-2),new GuardVector(2.02,5,2))});world.AddActor("a",new GuardVector(-3,.9,0),0);
  for(int i=1;i<=180;i++){double t=(i-1)/60.0;var s=world.Snapshot("a",t);var status=world.SubmitInput("a",new GuardServerInput{sequence=s.nextInputSequence,acknowledgedTick=s.tick,epoch=s.epoch,moveX=1,moveZ=i>70&&i<130?.4:0,jump=i%20==0,dash=i%30==0},t);s=world.Snapshot("a",i/60.0);Row("S",(int)status,s.position.x,s.position.y,s.position.z,s.velocity.x,s.velocity.y,s.velocity.z,s.grounded,s.epoch,s.health);}
  for(int id=0;id<128;id++)for(int sample=0;sample<5;sample++)foreach(int joint in new[]{0,3,6,9,11,12,13,15,16,17}){float t=(sample-1)*.37f;var v=OUTMotorMotionCache.SampleNormalized(id,t,joint);Row("P",v.x,v.y,v.z,OUTMotorMotionCache.GetFootContactMask(id,t));}
  var planner=new OUTMotorFootPlanner(1);var goals=new Vector3[18];var velocities=new Vector3[18];var detached=new byte[18];var heading=Quaternion.Yaw(30);for(int j=0;j<18;j++)goals[j]=heading*OUTMotorData.Pose(0,j);planner.ResetActor(0,goals,0,heading);
  for(int i=0;i<60;i++){float t=i/30f;Vector3 anchor=new Vector3(i*.01f,0,i*.02f);for(int j=0;j<18;j++)goals[j]=anchor+heading*OUTMotorMotionCache.SampleNormalized(8,t,j);planner.Apply(0,.05f,t,OUTMotorMotionCache.GetFootContactMask(8,t),OUTMotorMotionCache.GetClip(8).Duration,new Vector3(.2f,0,.4f),heading,anchor,goals,velocities,detached,0,false);foreach(int j in new[]{11,12,13,15,16,17})Row("G",goals[j].x,goals[j].y,goals[j].z,velocities[j].x,velocities[j].y,velocities[j].z);}
 }
}
