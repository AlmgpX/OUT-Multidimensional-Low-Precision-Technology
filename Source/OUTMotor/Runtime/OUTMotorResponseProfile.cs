// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;
using UnityEngine;

namespace OUT.Motor
{
    [CreateAssetMenu(fileName = "OUTMotorResponseProfile", menuName = "OUT Motor/Response profile")]
    public sealed class OUTMotorResponseProfile : ScriptableObject
    {
        [SerializeField, HideInInspector] private byte[] packedInt4;
        [TextArea] public string provenance = "Unconfigured: uses original response bank.";
        public float trainingMeanSquaredError, holdoutMeanSquaredError;
        public bool IsValid { get { return packedInt4 != null && packedInt4.Length == 128; } }

        // No allocation or decode into a temporary array in the simulation hot path.
        public int GetCode(int cell)
        {
            cell &= 255;
            return IsValid ? (packedInt4[cell >> 1] >> ((cell & 1) * 4)) & 15
                : OUTMotorData.ResponseCode(cell >> 6, (cell >> 4) & 3, (cell >> 2) & 3, cell & 3);
        }

        public void SetPackedBank(byte[] bank)
        {
            if (bank == null || bank.Length != 128) throw new ArgumentException("Response bank must contain 128 packed bytes.", nameof(bank));
            packedInt4 = (byte[])bank.Clone();
        }
    }
}
