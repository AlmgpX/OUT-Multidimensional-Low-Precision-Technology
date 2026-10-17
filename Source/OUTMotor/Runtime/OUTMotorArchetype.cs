// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using UnityEngine;



namespace OUT.Motor

{

    public enum OUTMotorEquipment : byte { None, Baton, Rifle, Stone, Flag, Sword }



    /// <summary>Shared authoring data. Applied once at spawn, never cloned per unit.</summary>

    [CreateAssetMenu(fileName="CrowdArchetype", menuName="OUT Motor/Crowd archetype")]

    public sealed class OUTMotorArchetype : ScriptableObject

    {

        [Header("Body")]

        public OUTMotorAnatomy anatomy=OUTMotorAnatomy.Human;

        [Min(20)] public float mass=70;

        [Range(.2f,2f)] public float strength=1;

        [Min(1)] public float health=100;

        [Header("Movement")]

        public OUTMotorMotionFamily motion=OUTMotorMotionFamily.Walk;

        [Range(0,3)] public float speed=.65f;

        [Header("Equipment")]

        public OUTMotorEquipment equipment;

        public OUTMotorEquipment variant;

        [Tooltip("0 disables variation; 4 equips every fourth unit with the variant.")]

        [Range(0,32)] public int variantEvery;

        [Header("Combat")]

        [Min(.1f)] public float attackInterval=1;

        [Min(0)] public float damage=7;

        [Min(0)] public float impulse=35;



        public OUTMotorEquipment EquipmentAt(int index) => variantEvery>0 && index%variantEvery==0 ? variant : equipment;

        public static byte Appearance(OUTMotorEquipment value) => value==OUTMotorEquipment.Baton?(byte)5:

            value==OUTMotorEquipment.Rifle?(byte)6:value==OUTMotorEquipment.Flag?(byte)8:

            value==OUTMotorEquipment.Stone?(byte)9:value==OUTMotorEquipment.Sword?(byte)10:(byte)0;

        public void Apply(OUTMotorWorld world,int actor,int variantIndex=0)

        {

            world.SetAnatomy(actor,anatomy);

            world.SetMotionFamily(actor,motion);

            world.Strength[actor]=Mathf.Clamp(strength,.2f,2);

            world.Health[actor]=Mathf.Max(1,health);

            world.VisualArchetype[actor]=Appearance(EquipmentAt(variantIndex));

            var item=EquipmentAt(variantIndex);

            world.Role[actor]=(byte)(item==OUTMotorEquipment.Stone||item==OUTMotorEquipment.Flag||item==OUTMotorEquipment.Rifle ? OUTMotorRole.Thrower:OUTMotorRole.Zombie);

        }

    }

}
