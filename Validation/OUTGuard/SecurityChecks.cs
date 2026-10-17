// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using System;

using System.IO;

using System.Text.Json;

using OUT.Guard.Diagnostics;

var results = GuardSecurityChecks.Run(); int failed = 0;

foreach (var result in results) { Console.WriteLine((result.passed ? "PASS " : "FAIL ") + result.name); if (!result.passed) failed++; }

if (args.Length > 0) File.WriteAllText(args[0], JsonSerializer.Serialize(results, new JsonSerializerOptions { IncludeFields = true, WriteIndented = true }));

Console.WriteLine(results.Count + " cases, " + failed + " failed."); return failed == 0 ? 0 : 1;
