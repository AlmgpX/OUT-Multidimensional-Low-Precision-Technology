# Validation scope

Publisher-authorized checks can run from the repository root:

    dotnet run --project Validation/OUTGuard/Core.csproj -c Release
    dotnet run --project Validation/OUTGuard/Security.csproj -c Release
    python Tools/audit_publication.py

These runners inspect the source subset without the excluded fixed shipped model.
They do not certify this as a complete Unity package, evaluate the absent baseline,
measure real-game detection, or establish Motor performance. The Motor test source
requires Unity, NUnit and the corresponding licensed data-bearing product files;
it was not executed as a complete asset in this repository.

The native CMake target contains source only. A fresh native build has not been claimed.
Use the purchased complete product and its validation instructions for integration.
See CURRENT_RESULTS.txt for commands actually run against this snapshot. Public review
permission does not authorize running these tools without a separate applicable license;
the publisher ran them under the publisher's own authorization.
